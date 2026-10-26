#include "voice_isolation_render.h"
#include "audio_playback.h"
#include "timeline_audio_mix.h"
#include "video_probe.h"
#include "logging/logger.h"
#include <QCryptographicHash>
#include <QDataStream>
#include <QFileInfo>
#include <QDateTime>
#include <cmath>
extern "C" {
#include <libavfilter/avfilter.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
}

namespace media {
namespace {
QString pathString(const std::filesystem::path& path) { return QString::fromStdU16String(path.u16string()); }
void sourceIdentity(QDataStream& stream, const project::ProjectClip& clip, bool file_identity = true) {
    const QFileInfo file(pathString(clip.source_path));
    std::error_code error;
    const auto modified = file_identity ? std::filesystem::last_write_time(clip.source_path, error).time_since_epoch().count() : 0;
    stream << (file_identity ? file.canonicalFilePath() : file.filePath()) << (file_identity ? file.size() : 0) << qint64(error ? 0 : modified)
        << qint64(clip.source_start_frame) << qint64(clip.source_start_time_us) << qint64(clip.source_duration_time_us)
        << qint64(clip.duration_frames);
}
void effects(QDataStream& stream, const std::vector<timeline::AudioEffectInstance>& chain) {
    stream << quint32(chain.size());
    for (const auto& effect : chain) {
        stream << quint32(effect.parameters.index()) << effect.enabled;
        std::visit([&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, timeline::AudioEqualizerParameters>) stream << value.low_shelf_frequency_hz << value.low_shelf_gain_db << value.mid_frequency_hz << value.mid_gain_db << value.mid_q << value.high_shelf_frequency_hz << value.high_shelf_gain_db;
            else if constexpr (std::is_same_v<T, timeline::AudioCompressorParameters>) stream << value.threshold_db << value.ratio << value.attack_ms << value.release_ms << value.makeup_gain_db;
            else if constexpr (std::is_same_v<T, timeline::AudioDialogueCleanupParameters>) stream << value.highpass_frequency_hz << value.deesser_intensity;
            else if constexpr (std::is_same_v<T, timeline::AudioNoiseReductionParameters>) stream << value.reduction_db << value.noise_floor_db;
            else stream << value.hum_frequency_hz << value.declick_enabled << value.declick_sensitivity;
        }, effect.parameters);
    }
}
QString digest(const QByteArray& bytes) { return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()); }
void prefix(QDataStream& stream, double fps, int channels) { stream << "isolation-render-v1-rnnoise-std-v1" << quint32(avfilter_version()) << quint32(avcodec_version()) << quint32(swresample_version()) << fps << channels; }
QString clipKey(const project::ProjectClip& clip, double fps, int channels, bool file_identity = true) {
    QByteArray bytes; QDataStream stream(&bytes, QIODevice::WriteOnly); prefix(stream, fps, channels); sourceIdentity(stream, clip, file_identity); return digest(bytes);
}
QString trackKey(const project::ProjectTrack& track, double fps, int channels, bool file_identity = true) {
    QByteArray bytes; QDataStream stream(&bytes, QIODevice::WriteOnly); prefix(stream, fps, channels);
    for (const auto& clip : track.clips) {
        if ((clip.kind != timeline::ClipKind::Audio && clip.kind != timeline::ClipKind::Video) || clip.audio_extracted) continue;
        sourceIdentity(stream, clip, file_identity);
        stream << qint64(clip.timeline_start_frame) << clip.audio_gain << clip.audio_muted << clip.voice_isolation.enabled << clip.voice_isolation.amount;
        effects(stream, clip.audio_effects);
        stream << quint32(clip.audio_gain_keyframes.size());
        for (const auto& point : clip.audio_gain_keyframes) stream << qint64(point.frame) << point.gain;
    }
    for (const auto& transition : track.transitions) stream << quint64(transition.from_clip_index) << quint64(transition.to_clip_index) << qint64(transition.duration_frames) << qint32(transition.kind);
    return digest(bytes);
}
void cancel(const IsolationCancelled& canceled) { if (canceled && canceled()) throw VoiceIsolationCancelled{}; }
}
bool hasVoiceIsolation(const project::ProjectDocument& document) {
    return std::any_of(document.timeline_tracks.begin(), document.timeline_tracks.end(), [](const auto& track) {
        return track.voice_isolation.enabled || std::any_of(track.clips.begin(), track.clips.end(), [](const auto& clip) { return clip.voice_isolation.enabled && !clip.audio_extracted; });
    });
}
QString voiceIsolationRequestKey(const project::ProjectDocument& document, int channels, bool file_identity) {
    QByteArray bytes; QDataStream stream(&bytes, QIODevice::WriteOnly);
    for (const auto& track : document.timeline_tracks) {
        stream << quint64(track.track_id) << track.voice_isolation.enabled << track.voice_isolation.amount;
        if (track.voice_isolation.enabled) stream << trackKey(track, document.timeline_frame_rate.asDouble(), channels, file_identity);
        for (const auto& clip : track.clips) if (clip.voice_isolation.enabled && !clip.audio_extracted)
            stream << quint64(clip.clip_id) << clipKey(clip, document.timeline_frame_rate.asDouble(), channels, file_identity) << clip.voice_isolation.amount;
    }
    return digest(bytes);
}
std::shared_ptr<const VoiceIsolationContext> prepareVoiceIsolation(
    const project::ProjectDocument& document, const IsolationCancelled& canceled,
    const VoiceIsolationJobProgress& progress, const std::set<VoiceIsolationTarget>& force, int channels) {
    for (const auto& track : document.timeline_tracks) {
        if (!timeline::validVoiceIsolation(track.voice_isolation)) throw std::invalid_argument("Invalid track voice isolation settings.");
        for (const auto& clip : track.clips) if (!timeline::validVoiceIsolation(clip.voice_isolation)) throw std::invalid_argument("Invalid clip voice isolation settings.");
    }
    auto context = std::make_shared<VoiceIsolationContext>();
    if (!hasVoiceIsolation(document)) return context;
    const auto fps = document.timeline_frame_rate.asDouble();
    const auto initial_key = voiceIsolationRequestKey(document, channels);
    auto regenerate = force;
    for (const auto& track : document.timeline_tracks) if (track.voice_isolation.enabled &&
        std::any_of(track.clips.begin(), track.clips.end(), [&](const auto& clip) { return force.contains({true, clip.clip_id}); }))
        regenerate.insert({false, track.track_id});
    const auto build = [&](const QString& key, VoiceIsolationTarget target, std::int64_t first, std::int64_t count, const IsolationSource& reader, const std::function<QString()>& fresh_key) {
        cancel(canceled);
        try {
            if (!regenerate.contains(target)) if (auto cached = findVoiceIsolation(key)) { if (progress) progress(target, 100); return cached; }
            return generateVoiceIsolation(key, first, count, channels, reader, canceled, [&](int value) {
                if (value == 100 && key != fresh_key()) throw VoiceIsolationCancelled{};
                if (progress) progress(target, value);
            });
        } catch (const VoiceIsolationCancelled&) { throw; }
        catch (const std::exception& error) {
            cancel(canceled);
            logging::Logger::instance().log(logging::Level::Error, "voice_isolation", "prepare_target", error.what(),
                {{target.first ? "clip_id" : "track_id", std::to_string(target.second)}, {"key", key.toStdString()}, {"model_id", "rnnoise-std-v1"}});
            throw;
        }
    };
    for (const auto& track : document.timeline_tracks) for (const auto& clip : track.clips) {
        if (!clip.voice_isolation.enabled || clip.audio_extracted || (clip.kind != timeline::ClipKind::Audio && clip.kind != timeline::ClipKind::Video)) continue;
        cancel(canceled);
        auto decoder = AudioPlaybackSession::open(clip.source_path, {48000, channels});
        if (!decoder->has_audio()) continue;
        const auto metadata = VideoProbe{}.probe(clip.source_path);
        const auto source_fps = metadata.frame_rate.value_or(fps);
        const auto first = static_cast<std::int64_t>(std::llround(clip.kind == timeline::ClipKind::Audio ? clip.source_start_time_us * .048 : clip.source_start_frame * 48000.0 / source_fps));
        const auto count = static_cast<std::int64_t>(std::llround(clip.duration_frames * 48000.0 / fps));
        const auto artifact = build(clipKey(clip, fps, channels), {true, clip.clip_id}, first, count,
            [&](auto start, auto size) { return readNormalizedAudio(*decoder, start, size, canceled); }, [&] { return clipKey(clip, fps, channels); });
        context->clips[clip.clip_id] = {artifact, clip.voice_isolation.amount};
    }
    for (const auto& track : document.timeline_tracks) {
        if (!track.voice_isolation.enabled) continue;
        std::vector<TimelineAudioMixClip> clips;
        std::vector<TimelineAudioMixTransition> transitions;
        std::unordered_map<std::size_t, std::unique_ptr<AudioPlaybackSession>> decoders;
        std::int64_t end = 0;
        for (std::size_t index = 0; index < track.clips.size(); ++index) {
            cancel(canceled); const auto& clip = track.clips[index];
            if ((clip.kind != timeline::ClipKind::Audio && clip.kind != timeline::ClipKind::Video) || clip.audio_extracted) continue;
            const auto metadata = VideoProbe{}.probe(clip.source_path);
            if (!metadata.audio) continue;
            TimelineAudioMixClip mix;
            mix.has_audio = true; mix.kind = clip.kind; mix.source_index = index; mix.track_index = 0; mix.clip_index = index;
            mix.track_id = track.track_id; mix.clip_id = clip.clip_id;
            mix.timeline_start_frame = clip.timeline_start_frame; mix.duration_frames = clip.duration_frames;
            mix.source_start_frame = clip.source_start_frame; mix.source_frame_rate = metadata.frame_rate.value_or(fps);
            mix.source_start_time_us = clip.source_start_time_us; mix.source_duration_time_us = clip.source_duration_time_us;
            mix.clip_gain = clip.audio_gain; mix.clip_muted = clip.audio_muted; mix.audio_gain_keyframes = clip.audio_gain_keyframes;
            mix.clip_audio_effects = clip.audio_effects; mix.source_channel_count = channels;
            clips.push_back(std::move(mix)); end = std::max(end, clip.timeline_start_frame + clip.duration_frames);
        }
        if (clips.empty()) continue;
        for (const auto& item : track.transitions) transitions.push_back({0, static_cast<std::int64_t>(item.to_clip_index),
            track.clips[item.to_clip_index].timeline_start_frame + item.duration_frames, item.kind,
            static_cast<std::int64_t>(item.from_clip_index), item.duration_frames});
        TimelineAudioMixConfiguration config; config.apply_solo = false; config.voice_isolation = context;
        // Clip artifacts are available; the track artifact being generated is
        // deliberately absent. Track effects, fader and routes stay neutral.
        TimelineAudioMixState state;
        const auto reader = [&](std::size_t index, std::int64_t start, std::size_t count) {
            cancel(canceled); auto& decoder = decoders[index];
            if (!decoder) decoder = AudioPlaybackSession::open(track.clips[index].source_path, {48000, channels});
            return readNormalizedAudio(*decoder, start, count, canceled);
        };
        const auto input = [&](std::int64_t start, std::size_t count) {
            auto result = TimelineAudioMixAccumulator::render(clips, transitions, fps, 48000, channels, start, count, config, state, reader).samples;
            std::erase_if(decoders, [&](const auto& item) {
                const auto& clip = track.clips[item.first];
                return start + count > static_cast<long double>(clip.timeline_start_frame + clip.duration_frames) * 48000 / fps + 48000;
            });
            return result;
        };
        const auto artifact = build(trackKey(track, fps, channels), {false, track.track_id}, 0,
            static_cast<std::int64_t>(std::llround(end * 48000.0 / fps)), input, [&] { return trackKey(track, fps, channels); });
        context->tracks[track.track_id] = {artifact, track.voice_isolation.amount};
    }
    cancel(canceled);
    if (initial_key != voiceIsolationRequestKey(document, channels)) throw VoiceIsolationCancelled{};
    pruneVoiceIsolationCache();
    return context;
}
} // namespace media

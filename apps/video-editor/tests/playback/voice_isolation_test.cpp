#include "media/voice_isolation_render.h"
#include "media/audio_playback.h"
#include "media/timeline_audio_mix.h"
#include "application/voice_isolation_coordinator.h"
#include "application/timeline_command_service.h"
#include "application/editor_session.h"
#include "project/project_file.h"
#include "workspaces/audio/ui/voice_isolation_panel.h"
#include <QApplication>
#include <QCryptographicHash>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QDir>
#include <chrono>
#include <QFile>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <cmath>
#include <iostream>
#include <numeric>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
QString key(const char* name) { return QString::fromLatin1(QCryptographicHash::hash(name, QCryptographicHash::Sha256).toHex()); }
double rms(const std::vector<float>& pcm, std::size_t start = 0) {
    double energy = 0; for (auto i = start; i < pcm.size(); ++i) energy += pcm[i] * pcm[i];
    return std::sqrt(energy / (pcm.size() - start));
}
float noise(std::int64_t sample) {
    auto word = static_cast<std::uint32_t>(sample) * 747796405U + 2891336453U;
    word = ((word >> ((word >> 28U) + 4U)) ^ word) * 277803737U;
    return static_cast<float>(((word >> 22U) ^ word) / 4294967295.0 - .5) * .04F;
}
void waitUntil(const std::function<bool()>& done) {
    QElapsedTimer timer; timer.start();
    while (!done() && timer.elapsed() < 15000) { QApplication::processEvents(); QThread::msleep(1); }
    require(done(), "Background isolation did not finish within the bounded test timeout.");
}
}
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QTemporaryDir directory;
    qputenv("CREATIVE_SUITE_VOICE_ISOLATION_CACHE_ROOT", (directory.path() + "/cache").toUtf8());
    try {
        require(argc > 1 && directory.isValid(), "Missing licensed speech fixture.");
        auto decoder = media::AudioPlaybackSession::open(argv[1], {48000, 1});
        const auto speech = media::readNormalizedAudio(*decoder, 0, 96000);
        const auto input = [&](std::int64_t start, std::size_t count) {
            std::vector<float> pcm(count);
            for (std::size_t i = 0; i < count; ++i) pcm[i] = (start + i < speech.size() ? speech[start + i] : 0) + noise(start + i);
            return pcm;
        };
        const auto artifact = media::generateVoiceIsolation(key("noisy"), 0, 240000, 1, input, [] { return false; });
        require(artifact != nullptr, "Isolation did not publish a complete artifact.");
        auto wet = artifact->read(0, 240000, 48000, 1), dry = input(0, 240000);
        require(wet.size() == dry.size() && rms(wet, 192000) < rms(dry, 192000) * .7, "Neural processing failed to attenuate noise-only sections.");
        const auto whole = artifact->read(0, 96000, 48000, 1);
        std::vector<float> blocks;
        for (std::size_t start = 0; start < 96000; start += 997) {
            const auto block = artifact->read(start, std::min<std::size_t>(997, 96000 - start), 48000, 1);
            blocks.insert(blocks.end(), block.begin(), block.end());
        }
        require(whole == blocks && artifact->read(95900, 100, 48000, 1) == std::vector<float>(whole.end() - 100, whole.end()), "Cache block boundaries or seeks changed PCM.");
        const auto clean = media::generateVoiceIsolation(key("clean"), 0, 96000, 1, [&](auto start, auto count) {
            std::vector<float> output(count); for (std::size_t i = 0; i < count; ++i) if (start + i < speech.size()) output[i] = speech[start + i]; return output;
        }, {})->read(0, 96000, 48000, 1);
        double dot = 0, a = 0, b = 0;
        for (std::size_t i = 4800; i < clean.size(); ++i) { dot += clean[i] * speech[i]; a += clean[i] * clean[i]; b += speech[i] * speech[i]; }
        require(dot / std::sqrt(a * b) > .75 && rms(clean) > rms(speech) * .4, "Neural processing did not preserve clean speech or left uncompensated delay.");
        require(artifact->read(0, 4410, 44100, 2).size() == 8820, "Cache resampling did not preserve the requested layout/duration.");
        const auto surround = media::generateVoiceIsolation(key("surround"), 480, 997, 6,
            [](auto, auto count) { return std::vector<float>(count * 6, 0); }, {});
        require(surround->read(480, 997, 48000, 6) == std::vector<float>(997 * 6, 0), "Multichannel silence or final partial block changed duration/alignment.");
        struct ConstantPcm final : media::VoiceIsolationPcm {
            float value; explicit ConstantPcm(float v) : value(v) {}
            std::vector<float> read(std::int64_t, std::size_t count, int, int channels) const override { return std::vector<float>(count * channels, value); }
        };
        media::TimelineAudioMixClip mix_clip;
        mix_clip.has_audio = true; mix_clip.kind = timeline::ClipKind::Audio;
        mix_clip.clip_id = mix_clip.track_id = 1; mix_clip.duration_frames = 30; mix_clip.source_duration_time_us = 1000000;
        mix_clip.clip_gain = .5; mix_clip.track_gain = .5;
        auto prepared = std::make_shared<media::VoiceIsolationContext>();
        prepared->clips[1] = {std::make_shared<ConstantPcm>(.1F), .5};
        prepared->tracks[1] = {std::make_shared<ConstantPcm>(.04F), .25};
        media::TimelineAudioMixConfiguration configuration; configuration.voice_isolation = prepared;
        media::TimelineAudioMixState mix_state;
        auto mixed = media::TimelineAudioMixAccumulator::render(std::span(&mix_clip, 1), {}, 30, 48000, 2, 0, 480,
            configuration, mix_state, [](auto, auto, auto count) { return std::vector<float>(count * 2, .2F); });
        require(std::abs(mixed.samples.back() - .033125F) < .00001F, "Clip/track isolation mixing ran at the wrong stage or was applied twice.");
        const auto resampled_whole = artifact->read(0, 88200, 44100, 2);
        std::vector<float> resampled_blocks;
        for (std::size_t start = 0; start < 88200; start += 997) {
            const auto block = artifact->read(start, std::min<std::size_t>(997, 88200 - start), 44100, 2);
            resampled_blocks.insert(resampled_blocks.end(), block.begin(), block.end());
        }
        require(resampled_blocks == resampled_whole, "Resampling changed with cache block sizes.");
        const auto seek = artifact->read(50000, 997, 44100, 2);
        require(seek == std::vector<float>(resampled_whole.begin() + 100000, resampled_whole.begin() + 101994), "Cache resampling seek changed sample alignment.");
        bool canceled = false;
        try { media::generateVoiceIsolation(key("cancel"), 0, 48000, 1, input, [] { return true; }); } catch (const media::VoiceIsolationCancelled&) { canceled = true; }
        require(canceled && !media::findVoiceIsolation(key("cancel")), "Cancelled generation published partial PCM.");
        auto hit = media::findVoiceIsolation(key("noisy")); require(hit == artifact, "Cache lookup did not lease the completed artifact.");
        media::pruneVoiceIsolationCache(0); require(media::findVoiceIsolation(key("noisy")) != nullptr, "LRU evicted an active artifact.");

        project::ProjectDocument document;
        project::ProjectTrack track; track.track_id = 1; track.kind = timeline::TrackKind::Audio; track.name = "Voice"; track.voice_isolation.enabled = true;
        project::ProjectClip clip; clip.clip_id = 1; clip.kind = timeline::ClipKind::Audio; clip.source_path = argv[1]; clip.duration_frames = 30;
        clip.source_duration_time_us = 1000000; clip.voice_isolation.enabled = true; track.clips.push_back(clip); document.timeline_tracks.push_back(track);
        auto context = media::prepareVoiceIsolation(document, [] { return false; });
        require(context->clips.size() == 1 && context->tracks.size() == 1, "Clip/track dependency preparation lost a stage.");
        std::set<media::VoiceIsolationTarget> regenerated;
        context = media::prepareVoiceIsolation(document, {}, [&](auto target, int value) { if (value < 100) regenerated.insert(target); }, {{true, 1}});
        require(regenerated.contains({true, 1}) && regenerated.contains({false, 1}), "Forced clip regeneration did not refresh its dependent track.");
        const auto before_move = context->clips.at(1).audio;
        document.timeline_tracks[0].clips[0].timeline_start_frame = 30;
        context = media::prepareVoiceIsolation(document, {});
        require(context->clips.at(1).audio == before_move, "Moving a clip re-ran source isolation.");
        const auto recipe = media::voiceIsolationRequestKey(document);
        document.timeline_tracks[0].audio_gain = .5; document.timeline_tracks[0].audio_pan = .5;
        require(recipe == media::voiceIsolationRequestKey(document), "Post-isolation fader/pan invalidated the cache.");
        document.timeline_tracks[0].clips[0].audio_gain = .5;
        require(recipe != media::voiceIsolationRequestKey(document), "Clip gain did not invalidate track isolation.");

        const auto corrupted_key = key("corrupted");
        { auto cached = media::generateVoiceIsolation(corrupted_key, 0, 997, 1, input, {}); }
        QFile corrupt(media::voiceIsolationCacheRoot() + "/" + corrupted_key + ".vpcm");
        require(corrupt.open(QIODevice::ReadWrite) && corrupt.seek(520) && corrupt.write("bad!") == 4, "Could not inject cache corruption."); corrupt.close();
        require(!media::findVoiceIsolation(corrupted_key), "Corrupt cached PCM was accepted.");
        { QFile blocker(directory.path() + "/blocker"); require(blocker.open(QIODevice::WriteOnly), "Could not create cache I/O failure fixture."); }
        qputenv("CREATIVE_SUITE_VOICE_ISOLATION_CACHE_ROOT", (directory.path() + "/blocker/cache").toUtf8());
        bool failed = false;
        try { media::generateVoiceIsolation(key("io-error"), 0, 997, 1, input, {}); } catch (const std::exception&) { failed = true; }
        require(failed, "An unavailable cache directory was not reported.");
        qputenv("CREATIVE_SUITE_VOICE_ISOLATION_CACHE_ROOT", (directory.path() + "/cache").toUtf8());

        auto changing = document;
        const auto mutable_source = std::filesystem::path(directory.path().toStdU16String()) / "changing.flac";
        std::filesystem::copy_file(argv[1], mutable_source);
        changing.timeline_tracks[0].clips[0].source_path = mutable_source;
        const auto files_before = QDir(media::voiceIsolationCacheRoot()).entryList({"*.vpcm"}, QDir::Files).size();
        bool changed_source = false, discarded_source = false;
        try {
            media::prepareVoiceIsolation(changing, {}, [&](auto, int value) {
                if (!changed_source && value > 0) {
                    changed_source = true;
                    const auto previous = std::filesystem::last_write_time(mutable_source);
                    std::filesystem::last_write_time(mutable_source, previous + std::chrono::seconds(1));
                }
            });
        } catch (const media::VoiceIsolationCancelled&) { discarded_source = true; }
        require(changed_source && discarded_source && files_before == QDir(media::voiceIsolationCacheRoot()).entryList({"*.vpcm"}, QDir::Files).size(),
            "An input changed during generation published a stale cache artifact.");

        application::VoiceIsolationCoordinator coordinator;
        bool ready = false; int publications = 0;
        QObject::connect(&coordinator, &application::VoiceIsolationCoordinator::contextChanged, [&](auto value) { if (value) { ready = true; ++publications; } });
        coordinator.request(document, 1); waitUntil([&] { return ready; });
        require(publications == 1, "Background preparation published more than one result.");
        coordinator.regenerate({false, 1}); coordinator.cancel();
        QElapsedTimer idle; idle.start(); while (idle.elapsed() < 400) { QApplication::processEvents(); QThread::msleep(1); }
        require(publications == 1, "Cancelling the queue published a result.");

        {
            application::VoiceIsolationCoordinator stale;
            int stale_publications = 0; bool replaced = false;
            QObject::connect(&stale, &application::VoiceIsolationCoordinator::contextChanged, [&](auto result) { if (result) ++stale_publications; });
            QObject::connect(&stale, &application::VoiceIsolationCoordinator::stateChanged, [&](bool, quint64, QString state, int value) {
                if (!replaced && state == "Processing" && value > 0) { replaced = true; stale.request({}, 2); }
            });
            stale.request(document, 1); waitUntil([&] { return replaced; });
            QApplication::processEvents();
            require(stale_publications == 0, "A result from the replaced project was published.");
        }
        ui::VoiceIsolationPanel panel(nullptr); panel.setSettings({});
        int edits = 0; panel.apply = [&](auto value) { require(value.enabled && value.amount == .35, "Voice UI changed the wrong settings."); ++edits; };
        QTimer::singleShot(0, [&] {
            auto* dialog = QApplication::activeModalWidget(); require(dialog != nullptr, "Isolation configuration dialog did not open.");
            dialog->findChild<QCheckBox*>("audioVoiceIsolationEnabled")->setChecked(true);
            dialog->findChild<QSlider*>("audioVoiceIsolationAmount")->setValue(35);
            dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
        });
        panel.findChild<QPushButton*>("audioVoiceIsolationConfigure")->click(); require(edits == 1, "A configuration gesture did not produce one edit.");
        QTimer::singleShot(0, [&] { QApplication::activeModalWidget()->close(); });
        panel.findChild<QPushButton*>("audioVoiceIsolationConfigure")->click(); require(edits == 1, "Cancel committed isolation settings.");
        std::cout << "Voice isolation signal/cache/dependency/coordinator/UI checks passed.\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
    return 0;
}

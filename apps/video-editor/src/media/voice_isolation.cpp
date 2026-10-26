#include "voice_isolation.h"
#include "logging/logger.h"
#include <QCryptographicHash>
#include <QDir>
#include <QDateTime>
#include <cmath>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QtEndian>
#include <algorithm>
#include <bit>
#include <chrono>
#include <cstring>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
extern "C" {
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersrc.h>
#include <libavfilter/buffersink.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

static void initializeVoiceIsolationResource() { Q_INIT_RESOURCE(voice_isolation); }
namespace media {
namespace {
constexpr qint64 header_size = 512;
const QByteArray model_hash("6b8943dc4a9b6b24425873992a44f29c0577503276456af46a8854774faeb294");
std::mutex cache_mutex;
std::timed_mutex inference_mutex;
std::unordered_map<std::string, std::weak_ptr<const VoiceIsolationPcm>> leases;
void check(int code, const char* operation) {
    if (code >= 0) return;
    char error[AV_ERROR_MAX_STRING_SIZE]{}; av_strerror(code, error, sizeof(error));
    logging::Logger::instance().log(logging::Level::Error, "voice_isolation", operation, error,
        {{"error_code", std::to_string(code)}});
    throw std::runtime_error(std::string(operation) + ": " + error);
}
void checkCancel(const IsolationCancelled& canceled) { if (canceled && canceled()) throw VoiceIsolationCancelled{}; }
QString cachePath(const QString& key) {
    if (key.size() != 64 || std::any_of(key.begin(), key.end(), [](QChar c) {
        return !((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')); }))
        throw std::invalid_argument("Invalid voice isolation cache key.");
    return QDir(voiceIsolationCacheRoot()).filePath(key + ".vpcm");
}
struct Graph {
    AVFilterGraph* graph = avfilter_graph_alloc();
    AVFilterContext *source = nullptr, *sink = nullptr;
    explicit Graph(int channels) {
        try {
            if (!graph) check(AVERROR(ENOMEM), "allocate_graph");
            graph->nb_threads = 1;
            AVChannelLayout layout{}; av_channel_layout_default(&layout, channels);
            char layout_text[128]{}; check(av_channel_layout_describe(&layout, layout_text, sizeof(layout_text)), "describe_layout");
            av_channel_layout_uninit(&layout);
            const auto args = "sample_rate=48000:time_base=1/48000:sample_fmt=flt:channel_layout=" + std::string(layout_text);
            const auto create = [&](const char* type, const char* name, const char* options, AVFilterContext** output) {
                const auto* filter = avfilter_get_by_name(type);
                if (!filter) check(AVERROR_FILTER_NOT_FOUND, type);
                check(avfilter_graph_create_filter(output, filter, name, options, nullptr, graph), type);
            };
            create("abuffer", "input", args.c_str(), &source);
            create("abuffersink", "output", nullptr, &sink);
            const auto* filter = avfilter_get_by_name("arnndn");
            if (!filter) check(AVERROR_FILTER_NOT_FOUND, "find_arnndn");
            auto* neural = avfilter_graph_alloc_filter(graph, filter, "speech");
            if (!neural) check(AVERROR(ENOMEM), "allocate_arnndn");
            const auto path = voiceIsolationModelPath().toUtf8();
            check(av_opt_set(neural->priv, "model", path.constData(), 0), "set_model");
            check(av_opt_set_double(neural->priv, "mix", 1, 0), "set_wet_mix");
            check(avfilter_init_str(neural, nullptr), "initialize_arnndn");
            AVFilterContext* format = nullptr; create("aformat", "format", "sample_fmts=flt", &format);
            check(avfilter_link(source, 0, neural, 0), "link_input");
            check(avfilter_link(neural, 0, format, 0), "link_neural");
            check(avfilter_link(format, 0, sink, 0), "link_output");
            check(avfilter_graph_config(graph, nullptr), "configure_graph");
        } catch (...) { avfilter_graph_free(&graph); throw; }
    }
    ~Graph() { avfilter_graph_free(&graph); }
};
struct FrameDelete { void operator()(AVFrame* frame) const { av_frame_free(&frame); } };
using Frame = std::unique_ptr<AVFrame, FrameDelete>;
class CachedPcm final : public VoiceIsolationPcm {
    QString path_;
    qint64 first_, count_;
    int channels_;
    mutable std::mutex mutex_;
    mutable SwrContext* resampler_ = nullptr;
    mutable int rate_ = 0, output_channels_ = 0;
    mutable qint64 next_ = -1, input_ = 0;
    mutable std::vector<float> pending_;
    std::vector<float> raw(qint64 start, std::size_t count) const {
        std::vector<float> output(count * channels_, 0);
        const auto begin = std::max(start, first_), end = std::min(start + static_cast<qint64>(count), first_ + count_);
        if (end <= begin) return output;
        QFile file(path_);
        if (!file.open(QIODevice::ReadOnly) || !file.seek(header_size + (begin - first_) * channels_ * 4))
            throw std::runtime_error("Could not read the voice isolation cache.");
        const auto bytes = file.read((end - begin) * channels_ * 4);
        if (bytes.size() != (end - begin) * channels_ * 4) throw std::runtime_error("Truncated voice isolation cache.");
        for (qint64 index = 0; index < (end - begin) * channels_; ++index) {
            const auto word = qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(bytes.constData()) + index * 4);
            const auto value = std::bit_cast<float>(word);
            if (!std::isfinite(value)) throw std::runtime_error("Non-finite voice isolation cache sample.");
            output[(begin - start) * channels_ + index] = value;
        }
        return output;
    }
public:
    CachedPcm(QString path, qint64 first, qint64 count, int channels)
        : path_(std::move(path)), first_(first), count_(count), channels_(channels) {}
    ~CachedPcm() override { swr_free(&resampler_); }
    std::vector<float> read(std::int64_t start, std::size_t count, int rate, int channels) const override {
        if (count > 1048576 || start < 0 || start > std::numeric_limits<std::int64_t>::max() - static_cast<std::int64_t>(count) || rate <= 0 || channels <= 0 || channels > 64)
            throw std::invalid_argument("Invalid voice isolation read interval.");
        std::lock_guard lock(mutex_);
        if (rate == 48000 && channels == channels_) return raw(start, count);
        if (next_ != start || rate_ != rate || output_channels_ != channels) {
            swr_free(&resampler_); pending_.clear();
            AVChannelLayout from{}, to{};
            av_channel_layout_default(&from, channels_); av_channel_layout_default(&to, channels);
            const auto code = swr_alloc_set_opts2(&resampler_, &to, AV_SAMPLE_FMT_FLT, rate, &from, AV_SAMPLE_FMT_FLT, 48000, 0, nullptr);
            av_channel_layout_uninit(&from); av_channel_layout_uninit(&to); check(code, "allocate_resampler");
            check(swr_init(resampler_), "initialize_resampler");
            rate_ = rate; output_channels_ = channels;
            // An integer-second anchor keeps the output sample grid stable on
            // seeks and supplies preceding context for the resampling kernel.
            const auto second = std::max<qint64>(0, start - 1024) / rate;
            input_ = second * 48000; next_ = second * rate;
        }
        const auto skip = start - next_;
        const auto needed = (skip + count) * channels;
        while (pending_.size() < needed) {
            auto samples = raw(input_, 2048); input_ += 2048;
            const auto capacity = swr_get_out_samples(resampler_, 2048);
            std::vector<float> converted(static_cast<std::size_t>(capacity) * channels);
            auto* out = reinterpret_cast<uint8_t*>(converted.data());
            const auto* in = reinterpret_cast<const uint8_t*>(samples.data());
            const auto produced = swr_convert(resampler_, &out, capacity, &in, 2048);
            check(produced, "resample_cache"); converted.resize(produced * channels);
            pending_.insert(pending_.end(), converted.begin(), converted.end());
        }
        std::vector<float> output(pending_.begin() + skip * channels, pending_.begin() + needed);
        pending_.erase(pending_.begin(), pending_.begin() + needed); next_ = start + count;
        return output;
    }
};
} // namespace

QString voiceIsolationCacheRoot() {
    const auto override_root = qEnvironmentVariable("CREATIVE_SUITE_VOICE_ISOLATION_CACHE_ROOT");
    const auto root = override_root.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/voice-isolation-v1" : override_root;
    if (!QDir().mkpath(root)) throw std::runtime_error("Could not create the voice isolation cache directory.");
    return root;
}
QString voiceIsolationModelPath() {
    static std::mutex mutex; std::lock_guard lock(mutex);
    static std::once_flag resource_once; std::call_once(resource_once, initializeVoiceIsolationResource);
    const auto path = QDir(voiceIsolationCacheRoot()).filePath("rnnoise-std-v1.rnnn");
    QFile existing(path);
    if (existing.open(QIODevice::ReadOnly) && existing.size() == 302903 && QCryptographicHash::hash(existing.readAll(), QCryptographicHash::Sha256).toHex() == model_hash) return path;
    if (existing.exists()) logging::Logger::instance().log(logging::Level::Error, "voice_isolation", "verify_materialized_model", "The cached model failed integrity validation; restoring the bundled model.", {{"model_id", "rnnoise-std-v1"}});
    QFile resource(":/audio/models/rnnoise-std-v1.rnnn");
    if (!resource.open(QIODevice::ReadOnly)) throw std::runtime_error("The bundled voice isolation model is missing.");
    const auto bytes = resource.readAll();
    if (QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() != model_hash) throw std::runtime_error("The bundled voice isolation model failed its integrity check.");
    existing.close(); QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        throw std::runtime_error("Could not materialize the voice isolation model.");
    return path;
}
std::shared_ptr<const VoiceIsolationPcm> findVoiceIsolation(const QString& key) {
    const auto path = cachePath(key);
    std::lock_guard lock(cache_mutex);
    if (auto held = leases[key.toStdString()].lock()) return held;
    QFile file(path); if (!file.exists()) return {};
    const auto reject = [&]() -> std::shared_ptr<const VoiceIsolationPcm> {
        logging::Logger::instance().log(logging::Level::Error, "voice_isolation", "validate_cache", "Invalid cached PCM artifact.", {{"key", key.toStdString()}});
        file.close(); QFile::remove(path); return {};
    };
    if (!file.open(QIODevice::ReadOnly)) return reject();
    const auto header = QJsonDocument::fromJson(file.read(header_size).trimmed()).object();
    bool valid_first = false, valid_count = false;
    const auto first = header.value("first").toString().toLongLong(&valid_first), count = header.value("count").toString().toLongLong(&valid_count);
    const int channels = header.value("channels").toInt();
    if (!valid_first || !valid_count || first > std::numeric_limits<qint64>::max() - count || header.value("channels").toDouble(-1) != channels || header.value("format") != "voice-pcm-v1" || header.value("key") != key || first < 0 || count <= 0 || channels < 1 || channels > 64 ||
        count > (std::numeric_limits<qint64>::max() - header_size) / (channels * 4) || file.size() != header_size + count * channels * 4) return reject();
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) { const auto block = file.read(65536); if (block.isEmpty() && file.error() != QFileDevice::NoError) return reject(); hash.addData(block); }
    if (hash.result().toHex() != header.value("sha256").toString().toLatin1()) return reject();
    auto artifact = std::make_shared<CachedPcm>(path, first, count, channels);
    leases[key.toStdString()] = artifact;
    file.setFileTime(QDateTime::currentDateTimeUtc(), QFileDevice::FileModificationTime);
    return artifact;
}
void forgetVoiceIsolation(const QString& key) {
    std::lock_guard lock(cache_mutex);
    // Existing leases remain valid until consumers release them. Regeneration
    // atomically replaces the same deterministic artifact, never a partial file.
    if (leases[key.toStdString()].expired()) { leases.erase(key.toStdString()); QFile::remove(cachePath(key)); }
}
std::shared_ptr<const VoiceIsolationPcm> generateVoiceIsolation(const QString& key,
    std::int64_t first, std::int64_t count, int channels, const IsolationSource& source,
    const IsolationCancelled& canceled, const IsolationProgress& progress) {
    if (first < 0 || count <= 0 || first > std::numeric_limits<std::int64_t>::max() - count - 12000 || channels < 1 || channels > 64 || count > (std::numeric_limits<qint64>::max() - header_size) / (channels * 4))
        throw std::invalid_argument("Invalid voice isolation generation interval.");
    checkCancel(canceled);
    // Preview preparation, export and ducking analysis share one inference
    // lane. Waiting stays cancellable and never occupies the UI thread.
    std::unique_lock<std::timed_mutex> inference(inference_mutex, std::defer_lock);
    while (!inference.try_lock_for(std::chrono::milliseconds(10))) checkCancel(canceled);
    checkCancel(canceled);
    Graph filter(channels);
    QSaveFile output(cachePath(key));
    if (!output.open(QIODevice::WriteOnly) || output.write(QByteArray(header_size, ' ')) != header_size)
        throw std::runtime_error("Could not allocate the voice isolation cache file.");
    const auto preceding = std::min<std::int64_t>(first, 12000);
    std::int64_t skip = preceding + 480, written = 0, cursor = 0;
    QCryptographicHash hash(QCryptographicHash::Sha256);
    Frame frame(av_frame_alloc()), received(av_frame_alloc());
    if (!frame || !received) check(AVERROR(ENOMEM), "allocate_frame");
    const auto total = preceding + count + 480;
    while (written < count) {
        checkCancel(canceled);
        constexpr int block = 480;
        std::vector<float> samples(block * channels, 0);
        const auto available = std::min<std::int64_t>(block, preceding + count - cursor);
        if (available > 0) {
            auto input = source(first - preceding + cursor, static_cast<std::size_t>(available));
            if (input.size() != static_cast<std::size_t>(available) * channels) throw std::runtime_error("Unaligned voice isolation source PCM.");
            std::copy(input.begin(), input.end(), samples.begin());
        }
        frame->format = AV_SAMPLE_FMT_FLT; frame->sample_rate = 48000; frame->nb_samples = block; frame->pts = cursor;
        av_channel_layout_default(&frame->ch_layout, channels);
        check(av_frame_get_buffer(frame.get(), 0), "allocate_samples");
        std::memcpy(frame->data[0], samples.data(), samples.size() * sizeof(float));
        check(av_buffersrc_add_frame_flags(filter.source, frame.get(), 0), "push_samples"); av_frame_unref(frame.get());
        cursor += block;
        while (true) {
            const auto code = av_buffersink_get_frame(filter.sink, received.get());
            if (code == AVERROR(EAGAIN) || code == AVERROR_EOF) break;
            check(code, "pull_samples");
            const auto discard = std::min<std::int64_t>(skip, received->nb_samples); skip -= discard;
            const auto keep = std::min<std::int64_t>(received->nb_samples - discard, count - written);
            const auto* pcm = reinterpret_cast<const float*>(received->data[0]) + discard * channels;
            QByteArray bytes(static_cast<qsizetype>(keep * channels * 4), Qt::Uninitialized);
            for (std::int64_t index = 0; index < keep * channels; ++index) {
                if (!std::isfinite(pcm[index])) throw std::runtime_error("Voice isolation generated non-finite PCM.");
                qToLittleEndian(std::bit_cast<quint32>(pcm[index]), reinterpret_cast<uchar*>(bytes.data()) + index * 4);
            }
            if (output.write(bytes) != bytes.size()) throw std::runtime_error("Could not write the voice isolation cache (disk full or I/O failure).");
            hash.addData(bytes); written += keep; av_frame_unref(received.get());
        }
        if (cursor > total + 4800) throw std::runtime_error("The voice isolation processor did not drain its output.");
        if (progress) progress(static_cast<int>(written * 100 / count));
    }
    check(av_buffersrc_add_frame_flags(filter.source, nullptr, 0), "drain_processor");
    QJsonObject header{{"format", "voice-pcm-v1"}, {"key", key}, {"first", QString::number(first)},
        {"count", QString::number(count)}, {"channels", channels}, {"sha256", QString::fromLatin1(hash.result().toHex())}};
    auto bytes = QJsonDocument(header).toJson(QJsonDocument::Compact); bytes.resize(header_size, ' ');
    checkCancel(canceled);
    if (!output.seek(0) || output.write(bytes) != header_size || !output.commit()) throw std::runtime_error("Could not publish the complete voice isolation cache.");
    auto result = findVoiceIsolation(key); pruneVoiceIsolationCache(); return result;
}
void pruneVoiceIsolationCache(std::uint64_t budget) {
    std::lock_guard lock(cache_mutex);
    const auto files = QDir(voiceIsolationCacheRoot()).entryInfoList({"*.vpcm"}, QDir::Files | QDir::NoSymLinks, QDir::Time | QDir::Reversed);
    std::uint64_t unused = 0;
    for (const auto& file : files) if (leases[file.completeBaseName().toStdString()].expired()) unused += file.size();
    for (const auto& file : files) {
        if (unused <= budget) break;
        if (leases[file.completeBaseName().toStdString()].expired() && QFile::remove(file.absoluteFilePath())) unused -= file.size();
    }
    std::erase_if(leases, [](const auto& item) { return item.second.expired(); });
}
} // namespace media

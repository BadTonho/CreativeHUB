#pragma once
#include "media/voice_isolation_render.h"
#include <QObject>
#include <QTimer>
#include <QThreadPool>
#include <QFileSystemWatcher>
#include <atomic>

namespace application {
// UI-thread owner. Only snapshot scheduling and notifications run here; file
// validation, decoding, inference and cache reads/writes run on its one worker.
class VoiceIsolationCoordinator final : public QObject {
    Q_OBJECT
public:
    explicit VoiceIsolationCoordinator(QObject* parent = nullptr);
    ~VoiceIsolationCoordinator() override;
    void request(project::ProjectDocument document, std::uint64_t generation);
    void regenerate(media::VoiceIsolationTarget target);
    void cancel();
signals:
    void contextChanged(std::shared_ptr<const media::VoiceIsolationContext> context);
    void stateChanged(bool clip, quint64 id, QString state, int progress);
private:
    void start();
    void allStates(const QString& state);
    QTimer debounce_;
    QThreadPool worker_;
    QFileSystemWatcher watcher_;
    project::ProjectDocument document_;
    QString recipe_;
    int channels_ = 2;
    std::uint64_t generation_ = 0, serial_ = 0;
    std::shared_ptr<std::atomic_bool> canceled_;
    std::set<media::VoiceIsolationTarget> force_;
};
} // namespace application

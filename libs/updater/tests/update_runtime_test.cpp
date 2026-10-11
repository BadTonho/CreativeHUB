#include <creative_suite/updater/update_service.h>
#include <creative_suite/diagnostics/logger.h>
#include "../../../cmake/test_support/test_check.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

using creative_suite::updater::UpdateRuntime;
using creative_suite::updater::UpdateState;

namespace {
struct Launch { QString app; QStringList arguments; QString directory; };

struct RuntimeHarness {
    QTemporaryDir directory;
    QList<Launch> launches;
    QSet<QString> fail;

    RuntimeHarness() {
        CS_TEST_CHECK(directory.isValid());
        const bool logged = creative_suite::diagnostics::Logger::instance().initialize(
            QFileInfo(directory.filePath("logs")).filesystemFilePath());
        CS_TEST_CHECK(logged);
    }
    UpdateRuntime::InstallerLauncher launcher() {
        return [this](const QString& program, const QStringList& arguments, const QString& workingDirectory) {
            const auto app = QFileInfo(program).completeBaseName();
            launches.push_back({app, arguments, workingDirectory});
            return !fail.contains(app);
        };
    }
    std::unique_ptr<UpdateRuntime> lease(const QString& app) {
        return std::make_unique<UpdateRuntime>(app, directory.path(), launcher());
    }
    QString pending(const QString& app) const { return directory.filePath("pending/" + app + ".json"); }
    bool stage(UpdateRuntime& runtime, const QString& app, bool installed = true) {
        const auto installer = directory.filePath(app + ".exe");
        const QByteArray bytes("synthetic installer fixture");
        QFile file(installer);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) return false;
        file.close();
        return runtime.installOrWait(app, directory.filePath("applications/" + app), app + ".exe",
            installer, QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex(), installed);
    }
};

bool tick(UpdateRuntime& runtime) {
    auto* timer = runtime.findChild<QTimer*>();
    return timer && timer->isActive() && QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection);
}

QByteArray readBytes(const QString& path) {
    QFile file(path);
    const bool opened = file.open(QIODevice::ReadOnly);
    CS_TEST_CHECK(opened);
    return file.readAll();
}
} // namespace

class UpdateRuntimeTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        qRegisterMetaType<UpdateState>();
    }
    void multipleOpenEditors_data() {
        QTest::addColumn<bool>("closeFirstScheduled");
        QTest::newRow("first scheduled closes first") << true;
        QTest::newRow("last scheduled closes first") << false;
    }
    void multipleOpenEditors() {
        QFETCH(bool, closeFirstScheduled);
        RuntimeHarness h;
        auto video = h.lease("video-editor");
        auto image = h.lease("image-editor");
        UpdateRuntime hub("hub", h.directory.path(), h.launcher());
        QSignalSpy states(&hub, &UpdateRuntime::installationStateChanged);
        const bool videoQueued = h.stage(hub, "video-editor");
        const bool imageQueued = h.stage(hub, "image-editor");
        const bool imageQueuedAgain = h.stage(hub, "image-editor");
        QVERIFY(videoQueued && imageQueued && imageQueuedAgain);
        QCOMPARE(h.launches.size(), 0);
        QCOMPARE(states.size(), 3);
        for (const auto& state : states) QCOMPARE(qvariant_cast<UpdateState>(state[1]), UpdateState::WaitingForApplicationToClose);
        const auto first = closeFirstScheduled ? QString("video-editor") : QString("image-editor");
        if (closeFirstScheduled) video.reset(); else image.reset();
        // Exercise actual periodic monitoring, without manually resuming.
        QTRY_COMPARE_WITH_TIMEOUT(h.launches.size(), 1, 4000);
        QCOMPARE(h.launches.front().app, first);
        QVERIFY(!QFile::exists(h.pending(first)));
        const bool polledAgain = tick(hub);
        QVERIFY(polledAgain);
        QCOMPARE(h.launches.size(), 1);
        if (closeFirstScheduled) image.reset(); else video.reset();
        const bool polledLast = tick(hub);
        QVERIFY(polledLast);
        QCOMPARE(h.launches.size(), 2);
        QCOMPARE(states.size(), 5);
        QCOMPARE(qvariant_cast<UpdateState>(states[3][1]), UpdateState::InstallerStarted);
        QCOMPARE(qvariant_cast<UpdateState>(states[4][1]), UpdateState::InstallerStarted);
        QVERIFY(!hub.findChild<QTimer*>()->isActive());
        QVERIFY(!QFile::exists(h.pending("video-editor")));
        QVERIFY(!QFile::exists(h.pending("image-editor")));
        for (const auto& launch : h.launches) {
            QCOMPARE(launch.directory, h.directory.path());
            QCOMPARE(launch.arguments, QStringList({"/SILENT", "/NORESTART",
                QString("/DIR=\"%1\"").arg(QDir::toNativeSeparators(h.directory.filePath("applications/" + launch.app)))}));
        }
    }
    void resumeEveryPending_data() {
        QTest::addColumn<bool>("enumerateFiles");
        QTest::addColumn<bool>("closeOthersBeforeResume");
        QTest::newRow("managed IDs, some closed") << false << true;
        QTest::newRow("all pending files, some closed") << true << true;
        QTest::newRow("managed IDs, all open") << false << false;
        QTest::newRow("all pending files, all open") << true << false;
    }
    void resumeEveryPending() {
        QFETCH(bool, enumerateFiles);
        QFETCH(bool, closeOthersBeforeResume);
        RuntimeHarness h;
        auto video = h.lease("video-editor");
        auto image = h.lease("image-editor");
        auto motion = h.lease("motion-editor");
        {
            UpdateRuntime previousHub("hub", h.directory.path(), h.launcher());
            const bool a = h.stage(previousHub, "video-editor");
            const bool b = h.stage(previousHub, "image-editor");
            const bool c = h.stage(previousHub, "motion-editor");
            const bool self = h.stage(previousHub, "hub");
            QVERIFY(a && b && c && self);
        }
        const auto persisted = QJsonDocument::fromJson(readBytes(h.pending("video-editor"))).object();
        QCOMPARE(persisted.value("schema_version").toInt(), 1);
        if (closeOthersBeforeResume) {
            image.reset();
            motion.reset();
        }
        UpdateRuntime hub("hub", h.directory.path(), h.launcher());
        QSignalSpy states(&hub, &UpdateRuntime::installationStateChanged);
        const QStringList managed{"video-editor", "image-editor", "motion-editor", "image-editor", "hub"};
        hub.resumePendingInstalls(enumerateFiles ? QStringList{} : managed);
        QCOMPARE(h.launches.size(), closeOthersBeforeResume ? 2 : 0);
        if (!closeOthersBeforeResume) {
            motion.reset();
            const bool motionTick = tick(hub);
            QVERIFY(motionTick);
            QCOMPARE(h.launches.size(), 1);
            image.reset();
            const bool imageTick = tick(hub);
            QVERIFY(imageTick);
            QCOMPARE(h.launches.size(), 2);
        }
        QSet<QString> launched;
        for (const auto& launch : h.launches) launched.insert(launch.app);
        QCOMPARE(launched, QSet<QString>({"image-editor", "motion-editor"}));
        QVERIFY(QFile::exists(h.pending("hub")));
        QVERIFY(QFile::exists(h.pending("video-editor")));
        hub.resumePendingInstalls(enumerateFiles ? QStringList{} : managed);
        QCOMPARE(h.launches.size(), 2);
        video.reset();
        const bool polled = tick(hub);
        QVERIFY(polled);
        QCOMPARE(h.launches.size(), 3);
        QCOMPARE(h.launches.back().app, QString("video-editor"));
        QVERIFY(!hub.findChild<QTimer*>()->isActive());
        // Self-update stays deferred until the owner's shutdown signal.
        const bool shuttingDown = QMetaObject::invokeMethod(QCoreApplication::instance(), "aboutToQuit", Qt::DirectConnection);
        QVERIFY(shuttingDown);
        QCOMPARE(h.launches.size(), 4);
        QCOMPARE(h.launches.back().app, QString("hub"));
        QVERIFY(!QFile::exists(h.pending("hub")));
    }
    void removedPendingDoesNotStopOthers() {
        RuntimeHarness h;
        auto video = h.lease("video-editor");
        auto image = h.lease("image-editor");
        UpdateRuntime hub("hub", h.directory.path(), h.launcher());
        const bool a = h.stage(hub, "video-editor");
        const bool b = h.stage(hub, "image-editor");
        QVERIFY(a && b);
        const bool removed = QFile::remove(h.pending("image-editor"));
        QVERIFY(removed);
        const bool firstTick = tick(hub);
        QVERIFY(firstTick);
        QCOMPARE(h.launches.size(), 0);
        video.reset();
        const bool secondTick = tick(hub);
        QVERIFY(secondTick);
        QCOMPARE(h.launches.size(), 1);
        QCOMPARE(h.launches.front().app, QString("video-editor"));
        QVERIFY(!hub.findChild<QTimer*>()->isActive());
    }
    void failedLaunchDoesNotStopOthers() {
        RuntimeHarness h;
        h.fail.insert("image-editor");
        auto video = h.lease("video-editor");
        auto image = h.lease("image-editor");
        UpdateRuntime hub("hub", h.directory.path(), h.launcher());
        QSignalSpy errors(&hub, &UpdateRuntime::installationFailed);
        const bool a = h.stage(hub, "video-editor");
        const bool b = h.stage(hub, "image-editor");
        QVERIFY(a && b);
        image.reset();
        const bool failedTick = tick(hub);
        QVERIFY(failedTick);
        QCOMPARE(errors.size(), 1);
        QCOMPARE(errors.front()[0].toString(), QString("image-editor"));
        QVERIFY(QFile::exists(h.pending("image-editor")));
        const auto logPath = QString::fromStdU16String(creative_suite::diagnostics::Logger::instance().log_path().u16string());
        const auto log = readBytes(logPath);
        QVERIFY(log.contains("launch_queued_installer") && log.contains("image-editor") && log.contains("level=Error"));
        video.reset();
        const bool successTick = tick(hub);
        QVERIFY(successTick);
        QCOMPARE(h.launches.size(), 2);
        QCOMPARE(h.launches.back().app, QString("video-editor"));
        QCOMPARE(errors.size(), 1);
        QVERIFY(!hub.findChild<QTimer*>()->isActive());
        h.fail.clear();
        hub.resumePendingInstalls({"image-editor"});
        QCOMPARE(h.launches.size(), 3);
        QVERIFY(!QFile::exists(h.pending("image-editor")));
    }
    void busyLaunchRemainsQueued() {
        RuntimeHarness h;
        auto image = h.lease("image-editor");
        QLockFile busy(h.directory.filePath("video-editor.installer-launch.lock"));
        const bool locked = busy.tryLock(0);
        QVERIFY(locked);
        UpdateRuntime hub("hub", h.directory.path(), h.launcher());
        QSignalSpy states(&hub, &UpdateRuntime::installationStateChanged);
        const bool a = h.stage(hub, "video-editor");
        const bool b = h.stage(hub, "image-editor");
        QVERIFY(a && b);
        QCOMPARE(h.launches.size(), 0);
        QCOMPARE(states.size(), 1); // Only the open Image Editor is waiting for closure.
        hub.resumePendingInstalls({"video-editor", "image-editor"});
        QCOMPARE(h.launches.size(), 0);
        QCOMPARE(states.size(), 1);
        image.reset();
        const bool imageTick = tick(hub);
        QVERIFY(imageTick);
        QCOMPARE(h.launches.size(), 1);
        QCOMPARE(h.launches.front().app, QString("image-editor"));
        QVERIFY(QFile::exists(h.pending("video-editor")));
        busy.unlock();
        const bool videoTick = tick(hub);
        QVERIFY(videoTick);
        QCOMPARE(h.launches.size(), 2);
        QCOMPARE(h.launches.back().app, QString("video-editor"));
        QCOMPARE(states.size(), 3);
        QVERIFY(!hub.findChild<QTimer*>()->isActive());
    }
    void tamperedPackageDoesNotStopOthers() {
        RuntimeHarness h;
        auto video = h.lease("video-editor");
        auto image = h.lease("image-editor");
        UpdateRuntime hub("hub", h.directory.path(), h.launcher());
        QSignalSpy errors(&hub, &UpdateRuntime::installationFailed);
        const bool a = h.stage(hub, "video-editor");
        const bool b = h.stage(hub, "image-editor");
        QVERIFY(a && b);
        QFile changed(h.directory.filePath("image-editor.exe"));
        const bool opened = changed.open(QIODevice::WriteOnly);
        QVERIFY(opened);
        const auto written = changed.write("changed bytes");
        QCOMPARE(written, qint64(13));
        changed.close();
        video.reset();
        image.reset();
        const bool polled = tick(hub);
        QVERIFY(polled);
        QCOMPARE(h.launches.size(), 1);
        QCOMPARE(h.launches.front().app, QString("video-editor"));
        QCOMPARE(errors.size(), 1);
        QCOMPARE(errors.front()[0].toString(), QString("image-editor"));
        QVERIFY(QFile::exists(h.pending("image-editor")));
        QVERIFY(!hub.findChild<QTimer*>()->isActive());
    }
    void newInstallationLaunchesWithoutSilentArguments() {
        RuntimeHarness h;
        UpdateRuntime hub("hub", h.directory.path(), h.launcher());
        QSignalSpy states(&hub, &UpdateRuntime::installationStateChanged);
        const bool accepted = h.stage(hub, "motion-editor", false);
        QVERIFY(accepted);
        QCOMPARE(h.launches.size(), 1);
        QVERIFY(h.launches.front().arguments.isEmpty());
        QCOMPARE(qvariant_cast<UpdateState>(states.front()[1]), UpdateState::InstallerStarted);
        QVERIFY(!QFile::exists(h.pending("motion-editor")));
    }
};

QTEST_GUILESS_MAIN(UpdateRuntimeTest)
#include "update_runtime_test.moc"

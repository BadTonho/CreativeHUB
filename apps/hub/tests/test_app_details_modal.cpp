#include "ui/dialogs/app_details_modal.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <cassert>
#include <iostream>

using namespace creative_suite::hub;

namespace {

void createTestChangelog(const QString& path, const QString& content) {
    QFile file(path);
    bool ok = file.open(QIODevice::WriteOnly | QIODevice::Text);
    assert(ok);
    file.write(content.toUtf8());
    file.close();
}

} // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    AppInfo info(
        QStringLiteral("video-editor"),
        QStringLiteral("Video Editor"),
        QStringLiteral("Edição audiovisual e pós-produção"),
        QStringLiteral("Editor completo de vídeo."),
        QStringLiteral(""),
        QStringLiteral("0.1.0"),
        QStringLiteral("creative-suite-video-editor.exe"),
        QStringLiteral(""),
        AppStatus::NotInstalled,
        QStringList{QStringLiteral("Linha do tempo"), QStringLiteral("Transições")},
        QStringLiteral(".csp")
    );

    QWidget parent;
    parent.resize(800, 600);
    parent.show();

    AppDetailsModal modal(&parent);
    assert(!modal.isVisible());

    // Show app modal originating from a card rect
    QRect originRect(100, 100, 236, 264);
    modal.showApp(info, originRect);

    assert(modal.isVisible());
    assert(modal.cardGeometry().isValid());
    assert(modal.emptyChangelogLabel() != nullptr);

    // Test changelog integration with a mock folder containing version notes
    QTemporaryDir mockChangelogDir;
    assert(mockChangelogDir.isValid());
    QDir().mkpath(mockChangelogDir.path() + QStringLiteral("/video-editor"));
    createTestChangelog(
        mockChangelogDir.path() + QStringLiteral("/video-editor/0.1.0.md"),
        QStringLiteral("# Notas 0.1.0\n- Novas funcionalidades implementadas.")
    );
    createTestChangelog(
        mockChangelogDir.path() + QStringLiteral("/video-editor/0.2.0.md"),
        QStringLiteral("# Notas 0.2.0\n- Melhorias de estabilidade.")
    );

    modal.setChangelogBasePath(mockChangelogDir.path());

    assert(modal.changelogBrowser() != nullptr);
    assert(modal.changelogBrowser()->isVisible());
    assert(modal.versionCombo() != nullptr);
    assert(modal.versionCombo()->isVisible());
    assert(modal.versionCombo()->count() == 2);
    assert(modal.changelogBrowser()->toPlainText().contains(QStringLiteral("Novas funcionalidades")));

    bool closedSignalReceived = false;
    QObject::connect(&modal, &AppDetailsModal::closed, [&closedSignalReceived]() {
        closedSignalReceived = true;
    });

    // Close with animation
    modal.closeWithAnimation();

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 500) {
        app.processEvents(QEventLoop::AllEvents, 50);
    }

    assert(closedSignalReceived);
    assert(!modal.isVisible());

    std::cout << "All AppDetailsModal tests passed successfully!" << std::endl;
    return 0;
}

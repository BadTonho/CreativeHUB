#include "ui/dialogs/app_details_modal.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include "../../../cmake/test_support/test_check.h"
#include <iostream>

using namespace creative_suite::hub;

namespace {

void createTestChangelog(const QString& path, const QString& content) {
    QFile file(path);
    bool ok = file.open(QIODevice::WriteOnly | QIODevice::Text);
    CS_TEST_CHECK(ok);
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
    CS_TEST_CHECK(!modal.isVisible());

    // Show app modal originating from a card rect
    QRect originRect(100, 100, 236, 264);
    modal.showApp(info, originRect);

    CS_TEST_CHECK(modal.isVisible());
    CS_TEST_CHECK(modal.cardGeometry().isValid());
    CS_TEST_CHECK(modal.emptyChangelogLabel() != nullptr);

    // Test changelog integration with a mock folder containing version notes
    QTemporaryDir mockChangelogDir;
    CS_TEST_CHECK(mockChangelogDir.isValid());
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

    CS_TEST_CHECK(modal.changelogBrowser() != nullptr);
    CS_TEST_CHECK(modal.changelogBrowser()->isVisible());
    CS_TEST_CHECK(modal.versionCombo() != nullptr);
    CS_TEST_CHECK(modal.versionCombo()->isVisible());
    CS_TEST_CHECK(modal.versionCombo()->count() == 2);
    CS_TEST_CHECK(modal.changelogBrowser()->toPlainText().contains(QStringLiteral("Novas funcionalidades")));

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

    CS_TEST_CHECK(closedSignalReceived);
    CS_TEST_CHECK(!modal.isVisible());

    std::cout << "All AppDetailsModal tests passed successfully!" << std::endl;
    return 0;
}

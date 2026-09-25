#include "image_editor_window.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QMessageBox>

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Creative Suite"));
    QCoreApplication::setApplicationName(QStringLiteral("Image Editor"));
    QApplication::setApplicationVersion(QStringLiteral("Beta 0.1.0"));

    QCommandLineParser parser;
    parser.addHelpOption();
    const QCommandLineOption linked_source(
        QStringLiteral("linked-source"),
        QStringLiteral("Source image for a new linked document."),
        QStringLiteral("path"));
    const QCommandLineOption linked_document(
        QStringLiteral("linked-document"),
        QStringLiteral("Editable linked .cimg document path."),
        QStringLiteral("path"));
    const QCommandLineOption publish_output(
        QStringLiteral("publish-output"),
        QStringLiteral("PNG path published after saving the linked document."),
        QStringLiteral("path"));
    parser.addOption(linked_source);
    parser.addOption(linked_document);
    parser.addOption(publish_output);
    parser.process(application);

    const bool linked_mode = parser.isSet(linked_source) ||
        parser.isSet(linked_document) || parser.isSet(publish_output);
    if (linked_mode && (!parser.isSet(linked_source) ||
                        !parser.isSet(linked_document) ||
                        !parser.isSet(publish_output))) {
        const QString cause = QStringLiteral(
            "Linked mode requires --linked-source, --linked-document, and --publish-output.");
        image_editor::ImageEditorLogger{}.logError(
            QStringLiteral("parse_linked_arguments"), cause,
            parser.value(linked_document));
        QMessageBox::critical(
            nullptr,
            QStringLiteral("Image Editor launch error"),
            cause);
        return 2;
    }

    image_editor::ImageEditorWindow window;
    if (linked_mode && !window.openLinkedImage(
            parser.value(linked_source),
            parser.value(linked_document),
            parser.value(publish_output))) {
        return 2;
    }
    window.show();
    return application.exec();
}

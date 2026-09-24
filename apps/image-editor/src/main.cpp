#include "image_editor_window.h"

#include <QApplication>

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Creative Suite"));
    QCoreApplication::setApplicationName(QStringLiteral("Image Editor"));

    image_editor::ImageEditorWindow window;
    window.show();
    return application.exec();
}

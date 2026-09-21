#include "ui/media_browser_bin_tree_widget.h"

#include "ui/media_drag_mime.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QTreeWidgetItem>

#include <cstdio>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

} // namespace

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName("CreativeSuiteTests");
    QCoreApplication::setApplicationName("MediaBrowserBinTreeWidgetTest");

    try {
        MediaBrowserBinTreeWidget tree;
        tree.resize(320, 180);
        auto* all_media = new QTreeWidgetItem(&tree, {"All Media"});
        all_media->setData(0, Qt::UserRole, QString());
        auto* footage = new QTreeWidgetItem(all_media, {"Footage"});
        footage->setData(0, Qt::UserRole, QStringLiteral("Footage"));
        auto* archive = new QTreeWidgetItem(all_media, {"Archive"});
        archive->setData(0, Qt::UserRole, QStringLiteral("Archive"));
        tree.expandAll();
        tree.show();
        application.processEvents();

        bool media_drop_received = false;
        QString received_media_path;
        QString received_media_destination;
        QObject::connect(
            &tree,
            &MediaBrowserBinTreeWidget::mediaDropRequested,
            [&media_drop_received, &received_media_path, &received_media_destination](
                const QString& source_path,
                const QString& destination_bin) {
                media_drop_received = true;
                received_media_path = source_path;
                received_media_destination = destination_bin;
            });

        const auto footage_position = tree.visualItemRect(footage).center();
        require(tree.itemAt(footage_position) == footage,
                "The footage test position did not resolve to the bin.");
        QMimeData media_mime;
        media_mime.setData(
            ui::kMediaPathMimeType,
            QByteArrayLiteral("C:/Media/clip.mp4"));
        QDragEnterEvent media_enter(
            footage_position,
            Qt::MoveAction,
            &media_mime,
            Qt::NoButton,
            Qt::NoModifier);
        QApplication::sendEvent(tree.viewport(), &media_enter);
        QDragMoveEvent media_move(
            footage_position,
            Qt::MoveAction,
            &media_mime,
            Qt::NoButton,
            Qt::NoModifier);
        QApplication::sendEvent(tree.viewport(), &media_move);
        QDropEvent media_drop(
            QPointF(footage_position),
            Qt::MoveAction,
            &media_mime,
            Qt::NoButton,
            Qt::NoModifier);
        QApplication::sendEvent(tree.viewport(), &media_drop);
        require(media_drop_received, "A media drop was not emitted.");
        require(received_media_path == "C:/Media/clip.mp4",
                "The media source path was not preserved in the drop.");
        require(received_media_destination == "Footage",
                "The media drop destination was incorrect.");

        bool bin_drop_received = false;
        QString received_source_bin;
        QString received_destination_bin;
        QObject::connect(
            &tree,
            &MediaBrowserBinTreeWidget::binDropRequested,
            [&bin_drop_received, &received_source_bin, &received_destination_bin](
                const QString& source_bin,
                const QString& destination_bin) {
                bin_drop_received = true;
                received_source_bin = source_bin;
                received_destination_bin = destination_bin;
            });

        const auto archive_position = tree.visualItemRect(archive).center();
        require(tree.itemAt(archive_position) == archive,
                "The archive test position did not resolve to the bin.");
        QMimeData bin_mime;
        bin_mime.setData(
            ui::kMediaBinPathMimeType,
            QByteArrayLiteral("Footage"));
        QDragEnterEvent bin_enter(
            archive_position,
            Qt::MoveAction,
            &bin_mime,
            Qt::NoButton,
            Qt::NoModifier);
        QApplication::sendEvent(tree.viewport(), &bin_enter);
        QDragMoveEvent bin_move(
            archive_position,
            Qt::MoveAction,
            &bin_mime,
            Qt::NoButton,
            Qt::NoModifier);
        QApplication::sendEvent(tree.viewport(), &bin_move);
        QDropEvent bin_drop(
            QPointF(archive_position),
            Qt::MoveAction,
            &bin_mime,
            Qt::NoButton,
            Qt::NoModifier);
        QApplication::sendEvent(tree.viewport(), &bin_drop);
        require(bin_drop_received, "A bin drop was not emitted.");
        require(received_source_bin == "Footage" &&
                    received_destination_bin == "Archive",
                "The bin drop paths were incorrect.");

        bool invalid_drop_received = false;
        QObject::connect(
            &tree,
            &MediaBrowserBinTreeWidget::binDropRequested,
            [&invalid_drop_received](const QString&, const QString&) {
                invalid_drop_received = true;
            });
        const auto all_media_position = tree.visualItemRect(all_media).center();
        QDropEvent invalid_drop(
            QPointF(all_media_position),
            Qt::MoveAction,
            &bin_mime,
            Qt::NoButton,
            Qt::NoModifier);
        QApplication::sendEvent(tree.viewport(), &invalid_drop);
        require(!invalid_drop_received,
                "A drop on All Media was incorrectly accepted.");
        require(!invalid_drop.isAccepted(),
                "An invalid drop was not rejected.");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}

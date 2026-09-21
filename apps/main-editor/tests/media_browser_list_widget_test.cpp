#include "ui/media_browser_list_widget.h"

#include <QApplication>
#include <QCoreApplication>
#include <QIcon>
#include <QListWidgetItem>
#include <QPixmap>
#include <QSettings>
#include <QStyle>

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
    QCoreApplication::setApplicationName("MediaBrowserListWidgetTest");
    QSettings::setDefaultFormat(QSettings::IniFormat);

    QSettings settings;
    settings.clear();

    try {
        MediaBrowserListWidget widget;
        require(
            widget.displayMode() == MediaBrowserListWidget::DisplayMode::List,
            "Media Browser must default to list mode.");
        require(widget.viewMode() == QListView::ListMode,
                "List mode was not applied to the QListWidget.");
        require(
            widget.editTriggers().testFlag(QAbstractItemView::DoubleClicked),
            "Media items must support double-click editing.");
        require(
            widget.editTriggers().testFlag(QAbstractItemView::EditKeyPressed),
            "Media items must support F2 editing.");

        auto* item = new QListWidgetItem("Sample clip", &widget);
        item->setData(Qt::UserRole, QStringLiteral("sample.mp4"));
        item->setData(
            media_browser_ui::kMediaIndexRole,
            static_cast<qint64>(7));
        item->setData(
            media_browser_ui::kMediaInfoRole,
            QStringLiteral("Name: Sample clip\nFormat: MP4"));
        item->setFlags(item->flags() | Qt::ItemIsEditable);
        QPixmap thumbnail(16, 16);
        thumbnail.fill(Qt::blue);
        item->setIcon(QIcon(thumbnail));

        widget.setDisplayMode(MediaBrowserListWidget::DisplayMode::Grid);
        require(
            widget.displayMode() == MediaBrowserListWidget::DisplayMode::Grid,
            "Media Browser did not switch to grid mode.");
        require(widget.viewMode() == QListView::IconMode,
                "Grid mode was not applied to the QListWidget.");
        require(!item->icon().isNull(),
                "The Media Browser item did not retain its thumbnail.");
        require(settings.value("media_browser/view_mode").toString() == "grid",
                "Grid mode was not persisted.");
        require(item->data(Qt::UserRole).toString() == "sample.mp4",
                "Media source path data was not preserved.");
        require(item->data(media_browser_ui::kMediaIndexRole).toLongLong() == 7,
                "Media index data was not preserved.");
        require(item->data(media_browser_ui::kMediaInfoRole).toString().contains(
                    "Format: MP4"),
                "Media information data was not preserved.");
        require(item->flags() & Qt::ItemIsEditable,
                "Media items must be editable.");

        auto* bin_item = new QListWidgetItem("Footage", &widget);
        bin_item->setIcon(QApplication::style()->standardIcon(QStyle::SP_DirIcon));
        bin_item->setData(
            media_browser_ui::kMediaItemTypeRole,
            media_browser_ui::kMediaItemTypeBin);
        bin_item->setData(
            media_browser_ui::kMediaBinPathRole,
            QStringLiteral("Projects/Footage"));
        bin_item->setFlags(
            (bin_item->flags() | Qt::ItemIsEditable) & ~Qt::ItemIsDragEnabled);
        require(!bin_item->icon().isNull(),
                "The Media Browser bin did not receive a folder icon.");
        require(bin_item->data(media_browser_ui::kMediaItemTypeRole).toInt() ==
                    media_browser_ui::kMediaItemTypeBin,
                "The Media Browser bin type was not preserved.");
        require(bin_item->data(media_browser_ui::kMediaBinPathRole).toString() ==
                    "Projects/Footage",
                "The Media Browser bin path was not preserved.");
        require(!(bin_item->flags() & Qt::ItemIsDragEnabled),
                "Bins must not use the media-to-Timeline drag operation.");
        require(bin_item->flags() & Qt::ItemIsEditable,
                "Bin items must be editable.");

        widget.setDisplayMode(MediaBrowserListWidget::DisplayMode::List);
        require(widget.viewMode() == QListView::ListMode,
                "Media Browser did not return to list mode.");

        MediaBrowserListWidget restored_widget;
        require(
            restored_widget.displayMode() ==
                MediaBrowserListWidget::DisplayMode::List,
            "List mode was not persisted after switching back.");

        settings.clear();
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        settings.clear();
        return 1;
    }
}

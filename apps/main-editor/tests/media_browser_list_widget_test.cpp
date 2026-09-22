#include "ui/media_browser_list_widget.h"
#include "main_window/main_window_support.h"

#include <QApplication>
#include <QCoreApplication>
#include <QIcon>
#include <QListWidgetItem>
#include <QLineEdit>
#include <QPixmap>
#include <QSettings>
#include <QStyleOptionViewItem>
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
        media::VideoMetadata metadata;
        metadata.display_name = "OriginalMediaName.mp4";
        metadata.width = 1920;
        metadata.height = 1080;
        metadata.frame_rate = 24.0;
        metadata.duration_seconds = 56.75;
        require(
            main_window_detail::compactMediaBrowserName("Short") == "Short",
            "Short Media Browser names must remain unchanged.");
        require(
            main_window_detail::compactMediaBrowserName("OriginalMediaName") ==
                "Origina...",
            "Long Media Browser names were not compacted to seven characters.");
        require(
            main_window_detail::compactMediaItemListText(
                metadata, "OriginalMediaName.mp4", false) == "Origina...",
            "Media metadata must not be shown in the compact item label.");
        require(
            main_window_detail::compactMediaItemListText(
                metadata, "OriginalMediaName.mp4", true) ==
                "Origina... [Offline]",
            "Offline status must remain visible in the compact item label.");

        MediaBrowserListWidget widget;
        require(
            widget.displayMode() == MediaBrowserListWidget::DisplayMode::List,
            "Media Browser must default to list mode.");
        require(widget.viewMode() == QListView::ListMode,
                "List mode was not applied to the QListWidget.");
        require(widget.iconScalePercent() ==
                    MediaBrowserListWidget::kDefaultIconScalePercent,
                "Media Browser must default to 100 percent icon scale.");
        require(widget.iconSize() == QSize(48, 32),
                "Default list icon size changed unexpectedly.");
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
        item->setData(
            media_browser_ui::kMediaFullDisplayNameRole,
            QStringLiteral("Sample clip with a longer original name"));
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
        require(widget.iconSize() == QSize(128, 72),
                "Default grid icon size changed unexpectedly.");
        require(widget.gridSize() == QSize(168, 126),
                "Default grid cell size changed unexpectedly.");
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

        QStyleOptionViewItem editor_options;
        auto* editor = widget.itemDelegate()->createEditor(
            &widget,
            editor_options,
            widget.indexFromItem(item));
        require(editor != nullptr,
                "Media items must provide an inline editor.");
        widget.itemDelegate()->setEditorData(
            editor,
            widget.indexFromItem(item));
        const auto* line_edit = qobject_cast<QLineEdit*>(editor);
        require(line_edit != nullptr &&
                    line_edit->text() ==
                        "Sample clip with a longer original name",
                "Inline editing must use the full original Media name.");
        delete editor;

        widget.setIconScalePercent(150);
        require(widget.iconScalePercent() == 150,
                "Media Browser did not apply the maximum icon scale.");
        require(widget.iconSize() == QSize(192, 108),
                "Maximum grid icon size is incorrect.");
        require(widget.gridSize() == QSize(232, 162),
                "Maximum grid cell size is incorrect.");
        require(settings.value("media_browser/icon_scale_percent").toInt() ==
                    150,
                "Icon scale was not persisted.");

        widget.setDisplayMode(MediaBrowserListWidget::DisplayMode::List);
        require(widget.iconSize() == QSize(72, 48),
                "Maximum list icon size is incorrect.");
        widget.setIconScalePercent(0);
        require(widget.iconScalePercent() ==
                    MediaBrowserListWidget::kMinimumIconScalePercent,
                "Icon scale was not clamped to its minimum.");
        require(widget.iconSize() == QSize(24, 16),
                "Minimum list icon size is incorrect.");
        widget.setIconScalePercent(999);
        require(widget.iconScalePercent() ==
                    MediaBrowserListWidget::kMaximumIconScalePercent,
                "Icon scale was not clamped to its maximum.");

        auto* bin_item = new QListWidgetItem("Footage", &widget);
        bin_item->setIcon(QApplication::style()->standardIcon(QStyle::SP_DirIcon));
        bin_item->setData(
            media_browser_ui::kMediaFullDisplayNameRole,
            QStringLiteral("Footage"));
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

        MediaBrowserListWidget restored_widget;
        require(
            restored_widget.displayMode() ==
                MediaBrowserListWidget::DisplayMode::List,
            "List mode was not preserved after switching back.");
        require(restored_widget.iconScalePercent() == 150,
                "Icon scale was not restored from the global preference.");
        require(restored_widget.iconSize() == QSize(72, 48),
                "Restored list icon size is incorrect.");

        widget.setDisplayMode(MediaBrowserListWidget::DisplayMode::List);
        require(widget.viewMode() == QListView::ListMode,
                "Media Browser did not return to list mode.");

        settings.clear();
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        settings.clear();
        return 1;
    }
}

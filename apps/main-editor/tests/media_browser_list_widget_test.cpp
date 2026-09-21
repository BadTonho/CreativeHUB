#include "ui/media_browser_list_widget.h"

#include <QApplication>
#include <QCoreApplication>
#include <QIcon>
#include <QListWidgetItem>
#include <QPixmap>
#include <QSettings>

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

        auto* item = new QListWidgetItem("Sample clip", &widget);
        item->setData(Qt::UserRole, QStringLiteral("sample.mp4"));
        item->setData(
            media_browser_ui::kMediaIndexRole,
            static_cast<qint64>(7));
        item->setData(
            media_browser_ui::kMediaInfoRole,
            QStringLiteral("Name: Sample clip\nFormat: MP4"));
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

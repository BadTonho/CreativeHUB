#include "ui/effects/effects_catalog.h"
#include "ui/effects/effects_favorites_widget.h"
#include "ui/effects/effects_list_widget.h"
#include "ui/media_browser/media_drag_mime.h"
#include "ui/effects/effects_toolbox_widget.h"

#include <QApplication>
#include <QMimeData>
#include <QMouseEvent>
#include <QSettings>
#include <QTemporaryDir>

#include <cstdio>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

}  // namespace

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    QTemporaryDir settings_dir;
    if (!settings_dir.isValid()) return 1;
    QCoreApplication::setOrganizationName("CreativeSuiteTests");
    QCoreApplication::setApplicationName("effects-panel");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings_dir.path());

    try {
        EffectsToolboxWidget toolbox;
        EffectsFavoritesWidget favorites;
        EffectsListWidget effects_list;

        require(favorites.count() == 0,
                "Effects Favorites must initially be empty.");
        require(toolbox.count() == 6,
                "Effects Toolbox must contain the implemented categories and Generators.");
        require(toolbox.item(0)->text() == "All",
                "All category must be first.");
        require(toolbox.item(1)->text() == "Video",
                "Video category is missing.");
        require(toolbox.item(3)->text() == "Audio",
                "Audio category is missing.");
        require(toolbox.item(4)->text() == "Transitions",
                "Transitions category is missing.");
        require(toolbox.item(5)->text() == "Text",
                "Text category is missing.");
        require(toolbox.currentCategoryId() == "all",
                "Effects Toolbox must initially select All.");
        require(effects_list.count() == 18,
                "Effects list must contain all visual filters and existing tools.");
        require(effects_list.categoryId() == "all",
                "Effects list must initially show All.");
        require(effects_list.visibleEffectCount() == 11,
                "All category must show every effect.");
        require(effects_list.item(0)->data(Qt::UserRole).toString() ==
                    "video.grayscale",
                "Effect IDs must be stable.");

        const auto text_items = effects_list.findItems(
            "Text", Qt::MatchExactly);
        require(text_items.size() == 1,
                "Text effect must be available for dragging.");
        require(effects_list.dragEnabled(),
                "Effects drag support is not enabled.");
        for (int index = 0; index < effects_list.count(); ++index) {
            const auto effect_id = effects_list.item(index)
                ->data(Qt::UserRole).toString();
            const bool expected_draggable =
                (effect_id.startsWith(QStringLiteral("video.")) && effect_id != QStringLiteral("video.gaussian_blur")) ||
                effect_id == QStringLiteral("text.text") ||
                effect_id == QStringLiteral("transitions.cross_dissolve") ||
                effect_id == QStringLiteral("transitions.fade_to_black");
            require(
                effects_list.item(index)->flags().testFlag(Qt::ItemIsDragEnabled) ==
                    expected_draggable,
                "Visual filters, Text, and transitions must be draggable.");
        }
        const auto verifyDragMime = [&effects_list](
            const QString& name,
            const QByteArray& expected_id) {
            const auto items = effects_list.findItems(name, Qt::MatchExactly);
            require(items.size() == 1,
                    "A draggable effect is missing from the Effects list.");
            auto* mime = effects_list.mimeData(items);
            require(mime != nullptr &&
                        mime->hasFormat(ui::kEffectIdMimeType) &&
                        mime->data(ui::kEffectIdMimeType) == expected_id,
                    "A draggable effect has invalid MIME data.");
            delete mime;
        };
        verifyDragMime("Background", QByteArrayLiteral("video.background"));
        verifyDragMime("Fusion Composition", QByteArrayLiteral("video.fusion_composition"));
        verifyDragMime("Adjustment Clip", QByteArrayLiteral("video.adjustment_clip"));
        verifyDragMime("Text", QByteArrayLiteral("text.text"));
        verifyDragMime("Grayscale", QByteArrayLiteral("video.grayscale"));
        verifyDragMime("Brightness", QByteArrayLiteral("video.brightness"));
        verifyDragMime("Contrast", QByteArrayLiteral("video.contrast"));
        verifyDragMime("Saturation", QByteArrayLiteral("video.saturation"));
        verifyDragMime(
            "Cross Dissolve",
            QByteArrayLiteral("transitions.cross_dissolve"));
        verifyDragMime(
            "Fade to Black",
            QByteArrayLiteral("transitions.fade_to_black"));

        bool changed = false;
        QString changed_category;
        QObject::connect(
            &toolbox,
            &EffectsToolboxWidget::categoryChanged,
            [&changed, &changed_category](const QString& category_id) {
                changed = true;
                changed_category = category_id;
            });

        toolbox.setCurrentRow(1);
        require(changed && changed_category == "video",
                "Selecting Video must emit its category ID.");
        effects_list.setCategory(toolbox.currentCategoryId());
        require(effects_list.categoryId() == "video",
                "Effects list category did not change to Video.");
        require(effects_list.visibleEffectCount() == 7,
                "Video category must show the four visual filters.");

        require(toolbox.item(2)->text().trimmed()=="Generators","Generators subcategory is missing.");
        effects_list.setCategory("video.generators");
        require(effects_list.visibleEffectCount()==1,"Generators must show Background only.");
        require(effects::setFavorite("video.background",true),"Background could not be favorited.");
        favorites.refresh();require(favorites.count()==1,"Favorites did not show Background.");
        auto* favorite_mime=favorites.mimeData({favorites.item(0)});
        require(favorite_mime && favorite_mime->data(ui::kEffectIdMimeType)=="video.background","Favorite Background is not draggable.");
        delete favorite_mime;
        EffectsFavoritesWidget reopened;require(reopened.count()==1,"Favorite did not persist.");
        require(effects::setFavorite("video.background",false),"Background could not be unfavorited.");
        favorites.refresh();require(favorites.count()==0,"Removed favorite remained visible.");
        effects_list.setCategory("audio");
        require(effects_list.visibleEffectCount() == 1,
                "Audio category must show one effect.");
        effects_list.setCategory("transitions");
        require(effects_list.visibleEffectCount() == 2,
                "Transitions category must show two effects.");
        effects_list.setCategory("text");
        require(effects_list.visibleEffectCount() == 1,
                "Text category must show one effect.");
        effects_list.setCategory("unknown");
        require(effects_list.categoryId() == "all" &&
                    effects_list.visibleEffectCount() == 11,
                "Unknown categories must fall back to All.");

        require(effects::categories().size() == 8,
                "Shared effect catalog contains unimplemented categories.");
        require(effects::definitions().size() == 18,
                "The Effects catalog must retain existing tools alongside visual filters.");
        QObject::connect(&toolbox, &EffectsToolboxWidget::categoryChanged,
                         &effects_list, &EffectsListWidget::setCategory);
        require(effects::setFavorite("fusion.ellipse_mask", true) &&
                effects::setFavorite("video.adjustment_clip", true) &&
                effects::setFavorite("video.background", true), "Favorites setup failed.");
        favorites.refresh();
        require(favorites.count() == 2, "Edit must hide Fusion-only favorites.");
        toolbox.setCurrentRow(3); // Audio disappears on entering Fusion.
        effects_list.setWorkspace(effects::Workspace::Fusion);
        favorites.setWorkspace(effects::Workspace::Fusion);
        toolbox.setWorkspace(effects::Workspace::Fusion);
        require(toolbox.count() == 6 && toolbox.currentCategoryId() == "all" &&
                effects_list.categoryId() == "all" && effects_list.visibleEffectCount() == 13,
                "Fusion categories and All fallback are incorrect.");
        require(favorites.count() == 2 && effects::favoriteIds().size() == 3,
                "Workspace filtering must retain hidden saved favorites.");
        for (const auto& id : {"video.adjustment_clip", "video.fusion_composition", "audio.gain",
                               "transitions.cross_dissolve", "transitions.fade_to_black"}) {
            for (int i = 0; i < effects_list.count(); ++i) if (effects_list.item(i)->data(Qt::UserRole) == id) {
                require(effects_list.item(i)->isHidden() &&
                        effects_list.mimeData({effects_list.item(i)}) == nullptr,
                        "Fusion must reject Edit-only MIME even for a stale item.");
            }
        }
        effects_list.setCategory("video");
        require(effects_list.visibleEffectCount()==6,"Fusion Video must include Gaussian Blur.");
        verifyDragMime("Gaussian Blur","video.gaussian_blur");
        effects_list.setCategory("fusion.masks");
        require(effects_list.visibleEffectCount() == 2, "Masks category must contain both shapes.");
        verifyDragMime("Ellipse Mask", "fusion.ellipse_mask");
        effects_list.setCategory("fusion");
        require(effects_list.visibleEffectCount() == 6, "Fusion must include its Masks descendants.");
        int activations = 0;
        QObject::connect(&effects_list, &EffectsListWidget::creationRequested,
                         [&](const QString& id) { if (id == "fusion.ellipse_mask") ++activations; });
        auto* ellipse = effects_list.findItems("Ellipse Mask", Qt::MatchExactly).front();
        effects_list.resize(260, 400);
        effects_list.show();
        application.processEvents();
        effects_list.scrollToItem(ellipse);
        const auto point = effects_list.visualItemRect(ellipse).center();
        const auto global = effects_list.viewport()->mapToGlobal(point);
        for (const auto type : {QEvent::MouseButtonPress, QEvent::MouseButtonRelease,
                                QEvent::MouseButtonDblClick, QEvent::MouseButtonRelease}) {
            QMouseEvent event(type, QPointF(point), QPointF(global), Qt::LeftButton,
                type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(effects_list.viewport(), &event);
        }
        require(activations == 1, "Double click must request Fusion node creation.");
        effects_list.setEnabled(false);
        effects_list.itemDoubleClicked(ellipse);
        require(activations == 1 && effects_list.mimeData({ellipse}) == nullptr,
                "Unavailable creation must reject activation and dragging.");
        effects_list.setEnabled(true);
        int favorite_activations = 0;
        QObject::connect(&favorites, &EffectsFavoritesWidget::creationRequested,
                         [&](const QString& id) { if (id == "fusion.ellipse_mask") ++favorite_activations; });
        favorites.itemDoubleClicked(favorites.findItems("Ellipse Mask", Qt::MatchExactly).front());
        require(favorite_activations == 1, "Fusion favorites must support double click.");
        effects_list.setWorkspace(effects::Workspace::Edit);
        favorites.setWorkspace(effects::Workspace::Edit);
        toolbox.setCurrentRow(toolbox.count() - 1); // Masks disappears on returning to Edit.
        toolbox.setWorkspace(effects::Workspace::Edit);
        require(toolbox.currentCategoryId() == "all" && effects_list.visibleEffectCount() == 11 &&
                favorites.count() == 2 && effects::favoriteIds().size() == 3,
                "Returning to Edit must restore its catalog without losing favorites.");
        effects_list.itemDoubleClicked(ellipse);
        require(activations == 1 && effects_list.mimeData({ellipse}) == nullptr,
                "Fusion-only items must not activate or drag in Edit.");
        effects_list.setWorkspace(effects::Workspace::None);
        favorites.setWorkspace(effects::Workspace::None);
        toolbox.setWorkspace(effects::Workspace::None);
        require(toolbox.count() == 0 && favorites.count() == 0 && effects_list.visibleEffectCount() == 0,
                "Render must not offer editing actions.");
        require(effects::setFavorite("video.gaussian_blur",true),"Blur favorite must persist.");
        favorites.setWorkspace(effects::Workspace::Fusion);require(favorites.count()==3,"Fusion Blur favorite visible.");
        favorites.setWorkspace(effects::Workspace::Edit);require(favorites.count()==2 && effects::favoriteIds().contains("video.gaussian_blur"),"Edit hides but retains Blur favorite.");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}

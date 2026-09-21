#include "ui/effects_catalog.h"
#include "ui/effects_favorites_widget.h"
#include "ui/effects_list_widget.h"
#include "ui/effects_toolbox_widget.h"

#include <QApplication>

#include <cstdio>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

}  // namespace

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);

    try {
        EffectsToolboxWidget toolbox;
        EffectsFavoritesWidget favorites;
        EffectsListWidget effects_list;

        require(favorites.count() == 0,
                "Effects Favorites must initially be empty.");
        require(toolbox.count() == 5,
                "Effects Toolbox must contain five implemented categories.");
        require(toolbox.item(0)->text() == "All",
                "All category must be first.");
        require(toolbox.item(1)->text() == "Video",
                "Video category is missing.");
        require(toolbox.item(2)->text() == "Audio",
                "Audio category is missing.");
        require(toolbox.item(3)->text() == "Transitions",
                "Transitions category is missing.");
        require(toolbox.item(4)->text() == "Text",
                "Text category is missing.");
        require(toolbox.currentCategoryId() == "all",
                "Effects Toolbox must initially select All.");
        require(effects_list.count() == 5,
                "Effects list must contain five implemented effects.");
        require(effects_list.categoryId() == "all",
                "Effects list must initially show All.");
        require(effects_list.visibleEffectCount() == 5,
                "All category must show every effect.");
        require(effects_list.item(0)->data(Qt::UserRole).toString() ==
                    "video.grayscale",
                "Effect IDs must be stable.");

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
        require(effects_list.visibleEffectCount() == 1,
                "Video category must show one effect.");

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
                    effects_list.visibleEffectCount() == 5,
                "Unknown categories must fall back to All.");

        require(effects::categories().size() == 5,
                "Shared effect catalog contains unimplemented categories.");
        require(effects::definitions().size() == 5,
                "Shared effect catalog definitions are incomplete.");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}

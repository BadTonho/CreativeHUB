#pragma once
#include "model/composition_document.h"
#include <QWidget>
#include <array>
#include <functional>
class QCheckBox;
class QDoubleSpinBox;
class QToolButton;
class QComboBox;
class QListWidget;
namespace motion::ui {
class TrimPathsInspector final : public QWidget {
  public:
    using Edit = std::function<bool(model::CompositionDocument &)>;
    using EditHandler = std::function<void(Edit)>;
    using CurveHandler = std::function<void(model::LayerId, model::TrimPathProperty, std::int64_t)>;
    explicit TrimPathsInspector(QWidget *parent = nullptr);
    void setHandlers(EditHandler edit, CurveHandler curve);
    void sync(const model::CompositionDocument *, model::LayerId, std::int64_t frame, bool blocked);
    void selectProperty(model::TrimPathProperty);

  private:
    void refresh();
    const model::CompositionDocument *document_ = nullptr;
    model::LayerId layer_ = 0;
    std::int64_t frame_ = 0;
    bool blocked_ = false, syncing_ = false;
    EditHandler edit_;
    CurveHandler curve_;
    QCheckBox *enabled_;
    std::array<QDoubleSpinBox *, 3> fields_{};
    std::array<QToolButton *, 3> keys_{};
    QComboBox *property_;
    QListWidget *key_list_;
};
} // namespace motion::ui

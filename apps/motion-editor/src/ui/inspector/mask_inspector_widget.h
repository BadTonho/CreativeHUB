#pragma once
#include "model/composition_document.h"
#include <QWidget>
#include <functional>
class QComboBox;class QDoubleSpinBox;class QCheckBox;class QPushButton;class QLabel;class QListWidget;
namespace motion::ui {
class MaskInspectorWidget final : public QWidget {
public:
    using Edit=std::function<bool(model::CompositionDocument&)>;
    using EditHandler=std::function<void(Edit)>;
    using ViewHandler=std::function<void(model::LayerId,int,bool)>;
    using CurveHandler=std::function<void(model::MaskId,std::size_t,std::int64_t)>;
    explicit MaskInspectorWidget(QWidget* parent=nullptr);
    void setHandlers(EditHandler edit,ViewHandler view,CurveHandler curve);
    void sync(const model::CompositionDocument*,model::LayerId,std::int64_t frame);
private:
    void refresh();void apply();void toggleKey(std::size_t);void clearView();
    model::MaskId selectedMask() const;
    const model::CompositionDocument* document_=nullptr;
    model::LayerId layer_=0;std::int64_t frame_=0;int target_=-2;
    bool syncing_=false;
    EditHandler edit_;ViewHandler view_;CurveHandler curve_;
    QComboBox* targets_;QComboBox* masks_;QComboBox* properties_;
    std::array<double,6> shown_values_{};
    std::array<QDoubleSpinBox*,6> fields_{};std::array<QPushButton*,5> keys_{};
    QCheckBox* invert_;QPushButton* view_button_;QLabel* uses_;QListWidget* key_list_;
};
}

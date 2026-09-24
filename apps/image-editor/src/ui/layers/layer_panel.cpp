#include "layer_panel.h"

#include <QAbstractItemView>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QSignalBlocker>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace image_editor {
namespace {

constexpr int kLayerIdRole = Qt::UserRole + 1;
constexpr int kBackgroundRole = Qt::UserRole + 2;

} // namespace

LayerPanel::LayerPanel(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("imageEditorLayerPanel"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(6);

    layer_list_ = new QListWidget(this);
    layer_list_->setObjectName(QStringLiteral("imageLayerList"));
    layer_list_->setSelectionMode(QAbstractItemView::SingleSelection);
    layer_list_->setEditTriggers(QAbstractItemView::DoubleClicked |
                                 QAbstractItemView::EditKeyPressed |
                                 QAbstractItemView::SelectedClicked);
    layout->addWidget(layer_list_, 1);

    edit_hint_ = new QLabel(this);
    edit_hint_->setObjectName(QStringLiteral("layerEditingHint"));
    edit_hint_->setWordWrap(true);
    edit_hint_->setText(QStringLiteral("Select or create an editable layer to paint or transform."));
    layout->addWidget(edit_hint_);

    auto* actions = new QHBoxLayout;
    add_button_ = new QToolButton(this);
    add_button_->setObjectName(QStringLiteral("addImageLayerButton"));
    add_button_->setText(QStringLiteral("+"));
    add_button_->setToolTip(QStringLiteral("Add layer"));
    actions->addWidget(add_button_);
    delete_button_ = new QToolButton(this);
    delete_button_->setObjectName(QStringLiteral("deleteImageLayerButton"));
    delete_button_->setText(QStringLiteral("−"));
    delete_button_->setToolTip(QStringLiteral("Delete layer"));
    actions->addWidget(delete_button_);
    rename_button_ = new QToolButton(this);
    rename_button_->setObjectName(QStringLiteral("renameImageLayerButton"));
    rename_button_->setText(QStringLiteral("Rename"));
    rename_button_->setToolTip(QStringLiteral("Rename layer"));
    actions->addWidget(rename_button_);
    move_up_button_ = new QToolButton(this);
    move_up_button_->setObjectName(QStringLiteral("moveImageLayerUpButton"));
    move_up_button_->setText(QStringLiteral("↑"));
    move_up_button_->setToolTip(QStringLiteral("Move layer up"));
    actions->addWidget(move_up_button_);
    move_down_button_ = new QToolButton(this);
    move_down_button_->setObjectName(QStringLiteral("moveImageLayerDownButton"));
    move_down_button_->setText(QStringLiteral("↓"));
    move_down_button_->setToolTip(QStringLiteral("Move layer down"));
    actions->addWidget(move_down_button_);
    layout->addLayout(actions);

    auto* opacity_row = new QHBoxLayout;
    auto* opacity_label = new QLabel(QStringLiteral("Opacity"), this);
    opacity_label->setObjectName(QStringLiteral("imageLayerOpacityLabel"));
    opacity_row->addWidget(opacity_label);
    opacity_slider_ = new QSlider(Qt::Horizontal, this);
    opacity_slider_->setObjectName(QStringLiteral("imageLayerOpacitySlider"));
    opacity_slider_->setRange(0, 100);
    opacity_slider_->setValue(100);
    opacity_slider_->setAccessibleName(QStringLiteral("Layer opacity"));
    opacity_row->addWidget(opacity_slider_, 1);
    layout->addLayout(opacity_row);

    connect(layer_list_, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem* current) {
                if (refreshing_ || current == nullptr) return;
                emit layerSelected(current->data(kLayerIdRole).toString());
                updateControls();
            });
    connect(layer_list_, &QListWidget::itemChanged, this,
            [this](QListWidgetItem* item) {
                if (refreshing_ || item == nullptr) return;
                const QString id = item->data(kLayerIdRole).toString();
                const auto found = std::find_if(layers_.cbegin(), layers_.cend(),
                    [&id](const ImageLayerData& layer) { return layer.id == id; });
                if (found == layers_.cend()) return;
                if (item->text() != found->name && !found->background) {
                    emit layerRenamed(id, item->text());
                    return;
                }
                const bool visible = item->checkState() == Qt::Checked;
                if (visible != found->visible) emit layerVisibilityChanged(id, visible);
            });
    connect(add_button_, &QToolButton::clicked, this,
            &LayerPanel::addLayerRequested);
    connect(delete_button_, &QToolButton::clicked, this,
            [this]() { emit deleteLayerRequested(selectedLayerId()); });
    connect(rename_button_, &QToolButton::clicked, this, [this]() {
        if (layer_list_->currentItem() != nullptr &&
            !layer_list_->currentItem()->data(kBackgroundRole).toBool()) {
            layer_list_->editItem(layer_list_->currentItem());
        }
    });
    connect(move_up_button_, &QToolButton::clicked, this,
            [this]() { emit moveLayerRequested(selectedLayerId(), 1); });
    connect(move_down_button_, &QToolButton::clicked, this,
            [this]() { emit moveLayerRequested(selectedLayerId(), -1); });
    connect(opacity_slider_, &QSlider::sliderPressed, this,
            &LayerPanel::opacityEditStarted);
    connect(opacity_slider_, &QSlider::sliderReleased, this,
            &LayerPanel::opacityEditFinished);
    connect(opacity_slider_, &QSlider::valueChanged, this, [this](int value) {
        if (!refreshing_ && !selectedLayerId().isEmpty()) {
            emit layerOpacityChanged(selectedLayerId(), value);
        }
    });
    updateControls();
}

void LayerPanel::setLayers(const QVector<ImageLayerData>& layers,
                           const QString& selected_layer_id) {
    refreshing_ = true;
    const QSignalBlocker blocker(layer_list_);
    layers_ = layers;
    layer_list_->clear();
    QListWidgetItem* selected = nullptr;
    for (auto it = layers.crbegin(); it != layers.crend(); ++it) {
        auto* item = new QListWidgetItem(it->name, layer_list_);
        item->setData(kLayerIdRole, it->id);
        item->setData(kBackgroundRole, it->background);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable |
                       Qt::ItemIsEnabled);
        if (!it->background) item->setFlags(item->flags() | Qt::ItemIsEditable);
        else item->setToolTip(QStringLiteral("Locked Background layer"));
        item->setCheckState(it->visible ? Qt::Checked : Qt::Unchecked);
        if (it->id == selected_layer_id) selected = item;
    }
    if (selected != nullptr) layer_list_->setCurrentItem(selected);
    const auto active = std::find_if(layers.cbegin(), layers.cend(),
        [&selected_layer_id](const ImageLayerData& layer) {
            return layer.id == selected_layer_id;
        });
    {
        const QSignalBlocker opacity_blocker(opacity_slider_);
        opacity_slider_->setValue(active == layers.cend() ? 100 : active->opacity);
    }
    refreshing_ = false;
    updateControls();
}

QString LayerPanel::selectedLayerId() const {
    return layer_list_->currentItem() == nullptr
        ? QString{} : layer_list_->currentItem()->data(kLayerIdRole).toString();
}

void LayerPanel::updateControls() {
    const QString id = selectedLayerId();
    const auto active = std::find_if(layers_.cbegin(), layers_.cend(),
        [&id](const ImageLayerData& layer) { return layer.id == id; });
    const bool selected = active != layers_.cend();
    const bool editable = selected && !active->background;
    edit_hint_->setVisible(selected && active->background);
    add_button_->setEnabled(!layers_.isEmpty());
    delete_button_->setEnabled(editable);
    rename_button_->setEnabled(editable);
    move_up_button_->setEnabled(editable && active + 1 != layers_.cend());
    move_down_button_->setEnabled(editable && active != layers_.cbegin() + 1);
    opacity_slider_->setEnabled(editable);
}

} // namespace image_editor

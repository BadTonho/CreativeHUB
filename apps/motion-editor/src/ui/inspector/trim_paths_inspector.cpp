#include "trim_paths_inspector.h"
#include "ui/translation_helpers.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
namespace motion::ui {
namespace m = model;
TrimPathsInspector::TrimPathsInspector(QWidget *parent) : QWidget(parent) {
    setObjectName("motion-trim-paths-inspector");
    auto *layout = new QVBoxLayout(this);
    auto *group = new QGroupBox(MotionUiTranslation::tr("Trim Paths"), this);
    layout->addWidget(group);
    auto *form = new QFormLayout(group);
    enabled_ = new QCheckBox(MotionUiTranslation::tr("Enable Trim Paths"), group);
    enabled_->setObjectName("motion-trim-enabled");
    form->addRow(enabled_);
    const std::array names{MotionUiTranslation::tr("Start (%)"), MotionUiTranslation::tr("End (%)"),
                           MotionUiTranslation::tr("Offset (degrees)")};
    for (std::size_t i = 0; i < 3; ++i) {
        auto *row = new QWidget(group);
        auto *horizontal = new QHBoxLayout(row);
        horizontal->setContentsMargins(0, 0, 0, 0);
        auto *field = fields_[i] = new QDoubleSpinBox(row);
        field->setObjectName(QString("motion-trim-value-%1").arg(i));
        field->setDecimals(6);
        field->setRange(i == 2 ? -1e9 : 0, i == 2 ? 1e9 : 100);
        field->setKeyboardTracking(false);
        field->setSuffix(i == 2 ? QString::fromUtf8(" °") : QStringLiteral(" %"));
        horizontal->addWidget(field, 1);
        auto *key = keys_[i] = new QToolButton(row);
        key->setObjectName(QString("motion-trim-key-%1").arg(i));
        key->setCheckable(true);
        key->setText(QString::fromUtf8("◇"));
        key->setToolTip(MotionUiTranslation::tr("Toggle Keyframe"));
        horizontal->addWidget(key);
        form->addRow(names[i], row);
        connect(field, &QDoubleSpinBox::valueChanged, this, [this, i](double value) {
            if (syncing_ || blocked_ || !edit_)
                return;
            const auto id = layer_;
            const auto frame = frame_;
            const auto p = static_cast<m::TrimPathProperty>(i);
            edit_([=](auto &doc) { return doc.setLayerTrimValueAtFrame(id, p, value, frame); });
        });
        connect(key, &QToolButton::clicked, this, [this, i] {
            if (syncing_ || blocked_ || !edit_ || !document_)
                return;
            const auto id = layer_;
            const auto frame = frame_;
            const auto *animation = document_->layerTrimPaths(id);
            if (!animation)
                return;
            const auto found = std::find_if(document_->layers().begin(), document_->layers().end(),
                                            [id](const auto &l) { return l.id == id; });
            if (found == document_->layers().end())
                return;
            const auto local = frame - found->timeline_start_frame;
            const auto &keys = animation->get().keys[i];
            const bool exists =
                std::any_of(keys.begin(), keys.end(), [local](const auto &k) { return k.frame == local; });
            const auto p = static_cast<m::TrimPathProperty>(i);
            edit_([=](auto &doc) {
                return exists ? doc.removeLayerTrimKeyframe(id, p, local)
                              : doc.setLayerTrimKeyframe(id, p, frame);
            });
        });
    }
    auto *help =
        new QLabel(MotionUiTranslation::tr("Only the stroke is trimmed. Wrapped portions reappear at the "
                                           "start, including on open paths. 360 degrees is one turn."),
                   this);
    help->setWordWrap(true);
    layout->addWidget(help);
    property_ = new QComboBox(this);
    property_->setObjectName("motion-trim-property");
    for (const auto &name : names)
        property_->addItem(name);
    layout->addWidget(property_);
    key_list_ = new QListWidget(this);
    key_list_->setObjectName("motion-trim-keyframes");
    key_list_->setMaximumHeight(180);
    layout->addWidget(key_list_);
    layout->addStretch();
    connect(enabled_, &QCheckBox::toggled, this, [this](bool value) {
        if (syncing_ || blocked_ || !edit_)
            return;
        const auto id = layer_;
        edit_([=](auto &doc) { return doc.setLayerTrimPathsEnabled(id, value); });
    });
    connect(property_, &QComboBox::currentIndexChanged, this, [this] {
        if (!syncing_)
            refresh();
    });
    connect(key_list_, &QListWidget::itemClicked, this, [this](auto *item) {
        if (curve_)
            curve_(layer_, static_cast<m::TrimPathProperty>(property_->currentIndex()),
                   item->data(Qt::UserRole).toLongLong());
    });
    refresh();
}
void TrimPathsInspector::setHandlers(EditHandler edit, CurveHandler curve) {
    edit_ = std::move(edit);
    curve_ = std::move(curve);
}
void TrimPathsInspector::selectProperty(m::TrimPathProperty property) {
    if (m::validTrimProperty(property))
        property_->setCurrentIndex(static_cast<int>(property));
}
void TrimPathsInspector::sync(const m::CompositionDocument *doc, m::LayerId layer, std::int64_t frame,
                              bool blocked) {
    document_ = doc;
    layer_ = layer;
    frame_ = frame;
    blocked_ = blocked;
    refresh();
}
void TrimPathsInspector::refresh() {
    syncing_ = true;
    const auto *animation = document_ ? document_->layerTrimPaths(layer_) : nullptr;
    const m::CompositionLayer *layer = nullptr;
    if (animation)
        for (const auto &l : document_->layers())
            if (l.id == layer_) {
                layer = &l;
                break;
            }
    setEnabled(animation && !blocked_);
    const auto local = layer ? frame_ - layer->timeline_start_frame : 0;
    const bool active = layer && local >= 0 && local < layer->duration_frames;
    const auto values = animation ? m::evaluateTrimPaths(*animation, local) : m::TrimPathValues{};
    {
        QSignalBlocker block(enabled_);
        enabled_->setChecked(values.enabled);
    }
    for (std::size_t i = 0; i < 3; ++i) {
        static const creative_suite::animation::ScalarKeyframes empty;
        const auto p = static_cast<m::TrimPathProperty>(i);
        const auto &keys = animation ? animation->get().keys[i] : empty;
        const bool at =
            std::any_of(keys.begin(), keys.end(), [local](const auto &k) { return k.frame == local; });
        QSignalBlocker a(fields_[i]), b(keys_[i]);
        fields_[i]->setValue(m::trimValue(values, p));
        fields_[i]->setReadOnly(!active || (!keys.empty() && !at));
        keys_[i]->setEnabled(active && !blocked_);
        keys_[i]->setChecked(at);
        fields_[i]->setToolTip(
            !keys.empty() && !at ? MotionUiTranslation::tr("Add a keyframe to edit this interpolated value.")
                                 : QString{});
    }
    key_list_->clear();
    if (animation)
        for (const auto &key : animation->get().keys[property_->currentIndex()]) {
            auto *item = new QListWidgetItem(
                MotionUiTranslation::tr("Frame %1: %2").arg(key.frame).arg(key.value, 0, 'f', 6), key_list_);
            item->setData(Qt::UserRole, QVariant::fromValue<qlonglong>(key.frame));
        }
    syncing_ = false;
}
} // namespace motion::ui

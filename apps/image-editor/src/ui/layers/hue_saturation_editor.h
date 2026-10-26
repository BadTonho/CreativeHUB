#pragma once
#include <creative_suite/effects/adjustment.h>
#include <QWidget>
#include <QSlider>
#include <QSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QComboBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QCoreApplication>
#include <QEvent>
#include <QTimer>
#include <array>
#include <functional>

namespace image_editor {
class HueSaturationEditor final : public QWidget {
public:
    using Parameters = creative_suite::effects::HueSaturationParameters;
    std::function<void()> started, finished;
    std::function<void(const Parameters&)> changed;

    explicit HueSaturationEditor(QWidget* parent) : QWidget(parent) {
        setObjectName("hueSaturationEditor");
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        colorize_ = new QCheckBox(QCoreApplication::translate("ImageEditorUI", "Colorize"), this);
        colorize_->setObjectName("colorizeCheckBox");
        layout->addWidget(colorize_);
        connect(colorize_, &QCheckBox::toggled, this, [this](bool enabled) {
            if (syncing_ || value_.colorize_enabled == enabled) return;
            finish(); begin(); value_.colorize_enabled = enabled; sync();
            if (changed) changed(value_);
            finish();
        });
        range_ = new QComboBox(this);
        range_->setObjectName("hueRangeSelector");
        for (const char* label : {QT_TRANSLATE_NOOP("ImageEditorUI", "Global"),
                QT_TRANSLATE_NOOP("ImageEditorUI", "Reds"), QT_TRANSLATE_NOOP("ImageEditorUI", "Yellows"),
                QT_TRANSLATE_NOOP("ImageEditorUI", "Greens"), QT_TRANSLATE_NOOP("ImageEditorUI", "Cyans"),
                QT_TRANSLATE_NOOP("ImageEditorUI", "Blues"), QT_TRANSLATE_NOOP("ImageEditorUI", "Magentas")})
            range_->addItem(QCoreApplication::translate("ImageEditorUI", label));
        range_->setAccessibleName(QCoreApplication::translate("ImageEditorUI", "Color range"));
        layout->addWidget(range_);
        connect(range_, &QComboBox::currentIndexChanged, this, [this] { if (!syncing_) { finish(); sync(); } });
        const char* labels[]{QT_TRANSLATE_NOOP("ImageEditorUI", "Hue"),
            QT_TRANSLATE_NOOP("ImageEditorUI", "Saturation"), QT_TRANSLATE_NOOP("ImageEditorUI", "Lightness")};
        const char* names[]{"hue", "saturation", "lightness"};
        const int low[]{-180, -100, -100}, high[]{180, 100, 100};
        for (int i = 0; i < 3; ++i) {
            auto* row = new QHBoxLayout;
            const auto label = QCoreApplication::translate("ImageEditorUI", labels[i]);
            row->addWidget(new QLabel(label, this));
            sliders_[i] = new QSlider(Qt::Horizontal, this);
            sliders_[i]->setRange(low[i], high[i]);
            sliders_[i]->setObjectName(QString::fromLatin1(names[i]) + "Slider");
            spins_[i] = new QSpinBox(this);
            spins_[i]->setRange(low[i], high[i]);
            spins_[i]->setSingleStep(1);
            spins_[i]->setObjectName(QString::fromLatin1(names[i]) + "Spin");
            spins_[i]->setSuffix(i == 0 ? QStringLiteral("\u00b0") : QStringLiteral("%"));
            sliders_[i]->setAccessibleName(label);
            spins_[i]->setAccessibleName(label);
            sliders_[i]->installEventFilter(this);
            spins_[i]->installEventFilter(this);
            row->addWidget(sliders_[i]); row->addWidget(spins_[i]); layout->addLayout(row);
            connect(sliders_[i], &QSlider::sliderPressed, this, [this] { finish(); begin(); });
            connect(sliders_[i], &QSlider::valueChanged, this, [this, i](int v) { edit(i, v); });
            connect(spins_[i], &QSpinBox::valueChanged, this, [this, i](int v) { edit(i, v); });
            connect(sliders_[i], &QSlider::sliderReleased, this, [this] { finish(); });
            connect(spins_[i], &QSpinBox::editingFinished, this, [this] { finish(); });
        }
        reset_ = new QPushButton(QCoreApplication::translate("ImageEditorUI", "Reset"), this);
        reset_->setObjectName("resetHueSaturationButton"); layout->addWidget(reset_);
        connect(reset_, &QPushButton::clicked, this, [this] {
            finish();
            auto next = value_;
            if (next.colorize_enabled) next.colorize = {};
            else if (range_->currentIndex() > 0) next.ranges[range_->currentIndex() - 1] = {};
            else { next.hue = 0; next.saturation = 0; next.lightness = 0; }
            if (next == value_) return;
            begin(); value_ = next; sync(); if (changed) changed(value_); finish();
        });
        sync();
    }
    void setValue(const QString& id, const Parameters& value) {
        if (id_ != id) { finish(); range_->setCurrentIndex(0); }
        id_ = id; value_ = value; sync();
    }
    void finish() {
        if (!active_) return;
        active_ = false;
        if (finished) finished();
    }
protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() == QEvent::FocusOut) finish();
        for (auto* slider : sliders_) if (watched == slider) {
            if (event->type() == QEvent::KeyRelease) finish();
            if (event->type() == QEvent::Wheel) QTimer::singleShot(0, this, [this] { finish(); });
        }
        return QWidget::eventFilter(watched, event);
    }
private:
    void begin() { if (!active_) { active_ = true; if (started) started(); } }
    void edit(int field, int value) {
        if (syncing_) return;
        auto next = value_;
        if (next.colorize_enabled)
            (field == 0 ? next.colorize.hue : field == 1 ? next.colorize.saturation : next.colorize.lightness) = value;
        else if (range_->currentIndex() > 0) {
            auto& r = next.ranges[range_->currentIndex() - 1];
            (field == 0 ? r.hue : field == 1 ? r.saturation : r.lightness) = value;
        }
        else (field == 0 ? next.hue : field == 1 ? next.saturation : next.lightness) = value;
        if (next == value_) return;
        begin(); value_ = next; sync(); if (changed) changed(value_);
    }
    void sync() {
        syncing_ = true;
        const auto selected = range_->currentIndex() > 0 ? value_.ranges[range_->currentIndex() - 1] :
            creative_suite::effects::HueRangeParameters{value_.hue, value_.saturation, value_.lightness};
        const int values[]{value_.colorize_enabled ? value_.colorize.hue : selected.hue,
            value_.colorize_enabled ? value_.colorize.saturation : selected.saturation,
            value_.colorize_enabled ? value_.colorize.lightness : selected.lightness};
        const int low[]{value_.colorize_enabled ? 0 : -180, value_.colorize_enabled ? 0 : -100, -100};
        const int high[]{value_.colorize_enabled ? 360 : 180, 100, 100};
        for (int i = 0; i < 3; ++i) {
            sliders_[i]->setRange(low[i], high[i]);
            spins_[i]->setRange(low[i], high[i]);
            sliders_[i]->setValue(values[i]);
            spins_[i]->setValue(values[i]);
        }
        colorize_->setChecked(value_.colorize_enabled);
        range_->setEnabled(!value_.colorize_enabled);
        reset_->setEnabled(value_.colorize_enabled ? value_.colorize != creative_suite::effects::ColorizeParameters{} :
            selected != creative_suite::effects::HueRangeParameters{});
        syncing_ = false;
    }
    QString id_;
    Parameters value_;
    std::array<QSlider*, 3> sliders_{};
    std::array<QSpinBox*, 3> spins_{};
    QCheckBox* colorize_ = nullptr;
    QComboBox* range_ = nullptr;
    QPushButton* reset_ = nullptr;
    bool syncing_ = false, active_ = false;
};
} // namespace image_editor

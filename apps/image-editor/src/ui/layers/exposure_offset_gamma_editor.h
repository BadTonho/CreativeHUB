#pragma once
#include <creative_suite/effects/adjustment.h>
#include <QWidget>
#include <QSlider>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QCoreApplication>
#include <QEvent>
#include <QTimer>
#include <array>
#include <cmath>
#include <functional>

namespace image_editor {
class ExposureOffsetGammaEditor final : public QWidget {
public:
    using Parameters = creative_suite::effects::ExposureOffsetGammaParameters;
    std::function<void()> started, finished;
    std::function<void(const Parameters&)> changed;

    explicit ExposureOffsetGammaEditor(QWidget* parent) : QWidget(parent) {
        setObjectName("exposureOffsetGammaEditor");
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        const char* labels[]{QT_TRANSLATE_NOOP("ImageEditorUI", "Exposure"),
            QT_TRANSLATE_NOOP("ImageEditorUI", "Offset"), QT_TRANSLATE_NOOP("ImageEditorUI", "Gamma")};
        const char* names[]{"exposure", "offset", "gamma"};
        const double low[]{-20, -.5, .01}, high[]{20, .5, 9.99};
        for (int i = 0; i < 3; ++i) {
            auto* row = new QHBoxLayout;
            const auto label = QCoreApplication::translate("ImageEditorUI", labels[i]);
            row->addWidget(new QLabel(label, this));
            sliders_[i] = new QSlider(Qt::Horizontal, this);
            sliders_[i]->setRange(int(std::lround(low[i]*scales_[i])), int(std::lround(high[i]*scales_[i])));
            sliders_[i]->setObjectName(QString::fromLatin1(names[i]) + "Slider");
            spins_[i] = new QDoubleSpinBox(this);
            spins_[i]->setDecimals(i == 1 ? 4 : 2);
            spins_[i]->setRange(low[i], high[i]);
            spins_[i]->setSingleStep(1. / scales_[i]);
            spins_[i]->setObjectName(QString::fromLatin1(names[i]) + "Spin");
            if (i == 0) spins_[i]->setSuffix(QCoreApplication::translate("ImageEditorUI", " stops"));
            sliders_[i]->setAccessibleName(label);
            spins_[i]->setAccessibleName(label);
            sliders_[i]->installEventFilter(this);
            spins_[i]->installEventFilter(this);
            row->addWidget(sliders_[i]); row->addWidget(spins_[i]); layout->addLayout(row);
            connect(sliders_[i], &QSlider::sliderPressed, this, [this] { finish(); begin(); });
            connect(sliders_[i], &QSlider::valueChanged, this, [this, i](int v) { edit(i, v / scales_[i]); });
            connect(spins_[i], &QDoubleSpinBox::valueChanged, this, [this, i](double v) { edit(i, v); });
            connect(sliders_[i], &QSlider::sliderReleased, this, [this] { finish(); });
            connect(spins_[i], &QDoubleSpinBox::editingFinished, this, [this] { finish(); });
        }
        reset_ = new QPushButton(QCoreApplication::translate("ImageEditorUI", "Reset"), this);
        reset_->setObjectName("resetExposureOffsetGammaButton"); layout->addWidget(reset_);
        connect(reset_, &QPushButton::clicked, this, [this] {
            finish();
            if (value_ == Parameters{}) return;
            begin(); value_ = {}; sync(); if (changed) changed(value_); finish();
        });
        sync();
    }
    void setValue(const QString& id, const Parameters& value) {
        if (id_ != id) finish();
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
    void edit(int field, double value) {
        if (syncing_) return;
        auto next = value_;
        (field == 0 ? next.exposure : field == 1 ? next.offset : next.gamma) = value;
        if (next == value_) return;
        begin(); value_ = next; sync(); if (changed) changed(value_);
    }
    void sync() {
        syncing_ = true;
        const double values[]{value_.exposure, value_.offset, value_.gamma};
        for (int i = 0; i < 3; ++i) {
            sliders_[i]->setValue(int(std::lround(values[i]*scales_[i])));
            spins_[i]->setValue(values[i]);
        }
        reset_->setEnabled(value_ != Parameters{});
        syncing_ = false;
    }
    static constexpr std::array<double, 3> scales_{100, 10000, 100};
    QString id_;
    Parameters value_;
    std::array<QSlider*, 3> sliders_{};
    std::array<QDoubleSpinBox*, 3> spins_{};
    QPushButton* reset_ = nullptr;
    bool syncing_ = false, active_ = false;
};
} // namespace image_editor

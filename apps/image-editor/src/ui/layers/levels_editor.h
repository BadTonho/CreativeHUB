#pragma once

#include <creative_suite/effects/adjustment.h>
#include <QCoreApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>

namespace image_editor {

class LevelsHistogram : public QWidget {
public:
    using Bins = std::array<std::uint64_t, 256>;
    explicit LevelsHistogram(QWidget* parent) : QWidget(parent) {
        setObjectName("levelsHistogram");
        setMinimumHeight(84);
        setAccessibleName(QCoreApplication::translate("ImageEditorUI", "Input histogram"));
    }
    void setBins(const Bins& bins) { bins_ = bins; update(); }
    [[nodiscard]] const Bins& bins() const noexcept { return bins_; }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.fillRect(rect(), palette().base());
        painter.setPen(palette().mid().color());
        painter.drawRect(rect().adjusted(0, 0, -1, -1));
        const auto maximum = *std::max_element(bins_.begin(), bins_.end());
        if (!maximum || width() <= 2 || height() <= 2) return;
        painter.setPen(palette().text().color());
        for (int x = 1; x < width() - 1; ++x) {
            const int index = (x - 1) * 256 / (width() - 2);
            const int top = height() - 2 - int(double(bins_[index]) / maximum * (height() - 3));
            painter.drawLine(x, height() - 2, x, top);
        }
    }
private:
    Bins bins_{};
};

class LevelsEditor final : public QWidget {
public:
    using Parameters = creative_suite::effects::LevelsParameters;
    using Bins = LevelsHistogram::Bins;
    std::function<void()> started, finished;
    std::function<void(const Parameters&)> changed;

    explicit LevelsEditor(QWidget* parent) : QWidget(parent) {
        setObjectName("levelsEditor");
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        auto* channel_row = new QHBoxLayout;
        channel_row->addWidget(new QLabel(trUi("Channel"), this));
        channel_ = new QComboBox(this);
        channel_->setObjectName("levelsChannelCombo");
        channel_->setAccessibleName(trUi("Levels channel"));
        channel_->addItem(trUi("RGB"));
        channel_->addItem(trUi("Red"));
        channel_->addItem(trUi("Green"));
        channel_->addItem(trUi("Blue"));
        channel_row->addWidget(channel_);
        channel_row->addStretch(1);
        layout->addLayout(channel_row);
        histogram_ = new LevelsHistogram(this);
        layout->addWidget(histogram_);
        const char* labels[]{"Input Black", "Input White", "Midtone Gamma", "Output Black", "Output White"};
        const char* names[]{"inputBlack", "inputWhite", "midtoneGamma", "outputBlack", "outputWhite"};
        for (int i = 0; i < 5; ++i) {
            auto* row = new QHBoxLayout;
            const auto label = QCoreApplication::translate("ImageEditorUI", labels[i]);
            row->addWidget(new QLabel(label, this));
            sliders_[i] = new QSlider(Qt::Horizontal, this);
            sliders_[i]->setObjectName(QString::fromLatin1(names[i]) + "Slider");
            sliders_[i]->setRange(i == 2 ? 10 : 0, i == 2 ? 999 : 255);
            sliders_[i]->setAccessibleName(label);
            sliders_[i]->installEventFilter(this);
            row->addWidget(sliders_[i]);
            if (i == 2) {
                gamma_spin_ = new QDoubleSpinBox(this);
                gamma_spin_->setObjectName("midtoneGammaSpin");
                gamma_spin_->setRange(.1, 9.99);
                gamma_spin_->setSingleStep(.01);
                gamma_spin_->setDecimals(2);
                gamma_spin_->setAccessibleName(label);
                gamma_spin_->installEventFilter(this);
                row->addWidget(gamma_spin_);
                connect(gamma_spin_, &QDoubleSpinBox::valueChanged, this,
                    [this](double value) { edit(2, int(std::lround(value * 100))); });
                connect(gamma_spin_, &QDoubleSpinBox::editingFinished, this, [this] { finish(); });
            } else {
                spins_[i] = new QSpinBox(this);
                spins_[i]->setObjectName(QString::fromLatin1(names[i]) + "Spin");
                spins_[i]->setRange(0, 255);
                spins_[i]->setAccessibleName(label);
                spins_[i]->installEventFilter(this);
                row->addWidget(spins_[i]);
                connect(spins_[i], &QSpinBox::valueChanged, this,
                    [this, i](int value) { edit(i, value); });
                connect(spins_[i], &QSpinBox::editingFinished, this, [this] { finish(); });
            }
            layout->addLayout(row);
            connect(sliders_[i], &QSlider::sliderPressed, this, [this] { finish(); begin(); });
            connect(sliders_[i], &QSlider::valueChanged, this,
                [this, i](int value) { edit(i, value); });
            connect(sliders_[i], &QSlider::sliderReleased, this, [this] { finish(); });
        }
        reset_ = new QPushButton(QCoreApplication::translate("ImageEditorUI", "Reset"), this);
        reset_->setObjectName("resetLevelsButton");
        layout->addWidget(reset_);
        connect(channel_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int index) {
                finish();
                channel_index_ = std::clamp(index, 0, 3);
                sync();
            });
        connect(reset_, &QPushButton::clicked, this, [this] {
            finish();
            if (activeValue() == creative_suite::effects::LevelsChannelParameters{}) return;
            begin();
            creative_suite::effects::setLevelsChannel(&value_, channel_index_, {});
            sync();
            if (changed) changed(value_);
            finish();
        });
        sync();
    }
    void setValue(const QString& id, const Parameters& value) {
        if (id_ != id) {
            finish();
            channel_index_ = 0;
            const QSignalBlocker block(channel_);
            channel_->setCurrentIndex(0);
        }
        id_ = id; value_ = value; sync();
    }
    void setHistogram(const Bins& bins) { histogram_->setBins(bins); }
    [[nodiscard]] const Bins& histogram() const noexcept { return histogram_->bins(); }
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
    static QString trUi(const char* text) {
        return QCoreApplication::translate("ImageEditorUI", text);
    }
    [[nodiscard]] creative_suite::effects::LevelsChannelParameters activeValue() const {
        return creative_suite::effects::levelsChannel(value_, channel_index_);
    }
    void begin() { if (!active_) { active_ = true; if (started) started(); } }
    void edit(int field, int value) {
        if (syncing_) return;
        auto next = value_;
        auto controls = activeValue();
        switch (field) {
        case 0: controls.input_black = value; break;
        case 1: controls.input_white = value; break;
        case 2: controls.gamma = value / 100.; break;
        case 3: controls.output_black = value; break;
        case 4: controls.output_white = value; break;
        }
        if (controls == activeValue() || !creative_suite::effects::validLevelsControls(controls)) return;
        creative_suite::effects::setLevelsChannel(&next, channel_index_, controls);
        if (!creative_suite::effects::validLevels(next)) return;
        begin(); value_ = next; sync(); if (changed) changed(value_);
    }
    void sync() {
        syncing_ = true;
        const auto controls = activeValue();
        sliders_[0]->setMaximum(controls.input_white - 1);
        sliders_[1]->setMinimum(controls.input_black + 1);
        sliders_[3]->setMaximum(controls.output_white);
        sliders_[4]->setMinimum(controls.output_black);
        spins_[0]->setMaximum(controls.input_white - 1);
        spins_[1]->setMinimum(controls.input_black + 1);
        spins_[3]->setMaximum(controls.output_white);
        spins_[4]->setMinimum(controls.output_black);
        const int values[]{controls.input_black, controls.input_white,
            int(std::lround(controls.gamma * 100)), controls.output_black, controls.output_white};
        for (int i = 0; i < 5; ++i) {
            sliders_[i]->setValue(values[i]);
            if (i != 2) spins_[i]->setValue(values[i]);
        }
        gamma_spin_->setValue(controls.gamma);
        reset_->setEnabled(controls != creative_suite::effects::LevelsChannelParameters{});
        syncing_ = false;
    }
    QString id_;
    Parameters value_;
    LevelsHistogram* histogram_ = nullptr;
    QComboBox* channel_ = nullptr;
    std::array<QSlider*, 5> sliders_{};
    std::array<QSpinBox*, 5> spins_{};
    QDoubleSpinBox* gamma_spin_ = nullptr;
    QPushButton* reset_ = nullptr;
    int channel_index_ = 0;
    bool syncing_ = false, active_ = false;
};

} // namespace image_editor

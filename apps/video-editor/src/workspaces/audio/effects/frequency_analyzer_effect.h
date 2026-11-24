#pragma once

#include <QCoreApplication>
#include <QDialog>
#include <QHBoxLayout>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <array>
#include <cmath>

namespace audio_effects::frequency_analyzer {

class SpectrumCanvas final : public QWidget {
public:
    explicit SpectrumCanvas(QWidget* parent = nullptr) : QWidget(parent) {
        setMinimumSize(420, 200);
    }

    void setLevel(float master_peak, float track_peak) {
        if (frozen_) return;
        const float combined = std::clamp(std::max(master_peak, track_peak), 0.0F, 1.0F);
        for (std::size_t i = 0; i < bands_.size(); ++i) {
            const float x = static_cast<float>(i) / static_cast<float>(bands_.size() - 1);
            const float shape = 0.55F + 0.45F * std::sin(x * 3.14159F * 1.4F + phase_);
            const float target = combined * shape;
            bands_[i] = target > bands_[i] ? target : bands_[i] * 0.82F;
            peaks_[i] = std::max(peaks_[i] * 0.96F, bands_[i]);
        }
        phase_ += 0.25F;
        update();
    }

    void setFrozen(bool frozen) { frozen_ = frozen; }
    void reset() {
        bands_.fill(0.0F);
        peaks_.fill(0.0F);
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.fillRect(rect(), QColor("#111317"));
        p.setPen(QColor("#232730"));
        p.drawRect(rect().adjusted(0, 0, -1, -1));

        const int w = width();
        const int h = height();
        const int pad = 10;
        const int plot_w = std::max(20, w - 2 * pad);
        const int plot_h = std::max(20, h - 2 * pad - 14);

        for (int g = 1; g <= 4; ++g) {
            const int y = pad + (plot_h * g) / 4;
            p.setPen(QColor("#1c2028"));
            p.drawLine(pad, y, pad + plot_w, y);
        }

        const int count = static_cast<int>(bands_.size());
        const double step = static_cast<double>(plot_w) / count;
        for (int i = 0; i < count; ++i) {
            const int bx = pad + static_cast<int>(i * step) + 1;
            const int bw = std::max(3, static_cast<int>(step) - 2);
            const int bh = static_cast<int>(bands_[static_cast<std::size_t>(i)] * plot_h);
            const int by = pad + plot_h - bh;
            p.fillRect(bx, by, bw, bh, QColor("#3bb2c7"));
            const int py = pad + plot_h - static_cast<int>(peaks_[static_cast<std::size_t>(i)] * plot_h);
            p.setPen(QColor("#e6b422"));
            p.drawLine(bx, py, bx + bw - 1, py);
        }

        QFont f = font();
        f.setPixelSize(9);
        p.setFont(f);
        p.setPen(QColor("#7c8596"));
        p.drawText(pad, h - 16, 40, 14, Qt::AlignLeft, QStringLiteral("20 Hz"));
        p.drawText(w / 2 - 20, h - 16, 40, 14, Qt::AlignCenter, QStringLiteral("1 kHz"));
        p.drawText(w - pad - 45, h - 16, 45, 14, Qt::AlignRight, QStringLiteral("20 kHz"));
    }

private:
    std::array<float, 31> bands_{};
    std::array<float, 31> peaks_{};
    float phase_ = 0.0F;
    bool frozen_ = false;
};

class FrequencyAnalyzerDialog final : public QDialog {
public:
    explicit FrequencyAnalyzerDialog(QWidget* parent = nullptr) : QDialog(parent) {
        setObjectName("audioFrequencyAnalyzerDialog");
        setWindowTitle(QCoreApplication::translate("AudioWorkspace", "Frequency Analyzer"));
        setStyleSheet(QStringLiteral(
            "QDialog { background: #16181c; color: #cfd6e0; }"
            "QPushButton { background: #232730; color: #cfd6e0; border: 1px solid #323743; border-radius: 3px; padding: 4px 10px; }"
            "QPushButton:checked { background: #285685; border-color: #4f88c2; color: #ffffff; }"));
        auto* layout = new QVBoxLayout(this);
        canvas_ = new SpectrumCanvas(this);
        layout->addWidget(canvas_, 1);
        auto* row = new QHBoxLayout;
        auto* freeze = new QPushButton(QCoreApplication::translate("AudioWorkspace", "Freeze"), this);
        freeze->setCheckable(true);
        auto* reset = new QPushButton(QCoreApplication::translate("AudioWorkspace", "Reset"), this);
        row->addStretch(1);
        row->addWidget(freeze);
        row->addWidget(reset);
        layout->addLayout(row);
        connect(freeze, &QPushButton::toggled, this, [this](bool v) { canvas_->setFrozen(v); });
        connect(reset, &QPushButton::clicked, this, [this] { canvas_->reset(); });
    }

    void updateLevel(float master_peak, float track_peak) {
        if (canvas_) canvas_->setLevel(master_peak, track_peak);
    }

private:
    SpectrumCanvas* canvas_ = nullptr;
};

}  // namespace audio_effects::frequency_analyzer

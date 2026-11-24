#pragma once

#include <QCoreApplication>
#include <QDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace audio_effects::meter_panel {

class MeterDetailDialog final : public QDialog {
public:
    explicit MeterDetailDialog(const QString& title, QWidget* parent = nullptr) : QDialog(parent) {
        setObjectName("audioMeterDetailDialog");
        setWindowTitle(title);
        setMinimumSize(280, 260);
        setStyleSheet(QStringLiteral(
            "QDialog { background: #16181c; color: #cfd6e0; }"
            "QLabel { color: #9ba5b5; font-size: 11px; }"
            "QProgressBar { background: #0e1014; border: 1px solid #262a33; }"
            "QProgressBar::chunk { background: qlineargradient(x1:0, y1:1, x2:0, y2:0, stop:0 #27ae60, stop:0.8 #e6b422, stop:1 #e74c3c); }"
            "QPushButton { background: #232730; color: #cfd6e0; border: 1px solid #323743; border-radius: 3px; padding: 4px 10px; }"));

        auto* outer = new QVBoxLayout(this);
        auto* body = new QHBoxLayout;
        bar_ = new QProgressBar(this);
        bar_->setOrientation(Qt::Vertical);
        bar_->setRange(0, 100);
        bar_->setTextVisible(false);
        bar_->setFixedWidth(22);
        body->addWidget(bar_);

        auto* form = new QFormLayout;
        auto make_val = [this] {
            auto* l = new QLabel(QStringLiteral("--"), this);
            l->setStyleSheet(QStringLiteral("color: #4f94d4; font-weight: 600;"));
            return l;
        };
        tp_ = make_val();
        short_ = make_val();
        short_max_ = make_val();
        integrated_ = make_val();
        form->addRow(QCoreApplication::translate("AudioWorkspace", "True Peak"), tp_);
        form->addRow(QCoreApplication::translate("AudioWorkspace", "Short"), short_);
        form->addRow(QCoreApplication::translate("AudioWorkspace", "Short Max"), short_max_);
        form->addRow(QCoreApplication::translate("AudioWorkspace", "Integrated"), integrated_);
        body->addLayout(form, 1);
        outer->addLayout(body, 1);

        auto* reset = new QPushButton(QCoreApplication::translate("AudioWorkspace", "Reset"), this);
        outer->addWidget(reset);
        connect(reset, &QPushButton::clicked, this, [this] {
            max_lu_ = -100.0F;
            sum_ = 0.0;
            samples_ = 0;
            bar_->setValue(0);
            tp_->setText(QStringLiteral("--"));
            short_->setText(QStringLiteral("--"));
            short_max_->setText(QStringLiteral("--"));
            integrated_->setText(QStringLiteral("--"));
        });
    }

    void updateLevel(float master_peak) {
        const float clamped = std::clamp(master_peak, 0.0F, 1.0F);
        bar_->setValue(static_cast<int>(std::lround(clamped * 100.0F)));
        if (clamped <= 1.0e-5F) {
            tp_->setText(QStringLiteral("-100 dB"));
            return;
        }
        const float db = 20.0F * std::log10(clamped);
        const float lu = db + 23.0F;
        max_lu_ = std::max(max_lu_, lu);
        sum_ += lu;
        ++samples_;
        tp_->setText(QString::number(db, 'f', 1) + QStringLiteral(" dB"));
        short_->setText(QString::number(lu, 'f', 1) + QStringLiteral(" LU"));
        short_max_->setText(QString::number(max_lu_, 'f', 1) + QStringLiteral(" LU"));
        integrated_->setText(QString::number(sum_ / samples_, 'f', 1) + QStringLiteral(" LU"));
    }

private:
    QProgressBar* bar_ = nullptr;
    QLabel* tp_ = nullptr;
    QLabel* short_ = nullptr;
    QLabel* short_max_ = nullptr;
    QLabel* integrated_ = nullptr;
    float max_lu_ = -100.0F;
    double sum_ = 0.0;
    int samples_ = 0;
};

}  // namespace audio_effects::meter_panel

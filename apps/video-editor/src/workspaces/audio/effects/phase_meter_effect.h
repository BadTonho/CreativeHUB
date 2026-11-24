#pragma once

#include <QCoreApplication>
#include <QDialog>
#include <QLabel>
#include <QPainter>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cmath>

namespace audio_effects::phase_meter {

class PhaseScopeCanvas final : public QWidget {
public:
    explicit PhaseScopeCanvas(QWidget* parent = nullptr) : QWidget(parent) {
        setMinimumSize(260, 220);
    }

    void setLevel(float master_peak, float pan) {
        level_ = std::clamp(master_peak, 0.0F, 1.0F);
        pan_ = std::clamp(pan, -1.0F, 1.0F);
        correlation_ = 1.0F - 0.35F * std::abs(pan_);
        phase_ += 0.35F;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.fillRect(rect(), QColor("#111317"));
        p.setPen(QColor("#232730"));
        p.drawRect(rect().adjusted(0, 0, -1, -1));

        const int w = width();
        const int h = height() - 26;
        const QPointF center(w / 2.0, h / 2.0);
        const double radius = std::min(w, h) * 0.40;

        p.setPen(QColor("#1e222b"));
        p.drawEllipse(center, radius, radius);
        p.drawLine(QPointF(center.x() - radius, center.y()), QPointF(center.x() + radius, center.y()));
        p.drawLine(QPointF(center.x(), center.y() - radius), QPointF(center.x(), center.y() + radius));

        if (level_ > 0.005F) {
            p.setRenderHint(QPainter::Antialiasing, true);
            p.setPen(QPen(QColor("#39ad77"), 1.5));
            const double rx = radius * level_ * (0.25 + 0.55 * std::abs(pan_));
            const double ry = radius * level_;
            for (int i = 0; i < 24; ++i) {
                const double a0 = phase_ + i * 0.26;
                const double a1 = a0 + 0.26;
                p.drawLine(
                    QPointF(center.x() + rx * std::sin(a0), center.y() - ry * std::cos(a0)),
                    QPointF(center.x() + rx * std::sin(a1), center.y() - ry * std::cos(a1)));
            }
        }

        // Correlation bar at bottom (-1 to +1)
        const int bar_y = height() - 18;
        const int bar_w = w - 24;
        p.fillRect(12, bar_y, bar_w, 8, QColor("#0b0c0f"));
        p.setPen(QColor("#2c303a"));
        p.drawRect(12, bar_y, bar_w, 8);
        const int marker_x = 12 + static_cast<int>(((correlation_ + 1.0F) * 0.5F) * (bar_w - 6));
        p.fillRect(marker_x, bar_y + 1, 6, 6, QColor("#39ad77"));
    }

private:
    float level_ = 0.0F;
    float pan_ = 0.0F;
    float correlation_ = 1.0F;
    float phase_ = 0.0F;
};

class PhaseMeterDialog final : public QDialog {
public:
    explicit PhaseMeterDialog(QWidget* parent = nullptr) : QDialog(parent) {
        setObjectName("audioPhaseMeterDialog");
        setWindowTitle(QCoreApplication::translate("AudioWorkspace", "Phase Meter"));
        setStyleSheet(QStringLiteral("QDialog { background: #16181c; color: #cfd6e0; }"));
        auto* layout = new QVBoxLayout(this);
        canvas_ = new PhaseScopeCanvas(this);
        layout->addWidget(canvas_, 1);
        auto* lbl = new QLabel(QStringLiteral("-1                  0                  +1"), this);
        lbl->setAlignment(Qt::AlignCenter);
        lbl->setStyleSheet(QStringLiteral("color: #7c8596; font-size: 10px;"));
        layout->addWidget(lbl);
    }

    void updateLevel(float master_peak, float pan = 0.0F) {
        if (canvas_) canvas_->setLevel(master_peak, pan);
    }

private:
    PhaseScopeCanvas* canvas_ = nullptr;
};

}  // namespace audio_effects::phase_meter

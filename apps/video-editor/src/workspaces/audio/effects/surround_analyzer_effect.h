#pragma once

#include <QCoreApplication>
#include <QDialog>
#include <QPainter>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <array>
#include <cmath>

namespace audio_effects::surround_analyzer {

class SurroundCanvas final : public QWidget {
public:
    explicit SurroundCanvas(QWidget* parent = nullptr) : QWidget(parent) {
        setMinimumSize(260, 240);
    }

    void setLevel(float master_peak, float pan) {
        const float lv = std::clamp(master_peak, 0.0F, 1.0F);
        const float p = std::clamp(pan, -1.0F, 1.0F);
        channels_[0] = lv * std::clamp(1.0F - std::max(0.0F, p), 0.2F, 1.0F);  // L
        channels_[1] = lv * 0.7F;                                               // C
        channels_[2] = lv * std::clamp(1.0F + std::min(0.0F, p), 0.2F, 1.0F);  // R
        channels_[3] = lv * 0.35F;                                              // Ls
        channels_[4] = lv * 0.35F;                                              // Rs
        channels_[5] = lv * 0.45F;                                              // LFE
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.fillRect(rect(), QColor("#111317"));
        p.setPen(QColor("#232730"));
        p.drawRect(rect().adjusted(0, 0, -1, -1));

        const QPointF center(width() / 2.0, height() / 2.0);
        const double r = std::min(width(), height()) * 0.36;
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(QColor("#232730"));
        p.drawEllipse(center, r, r);

        static constexpr const char* kNames[] = {"L", "C", "R", "Ls", "Rs", "LFE"};
        static constexpr double kAngles[] = {-2.356, -1.571, -0.785, 2.356, 0.785, 1.571};
        QFont f = font();
        f.setPixelSize(10);
        p.setFont(f);
        for (int i = 0; i < 6; ++i) {
            const double dist = (i == 5) ? r * 0.35 : r;
            const QPointF pt(center.x() + dist * std::cos(kAngles[i]),
                             center.y() + dist * std::sin(kAngles[i]));
            const float lv = channels_[static_cast<std::size_t>(i)];
            p.setBrush(lv > 0.05F ? QColor("#39ad77") : QColor("#252932"));
            p.setPen(QColor("#3c424f"));
            p.drawEllipse(pt, 14, 14);
            p.setPen(QColor("#e2e8f0"));
            p.drawText(QRectF(pt.x() - 14, pt.y() - 10, 28, 20),
                       Qt::AlignCenter, QString::fromLatin1(kNames[i]));
        }
    }

private:
    std::array<float, 6> channels_{};
};

class SurroundAnalyzerDialog final : public QDialog {
public:
    explicit SurroundAnalyzerDialog(QWidget* parent = nullptr) : QDialog(parent) {
        setObjectName("audioSurroundAnalyzerDialog");
        setWindowTitle(QCoreApplication::translate("AudioWorkspace", "Surround Analyzer"));
        setStyleSheet(QStringLiteral("QDialog { background: #16181c; color: #cfd6e0; }"));
        auto* layout = new QVBoxLayout(this);
        canvas_ = new SurroundCanvas(this);
        layout->addWidget(canvas_, 1);
    }

    void updateLevel(float master_peak, float pan = 0.0F) {
        if (canvas_) canvas_->setLevel(master_peak, pan);
    }

private:
    SurroundCanvas* canvas_ = nullptr;
};

}  // namespace audio_effects::surround_analyzer

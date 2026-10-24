#include "workspaces/color/ui/color_workspace.h"

#include "ui/preview/preview_widget.h"
#include "workspaces/color/evaluation/color_graph_evaluator.h"

#include <QCoreApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QFont>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QMetaObject>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPointer>
#include <QPushButton>
#include <QInputDialog>
#include <QLineF>
#include <QLineEdit>
#include <QMessageBox>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QThreadPool>
#include <QTimer>
#include <QVBoxLayout>
#include <QUuid>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <numbers>
#include <vector>

namespace ui {
namespace {

QString labelFor(std::string_view id) {
    if (id == "video.primary_grade") return QCoreApplication::translate("ColorWorkspace", "Primary Grade");
    if (id == "video.brightness") return QCoreApplication::translate("ColorWorkspace", "Brightness");
    if (id == "video.contrast") return QCoreApplication::translate("ColorWorkspace", "Contrast");
    if (id == "video.saturation") return QCoreApplication::translate("ColorWorkspace", "Saturation");
    if (id == "video.grayscale") return QCoreApplication::translate("ColorWorkspace", "Grayscale");
    return QString::fromStdString(std::string(id));
}

QString parameterLabel(std::string_view id) {
    if (id == "contrast") return QCoreApplication::translate("ColorWorkspace", "Contrast");
    if (id == "saturation") return QCoreApplication::translate("ColorWorkspace", "Saturation");
    if (id == "pivot") return QCoreApplication::translate("ColorWorkspace", "Pivot");
    if (id == "temperature") return QCoreApplication::translate("ColorWorkspace", "Temperature");
    if (id == "tint") return QCoreApplication::translate("ColorWorkspace", "Tint");
    return QCoreApplication::translate("ColorWorkspace", "Amount");
}

QImage calculateScope(const QImage& source, const QString& mode) {
    constexpr int width = 480;
    constexpr int height = 256;
    constexpr int left = 38;
    constexpr int right = 8;
    constexpr int top = 20;
    constexpr int bottom = 26;
    constexpr int plot_width = width - left - right;
    constexpr int plot_height = height - top - bottom;
    const bool parade = mode == QLatin1String("parade");
    const bool histogram = mode == QLatin1String("histogram");
    const bool vectorscope = mode == QLatin1String("vectorscope");
    if (histogram) {
        std::array<std::array<std::uint32_t, 256>, 3> bins{};
        const auto sampled = source.scaled(plot_width, plot_height, Qt::IgnoreAspectRatio,
            Qt::FastTransformation).convertToFormat(QImage::Format_RGB32);
        std::uint32_t peak = 0;
        for (int y = 0; y < sampled.height(); ++y) {
            const auto* row = reinterpret_cast<const QRgb*>(sampled.constScanLine(y));
            for (int x = 0; x < sampled.width(); ++x) {
                const std::array<int, 3> values{qRed(row[x]), qGreen(row[x]), qBlue(row[x])};
                for (int channel = 0; channel < 3; ++channel) {
                    peak = std::max(peak, ++bins[channel][values[channel]]);
                }
            }
        }
        QImage plot(width, height, QImage::Format_RGB32);
        plot.fill(QColor(18, 21, 27));
        QPainter painter(&plot);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QColor(46, 54, 65));
        for (int tick = 0; tick <= 4; ++tick) {
            const int y = top + plot_height - 1 - tick * (plot_height - 1) / 4;
            painter.drawLine(left, y, width - right - 1, y);
            painter.setPen(QColor(151, 164, 180));
            painter.drawText(2, y + 4, QStringLiteral("%1%").arg(tick * 25));
            painter.setPen(QColor(46, 54, 65));
        }
        for (int tick = 0; tick <= 4; ++tick) {
            const int x = left + tick * (plot_width - 1) / 4;
            painter.drawLine(x, top, x, top + plot_height - 1);
            painter.setPen(QColor(151, 164, 180));
            painter.drawText(x - 14, height - 8, 28, 14, Qt::AlignCenter,
                QStringLiteral("%1").arg(tick * 25));
            painter.setPen(QColor(46, 54, 65));
        }
        const std::array<QColor, 3> colors{
            QColor(255, 88, 88, 220), QColor(92, 236, 125, 220),
            QColor(102, 164, 255, 220)};
        for (int channel = 0; channel < 3; ++channel) {
            if (peak == 0) continue;
            QPainterPath path;
            for (int value = 0; value < 256; ++value) {
                const double x = left + value * (plot_width - 1) / 255.0;
                const double fraction = double(bins[channel][value]) / peak;
                const double y = top + (1.0 - fraction) * (plot_height - 1);
                if (value == 0) path.moveTo(x, y);
                else path.lineTo(x, y);
            }
            painter.setPen(QPen(colors[channel], 1.6));
            painter.drawPath(path);
            painter.setPen(colors[channel]);
            painter.drawText(width - 74 + channel * 22, top - 3, 18, 16,
                Qt::AlignCenter, QString::fromLatin1("RGB").mid(channel, 1));
        }
        painter.setPen(QColor(210, 218, 228));
        painter.drawText(left, height - 5, plot_width, 18, Qt::AlignCenter,
            QCoreApplication::translate("ColorWorkspace", "RGB Histogram"));
        return plot;
    }

    if (vectorscope) {
        constexpr int side = plot_height;
        constexpr int vector_left = left + (plot_width - side) / 2;
        constexpr int vector_top = top;
        constexpr int sample_width = plot_width;
        constexpr int sample_height = plot_height;
        std::vector<std::uint32_t> bins(static_cast<std::size_t>(side * side));
        const auto sampled = source.scaled(sample_width, sample_height,
            Qt::IgnoreAspectRatio, Qt::FastTransformation)
            .convertToFormat(QImage::Format_RGB32);
        std::uint32_t peak = 0;
        for (int y = 0; y < sampled.height(); ++y) {
            const auto* row = reinterpret_cast<const QRgb*>(sampled.constScanLine(y));
            for (int x = 0; x < sampled.width(); ++x) {
                const double red = qRed(row[x]) / 255.0;
                const double green = qGreen(row[x]) / 255.0;
                const double blue = qBlue(row[x]) / 255.0;
                const double luma = .2126 * red + .7152 * green + .0722 * blue;
                const double cb = (blue - luma) / 1.8556;
                const double cr = (red - luma) / 1.5748;
                const int px = std::clamp(static_cast<int>(std::lround(
                    (side - 1) * (.5 + cb))), 0, side - 1);
                const int py = std::clamp(static_cast<int>(std::lround(
                    (side - 1) * (.5 - cr))), 0, side - 1);
                const auto count = ++bins[static_cast<std::size_t>(py * side + px)];
                peak = std::max(peak, count);
            }
        }
        QImage plot(width, height, QImage::Format_RGB32);
        plot.fill(QColor(18, 21, 27));
        if (peak > 0) {
            for (int y = 0; y < side; ++y) {
                auto* row = reinterpret_cast<QRgb*>(plot.scanLine(vector_top + y));
                for (int x = 0; x < side; ++x) {
                    const auto count = bins[static_cast<std::size_t>(y * side + x)];
                    if (count == 0) continue;
                    const int intensity = static_cast<int>(std::lround(
                        220.0 * std::sqrt(double(count) / peak)));
                    row[vector_left + x] = qRgb(24 + intensity / 4,
                        54 + intensity * 3 / 5, 90 + intensity * 3 / 4);
                }
            }
        }
        QPainter painter(&plot);
        painter.setRenderHint(QPainter::Antialiasing);
        const double center = vector_left + (side - 1) / 2.0;
        const double center_y = vector_top + (side - 1) / 2.0;
        painter.setPen(QColor(46, 54, 65));
        painter.drawRect(vector_left, vector_top, side - 1, side - 1);
        painter.drawEllipse(QPointF(center, center_y), side * .25, side * .25);
        painter.drawLine(QPointF(center, vector_top), QPointF(center, vector_top + side - 1));
        painter.drawLine(QPointF(vector_left, center_y), QPointF(vector_left + side - 1, center_y));
        painter.setPen(QColor(151, 164, 180));
        painter.drawText(vector_left, height - 8, side, 14, Qt::AlignCenter,
            QStringLiteral("Cb  −50%        0        +50%"));
        painter.save();
        painter.translate(16, vector_top + side / 2.0);
        painter.rotate(-90);
        painter.drawText(-side / 2, 0, side, 16, Qt::AlignCenter,
            QStringLiteral("Cr  −50%        0        +50%"));
        painter.restore();
        struct Target {
            const char* label;
            QColor color;
            double red;
            double green;
            double blue;
        };
        const std::array<Target, 6> targets{{
            {"R", QColor(255, 70, 70), 1, 0, 0},
            {"Y", QColor(255, 230, 70), 1, 1, 0},
            {"G", QColor(70, 240, 90), 0, 1, 0},
            {"C", QColor(70, 230, 240), 0, 1, 1},
            {"B", QColor(80, 130, 255), 0, 0, 1},
            {"M", QColor(240, 90, 240), 1, 0, 1}}};
        painter.setFont(QFont(painter.font().family(), 8, QFont::DemiBold));
        for (const auto& target : targets) {
            const double luma = .2126 * target.red + .7152 * target.green +
                .0722 * target.blue;
            const double cb = (target.blue - luma) / 1.8556;
            const double cr = (target.red - luma) / 1.5748;
            const QPointF point(vector_left + (side - 1) * (.5 + cb),
                vector_top + (side - 1) * (.5 - cr));
            painter.setPen(target.color);
            painter.setBrush(QColor(18, 21, 27));
            painter.drawEllipse(point, 5, 5);
            painter.drawText(QRectF(point.x() - 5, point.y() - 5, 10, 10),
                Qt::AlignCenter, QString::fromLatin1(target.label));
        }
        painter.setPen(QColor(210, 218, 228));
        painter.drawText(left, height - 5, plot_width, 18, Qt::AlignCenter,
            QCoreApplication::translate("ColorWorkspace", "Vectorscope"));
        return plot;
    }

    std::array<std::vector<std::uint32_t>, 3> bins;
    for (auto& channel : bins)
        channel.resize(static_cast<std::size_t>(plot_width * plot_height));
    const int sample_width = parade ? plot_width / 3 : plot_width;
    const auto sampled = source.scaled(sample_width, plot_height, Qt::IgnoreAspectRatio,
                                       Qt::FastTransformation).convertToFormat(QImage::Format_RGB32);
    for (int y = 0; y < sampled.height(); ++y) {
        const auto* row = reinterpret_cast<const QRgb*>(sampled.constScanLine(y));
        for (int x = 0; x < sampled.width(); ++x) {
            const std::array<int, 3> values{qRed(row[x]), qGreen(row[x]), qBlue(row[x])};
            for (int channel = 0; channel < 3; ++channel) {
                const int channel_x = parade ? channel * sample_width + x : x;
                const int scope_y = plot_height - 1 - values[channel] * (plot_height - 1) / 255;
                ++bins[channel][scope_y * plot_width + channel_x];
            }
        }
    }
    QImage plot(width, height, QImage::Format_RGB32);
    plot.fill(QColor(18, 21, 27));
    QPainter painter(&plot);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(QColor(46, 54, 65));
    for (int tick = 0; tick <= 4; ++tick) {
        const int y = top + plot_height - 1 - tick * (plot_height - 1) / 4;
        painter.drawLine(left, y, width - right - 1, y);
        painter.setPen(QColor(151, 164, 180));
        painter.drawText(2, y + 4, QStringLiteral("%1%").arg(tick * 25));
        painter.setPen(QColor(46, 54, 65));
    }
    if (parade) {
        for (int divider = 1; divider < 3; ++divider) {
            const int x = left + divider * (plot_width / 3);
            painter.drawLine(x, top, x, top + plot_height - 1);
        }
    }
    for (int y = 0; y < plot_height; ++y) {
        auto* row = reinterpret_cast<QRgb*>(plot.scanLine(top + y));
        for (int x = 0; x < plot_width; ++x) {
            const auto index = y * plot_width + x;
            row[left + x] = qRgb(std::min(255, 18 + int(bins[0][index]) * 38),
                std::min(255, 21 + int(bins[1][index]) * 38),
                std::min(255, 27 + int(bins[2][index]) * 38));
        }
    }
    painter.setPen(QColor(210, 218, 228));
    if (parade) {
        const std::array<QString, 3> labels{QStringLiteral("R"), QStringLiteral("G"), QStringLiteral("B")};
        for (int channel = 0; channel < 3; ++channel)
            painter.drawText(left + channel * (plot_width / 3), height - 5,
                plot_width / 3, 18, Qt::AlignCenter, labels[channel]);
    } else {
        painter.drawText(left, height - 5, plot_width, 18, Qt::AlignCenter,
            QCoreApplication::translate("ColorWorkspace", "RGB waveform"));
    }
    return plot;
}

class RgbWheelDisk final : public QWidget {
public:
    explicit RgbWheelDisk(QWidget* parent = nullptr) : QWidget(parent) {
        setMinimumSize(96, 96);
        setFixedSize(108, 108);
        setFocusPolicy(Qt::StrongFocus);
    }
    void configure(double minimum, double maximum) { minimum_ = minimum; maximum_ = maximum; update(); }
    void setValues(std::array<double, 3> values) { values_ = values; update(); }
    void setHandlers(std::function<void()> started, std::function<void()> finished,
        std::function<void(std::array<double, 3>)> changed) {
        started_ = std::move(started); finished_ = std::move(finished); changed_ = std::move(changed);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const double diameter = std::min(width(), height()) - 8.0;
        const QRectF bounds((width() - diameter) / 2, (height() - diameter) / 2,
            diameter, diameter);
        static const QImage wheel = [] {
            QImage image(160, 160, QImage::Format_ARGB32_Premultiplied);
            for (int y = 0; y < image.height(); ++y) {
                auto* row = reinterpret_cast<QRgb*>(image.scanLine(y));
                for (int x = 0; x < image.width(); ++x) {
                    const double dx = (x + .5 - image.width() / 2.0) / (image.width() / 2.0);
                    const double dy = (y + .5 - image.height() / 2.0) / (image.height() / 2.0);
                    const double radius = std::sqrt(dx * dx + dy * dy);
                    if (radius > 1.0) { row[x] = qRgba(0, 0, 0, 0); continue; }
                    const double hue = std::atan2(dy, dx) / (2.0 * std::numbers::pi) + 1.0;
                    const QColor color = QColor::fromHsvF(std::fmod(hue, 1.0), radius, 1.0);
                    row[x] = color.rgba();
                }
            }
            return image;
        }();
        painter.drawImage(bounds, wheel);
        painter.setPen(QPen(QColor(191, 204, 219), 1.4));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(bounds);
        const double mean = (values_[0] + values_[1] + values_[2]) / 3.0;
        const double amplitude = std::max(.001, (maximum_ - minimum_) / 2.0);
        const double dx = values_[0] - mean;
        const double dy = (values_[1] - values_[2]) / std::sqrt(3.0);
        const double nx = std::clamp(dx / amplitude, -1.0, 1.0);
        const double ny = std::clamp(dy / amplitude, -1.0, 1.0);
        const double length = std::hypot(nx, ny);
        const double scale = length > 1.0 ? 1.0 / length : 1.0;
        const QPointF marker(bounds.center().x() + nx * scale * diameter * .46,
            bounds.center().y() - ny * scale * diameter * .46);
        painter.setPen(QPen(QColor(12, 16, 22), 3));
        painter.setBrush(QColor(242, 246, 250));
        painter.drawEllipse(marker, 5, 5);
    }
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() != Qt::LeftButton) return;
        dragging_ = true;
        if (started_) started_();
        updateFromPoint(event->position());
    }
    void mouseMoveEvent(QMouseEvent* event) override {
        if (dragging_) updateFromPoint(event->position());
    }
    void mouseReleaseEvent(QMouseEvent* event) override {
        if (!dragging_ || event->button() != Qt::LeftButton) return;
        updateFromPoint(event->position());
        dragging_ = false;
        if (finished_) finished_();
    }

private:
    void updateFromPoint(QPointF point) {
        const double diameter = std::min(width(), height()) - 8.0;
        const double radius = diameter * .46;
        double x = (point.x() - width() / 2.0) / radius;
        double y = (height() / 2.0 - point.y()) / radius;
        const double magnitude = std::hypot(x, y);
        if (magnitude > 1.0) { x /= magnitude; y /= magnitude; }
        const double angle = std::atan2(y, x);
        const double mean = (values_[0] + values_[1] + values_[2]) / 3.0;
        const double amplitude = (maximum_ - minimum_) / 2.0 * std::hypot(x, y);
        constexpr double two_pi_over_three = 2.0 * std::numbers::pi / 3.0;
        std::array<double, 3> values{
            mean + amplitude * std::cos(angle),
            mean + amplitude * std::cos(angle - two_pi_over_three),
            mean + amplitude * std::cos(angle + two_pi_over_three)};
        for (auto& value : values) value = std::clamp(value, minimum_, maximum_);
        if (values != values_ && changed_) changed_(values);
    }
    std::array<double, 3> values_{};
    double minimum_ = -100.0;
    double maximum_ = 100.0;
    bool dragging_ = false;
    std::function<void()> started_;
    std::function<void()> finished_;
    std::function<void(std::array<double, 3>)> changed_;
};

class PrimaryGradeWheel final : public QWidget {
public:
    PrimaryGradeWheel(QString key, double minimum, double maximum, double neutral,
        QWidget* parent = nullptr) : QWidget(parent), minimum_(minimum), maximum_(maximum) {
        setObjectName(QStringLiteral("colorWheel_%1").arg(key));
        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(5);
        disk_ = new RgbWheelDisk(this);
        disk_->setObjectName(QStringLiteral("colorWheel_%1_disk").arg(key));
        disk_->configure(minimum, maximum);
        layout->addWidget(disk_);
        master_ = new QSlider(Qt::Vertical, this);
        master_->setObjectName(QStringLiteral("colorWheel_%1_master").arg(key));
        master_->setRange(0, 1000);
        master_->setFixedHeight(100);
        master_->setAccessibleName(QCoreApplication::translate("ColorWorkspace", "Master value"));
        layout->addWidget(master_);
        auto* channels = new QGridLayout;
        channels->setContentsMargins(0, 0, 0, 0);
        channels->setHorizontalSpacing(4);
        channels->setVerticalSpacing(3);
        const std::array<QString, 3> channel_names{QStringLiteral("R"), QStringLiteral("G"), QStringLiteral("B")};
        for (int channel = 0; channel < 3; ++channel) {
            channels->addWidget(new QLabel(channel_names[channel], this), channel, 0);
            fields_[channel] = new QDoubleSpinBox(this);
            fields_[channel]->setObjectName(QStringLiteral("colorWheel_%1_%2_value").arg(key, channel_names[channel].toLower()));
            fields_[channel]->setRange(minimum_, maximum_);
            fields_[channel]->setDecimals(0);
            fields_[channel]->setFixedWidth(76);
            fields_[channel]->setValue(neutral);
            channels->addWidget(fields_[channel], channel, 1);
            connect(fields_[channel], &QDoubleSpinBox::editingFinished, this,
                [this] { editValuesFromFields(); });
        }
        layout->addLayout(channels, 1);
        setMinimumHeight(112);
        setMaximumHeight(120);
        disk_->setHandlers([this] { if (started_) started_(); },
            [this] { if (finished_) finished_(); },
            [this](std::array<double, 3> values) { setValues(values); if (changed_) changed_(rgb_values_); });
        connect(master_, &QSlider::sliderPressed, this, [this] { if (started_) started_(); });
        connect(master_, &QSlider::sliderReleased, this, [this] { if (finished_) finished_(); });
        connect(master_, &QSlider::valueChanged, this, [this](int value) {
            const double target = minimum_ + (maximum_ - minimum_) * value / 1000.0;
            const double mean = (rgb_values_[0] + rgb_values_[1] + rgb_values_[2]) / 3.0;
            const double delta = target - mean;
            auto updated = rgb_values_;
            for (auto& channel : updated) channel = std::clamp(channel + delta, minimum_, maximum_);
            setValues(updated);
            if (changed_) changed_(rgb_values_);
        });
        setValues({neutral, neutral, neutral});
    }
    void setHandlers(std::function<void()> started, std::function<void()> finished,
        std::function<void(std::array<double, 3>)> changed) {
        started_ = std::move(started); finished_ = std::move(finished); changed_ = std::move(changed);
    }
    void setValues(std::array<double, 3> values) {
        rgb_values_ = values;
        for (int channel = 0; channel < 3; ++channel) {
            const QSignalBlocker blocker(fields_[channel]);
            fields_[channel]->setValue(values[channel]);
        }
        const double mean = (values[0] + values[1] + values[2]) / 3.0;
        const QSignalBlocker blocker(master_);
        master_->setValue(static_cast<int>(std::lround((mean - minimum_) * 1000.0 /
            (maximum_ - minimum_))));
        disk_->setValues(rgb_values_);
    }

private:
    void editValuesFromFields() {
        std::array<double, 3> updated{};
        for (int channel = 0; channel < 3; ++channel) updated[channel] = fields_[channel]->value();
        if (updated == rgb_values_) return;
        if (started_) started_();
        setValues(updated);
        if (changed_) changed_(rgb_values_);
        if (finished_) finished_();
    }
    double minimum_ = 0.0;
    double maximum_ = 100.0;
    std::array<QDoubleSpinBox*, 3> fields_{};
    std::array<double, 3> rgb_values_{};
    RgbWheelDisk* disk_ = nullptr;
    QSlider* master_ = nullptr;
    std::function<void()> started_;
    std::function<void()> finished_;
    std::function<void(std::array<double, 3>)> changed_;
};

class PrimaryGradeBars final : public QWidget {
public:
    PrimaryGradeBars(QString key, double minimum, double maximum, double neutral,
        QWidget* parent = nullptr)
        : QWidget(parent), minimum_(minimum), maximum_(maximum) {
        setObjectName(QStringLiteral("colorBars_%1").arg(key));
        auto* layout = new QGridLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setHorizontalSpacing(5);
        layout->setVerticalSpacing(2);
        const std::array<QString, 3> channel_names{
            QStringLiteral("R"), QStringLiteral("G"), QStringLiteral("B")};
        for (int channel = 0; channel < 3; ++channel) {
            auto* label = new QLabel(channel_names[channel], this);
            label->setAlignment(Qt::AlignCenter);
            layout->addWidget(label, channel, 0);
            sliders_[channel] = new QSlider(Qt::Horizontal, this);
            sliders_[channel]->setObjectName(QStringLiteral("colorBars_%1_%2_slider")
                .arg(key, channel_names[channel].toLower()));
            sliders_[channel]->setRange(0, 1000);
            sliders_[channel]->setToolTip(channel_names[channel]);
            layout->addWidget(sliders_[channel], channel, 1);
            auto* field = new QDoubleSpinBox(this);
            fields_[channel] = field;
            field->setObjectName(QStringLiteral("colorBars_%1_%2_value")
                .arg(key, channel_names[channel].toLower()));
            field->setRange(minimum_, maximum_);
            field->setDecimals(0);
            field->setFixedWidth(68);
            field->setValue(neutral);
            field->setToolTip(channel_names[channel]);
            layout->addWidget(field, channel, 2);
            connect(field, &QDoubleSpinBox::editingFinished, this,
                [this, channel] { editValueFromField(channel); });
            connect(sliders_[channel], &QSlider::sliderPressed, this,
                [this] { if (started_) started_(); });
            connect(sliders_[channel], &QSlider::sliderReleased, this,
                [this] { if (finished_) finished_(); });
            connect(sliders_[channel], &QSlider::valueChanged, this,
                [this, channel](int value) { editChannel(channel, value); });
        }
        setMinimumHeight(72);
        setValues({neutral, neutral, neutral});
    }

    void setHandlers(std::function<void()> started, std::function<void()> finished,
        std::function<void(std::array<double, 3>)> changed) {
        started_ = std::move(started);
        finished_ = std::move(finished);
        changed_ = std::move(changed);
    }

    void setValues(std::array<double, 3> values) {
        rgb_values_ = values;
        for (int channel = 0; channel < 3; ++channel) {
            const QSignalBlocker blocker(fields_[channel]);
            fields_[channel]->setValue(values[channel]);
            const QSignalBlocker slider_blocker(sliders_[channel]);
            sliders_[channel]->setValue(static_cast<int>(std::lround(
                (values[channel] - minimum_) * 1000.0 / (maximum_ - minimum_))));
        }
    }

private:
    void editChannel(int channel, int slider_value) {
        auto updated = rgb_values_;
        updated[channel] = minimum_ + (maximum_ - minimum_) * slider_value / 1000.0;
        if (updated == rgb_values_) return;
        setValues(updated);
        if (changed_) changed_(rgb_values_);
    }

    void editValueFromField(int channel) {
        auto updated = rgb_values_;
        updated[channel] = fields_[channel]->value();
        if (updated == rgb_values_) return;
        if (started_) started_();
        setValues(updated);
        if (changed_) changed_(rgb_values_);
        if (finished_) finished_();
    }

    double minimum_ = 0.0;
    double maximum_ = 100.0;
    std::array<QSlider*, 3> sliders_{};
    std::array<QDoubleSpinBox*, 3> fields_{};
    std::array<double, 3> rgb_values_{};
    std::function<void()> started_;
    std::function<void()> finished_;
    std::function<void(std::array<double, 3>)> changed_;
};

const creative_suite::effects::CurvesPointList& curvePointsFor(
    const creative_suite::effects::VideoCurvesParameters& parameters,
    color::CurveKind kind) {
    using creative_suite::effects::CurvesPointList;
    switch (kind) {
    case color::CurveKind::Master: return parameters.rgb.points;
    case color::CurveKind::Red: return parameters.rgb.channels[0];
    case color::CurveKind::Green: return parameters.rgb.channels[1];
    case color::CurveKind::Blue: return parameters.rgb.channels[2];
    case color::CurveKind::HueVsHue: return parameters.targeted[0];
    case color::CurveKind::HueVsSaturation: return parameters.targeted[1];
    case color::CurveKind::HueVsLuminance: return parameters.targeted[2];
    case color::CurveKind::LuminanceVsSaturation: return parameters.targeted[3];
    case color::CurveKind::SaturationVsSaturation: return parameters.targeted[4];
    }
    return parameters.rgb.points;
}

class CurveEditor final : public QWidget {
public:
    explicit CurveEditor(QWidget* parent = nullptr) : QWidget(parent) {
        setObjectName(QStringLiteral("colorCurveEditor"));
        setMinimumSize(250, 190);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setFixedHeight(210);
        setMouseTracking(true);
        setToolTip(QCoreApplication::translate("ColorWorkspace",
            "Click the curve to add a point; drag points to edit; right-click a point to remove it."));
    }

    void setPoints(color::CurveKind kind,
        creative_suite::effects::CurvesPointList points) {
        kind_ = kind;
        points_ = std::move(points);
        update();
    }

    void setHandlers(std::function<void()> started, std::function<void()> finished,
        std::function<void(creative_suite::effects::CurvesPointList)> changed) {
        started_ = std::move(started);
        finished_ = std::move(finished);
        changed_ = std::move(changed);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(), QColor(20, 25, 32));
        const auto plot = plotRect();
        painter.setPen(QPen(QColor(54, 64, 76), 1.0));
        for (int tick = 0; tick <= 4; ++tick) {
            const double ratio = tick / 4.0;
            painter.drawLine(QPointF(plot.left() + ratio * plot.width(), plot.top()),
                QPointF(plot.left() + ratio * plot.width(), plot.bottom()));
            painter.drawLine(QPointF(plot.left(), plot.bottom() - ratio * plot.height()),
                QPointF(plot.right(), plot.bottom() - ratio * plot.height()));
        }
        if (isTargeted()) {
            painter.setPen(QPen(QColor(80, 91, 105), 1.0, Qt::DashLine));
            painter.drawLine(QPointF(plot.left(), plot.center().y()),
                QPointF(plot.right(), plot.center().y()));
        } else {
            painter.setPen(QPen(QColor(80, 91, 105), 1.0, Qt::DashLine));
            painter.drawLine(plot.bottomLeft(), plot.topRight());
        }
        QPainterPath curve;
        const auto lut = creative_suite::effects::curvesLut(points_);
        for (int x = 0; x < 256; ++x) {
            const auto point = QPointF(plot.left() + x * plot.width() / 255.0,
                plot.bottom() - lut[x] * plot.height() / 255.0);
            if (x == 0) curve.moveTo(point);
            else curve.lineTo(point);
        }
        QColor color(236, 241, 247);
        if (kind_ == color::CurveKind::Red) color = QColor(255, 94, 94);
        else if (kind_ == color::CurveKind::Green) color = QColor(100, 230, 130);
        else if (kind_ == color::CurveKind::Blue) color = QColor(107, 164, 255);
        painter.setPen(QPen(color, 2.0));
        painter.drawPath(curve);
        for (std::size_t i = 0; i < points_.size(); ++i) {
            painter.setPen(QPen(QColor(15, 19, 25), 1.5));
            painter.setBrush(i == active_point_ ? QColor(255, 184, 79) : color);
            painter.drawEllipse(toWidget(points_[i]), 4.5, 4.5);
        }
        painter.setPen(QColor(146, 159, 175));
        painter.drawText(QRectF(0, height() - 18, width(), 16), Qt::AlignCenter,
            QStringLiteral("Input 0–255     Output 0–255"));
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::RightButton) {
            const auto point = closestPoint(event->position());
            if (point > 0 && point + 1 < static_cast<int>(points_.size())) {
                if (started_) started_();
                points_.erase(points_.begin() + point);
                if (changed_) changed_(points_);
                update();
                if (finished_) finished_();
                event->accept();
                return;
            }
        }
        if (event->button() != Qt::LeftButton) return;
        active_point_ = closestPoint(event->position());
        if (active_point_ < 0) {
            if (points_.size() >= 16) return;
            const auto curve_point = fromWidget(event->position());
            const auto insertion = std::lower_bound(points_.begin(), points_.end(), curve_point.input,
                [](const auto& point, int input) { return point.input < input; });
            if (insertion != points_.end() && insertion->input == curve_point.input) return;
            active_point_ = static_cast<int>(insertion - points_.begin());
            points_.insert(insertion, curve_point);
            if (started_) started_();
            drag_started_ = true;
            if (changed_) changed_(points_);
            update();
            event->accept();
            return;
        }
        if (started_) started_();
        drag_started_ = true;
        movePoint(event->position());
        event->accept();
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        if (drag_started_ && active_point_ >= 0) movePoint(event->position());
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (!drag_started_ || event->button() != Qt::LeftButton) return;
        movePoint(event->position());
        drag_started_ = false;
        active_point_ = -1;
        update();
        if (finished_) finished_();
        event->accept();
    }

private:
    [[nodiscard]] bool isTargeted() const noexcept {
        return kind_ >= color::CurveKind::HueVsHue;
    }
    [[nodiscard]] QRectF plotRect() const noexcept {
        return QRectF(14.0, 10.0, std::max(1, width() - 28), std::max(1, height() - 36));
    }
    [[nodiscard]] QPointF toWidget(const creative_suite::effects::CurvesPoint& point) const {
        const auto plot = plotRect();
        return {plot.left() + point.input * plot.width() / 255.0,
            plot.bottom() - point.output * plot.height() / 255.0};
    }
    [[nodiscard]] creative_suite::effects::CurvesPoint fromWidget(QPointF point) const {
        const auto plot = plotRect();
        const int x = std::clamp(static_cast<int>(std::lround(
            (point.x() - plot.left()) * 255.0 / plot.width())), 0, 255);
        const int y = std::clamp(static_cast<int>(std::lround(
            (plot.bottom() - point.y()) * 255.0 / plot.height())), 0, 255);
        return {x, y};
    }
    [[nodiscard]] int closestPoint(QPointF position) const {
        int closest = -1;
        double distance = 11.0;
        for (std::size_t i = 0; i < points_.size(); ++i) {
            const auto d = QLineF(position, toWidget(points_[i])).length();
            if (d <= distance) { closest = static_cast<int>(i); distance = d; }
        }
        return closest;
    }
    void movePoint(QPointF position) {
        if (active_point_ < 0 || active_point_ >= static_cast<int>(points_.size())) return;
        auto next = fromWidget(position);
        if (active_point_ == 0) next.input = 0;
        else if (active_point_ + 1 == static_cast<int>(points_.size())) next.input = 255;
        else next.input = std::clamp(next.input, points_[active_point_ - 1].input + 1,
                                     points_[active_point_ + 1].input - 1);
        if (points_[active_point_] == next) return;
        points_[active_point_] = next;
        std::sort(points_.begin(), points_.end(), [](const auto& left, const auto& right) {
            return left.input < right.input;
        });
        active_point_ = static_cast<int>(std::find(points_.begin(), points_.end(), next) - points_.begin());
        if (changed_) changed_(points_);
        update();
    }
    color::CurveKind kind_ = color::CurveKind::Master;
    creative_suite::effects::CurvesPointList points_{{0, 0}, {255, 255}};
    int active_point_ = -1;
    bool drag_started_ = false;
    std::function<void()> started_;
    std::function<void()> finished_;
    std::function<void(creative_suite::effects::CurvesPointList)> changed_;
};

}  // namespace

ColorWorkspace::ColorWorkspace(QWidget* parent) : QWidget(parent) {
    setObjectName("colorWorkspacePage");
    setStyleSheet(QStringLiteral(R"(
        QWidget#colorWorkspacePage { background: #1c2028; color: #d7e0ea; }
        QWidget#colorNodesPanel, QWidget#colorPrimaryControls {
            background: #252b35; border: 1px solid #39424e; border-radius: 5px;
        }
        QWidget#colorWorkspacePage QLabel { color: #d7e0ea; }
        QLabel#colorWaveform {
            background: #141a22; border: 1px solid #39424e; border-radius: 5px;
        }
        QWidget#colorWorkspacePage QListWidget {
            background: #202630; color: #d7e0ea; border: 1px solid #39424e;
            padding: 3px; outline: 0;
        }
        QWidget#colorWorkspacePage QPushButton,
        QWidget#colorWorkspacePage QComboBox,
        QWidget#colorWorkspacePage QDoubleSpinBox {
            background: #303946; color: #e2e8f0; border: 1px solid #526070;
            border-radius: 4px; padding: 5px 7px;
        }
        QWidget#colorWorkspacePage QPushButton:hover { background: #39516a; }
    )"));
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(8);
    auto* upper = new QHBoxLayout;
    preview_container_ = new QWidget(this);
    preview_container_->setObjectName("colorViewerContainer");
    auto* preview_layout = new QVBoxLayout(preview_container_);
    preview_layout->setContentsMargins(0, 0, 0, 0);
    upper->addWidget(preview_container_, 3);
    auto* node_panel = new QWidget(this);
    node_panel->setObjectName("colorNodesPanel");
    auto* node_layout = new QVBoxLayout(node_panel);
    node_layout->addWidget(new QLabel(QCoreApplication::translate("ColorWorkspace", "Color nodes"), node_panel));
    nodes_ = new ColorNodeCanvas(node_panel);
    node_layout->addWidget(nodes_, 1);
    auto* node_action_controls = new QWidget(node_panel);
    node_action_controls->setObjectName("colorNodeActionControls");
    auto* node_action_layout = new QVBoxLayout(node_action_controls);
    node_action_layout->setContentsMargins(0, 0, 0, 0);
    node_kind_ = new QComboBox(node_action_controls);
    node_kind_->setObjectName("colorNewNodeType");
    for (const auto& effect : creative_suite::effects::builtInEffects()) {
        node_kind_->addItem(labelFor(effect.id), QString::fromUtf8(
            effect.id.data(), static_cast<qsizetype>(effect.id.size())));
    }
    const auto primary_grade = node_kind_->findData(QStringLiteral("video.primary_grade"));
    if (primary_grade >= 0) node_kind_->setCurrentIndex(primary_grade);
    node_action_layout->addWidget(node_kind_);
    auto* node_buttons = new QHBoxLayout;
    auto make_button = [node_action_controls, node_buttons](const QString& label, const char* object_name) {
        auto* button = new QPushButton(label, node_action_controls);
        button->setObjectName(QString::fromLatin1(object_name));
        node_buttons->addWidget(button);
        return button;
    };
    auto* add = make_button(QCoreApplication::translate("ColorWorkspace", "Add Effect"), "colorAddNodeButton");
    add_branch_button_ = make_button(QCoreApplication::translate("ColorWorkspace", "Add Branch"), "colorAddParallelBranchButton");
    auto* toggle = make_button(QCoreApplication::translate("ColorWorkspace", "On/Off"), "colorToggleNodeButton");
    auto* remove = make_button(QCoreApplication::translate("ColorWorkspace", "Remove"), "colorRemoveNodeButton");
    node_action_layout->addLayout(node_buttons);
    node_action_controls->hide();
    node_layout->addWidget(node_action_controls);
    upper->addWidget(node_panel, 2);
    root->addLayout(upper, 3);

    auto* transport = new QHBoxLayout;
    transport->addStretch();
    auto* previous = new QPushButton(QCoreApplication::translate("ColorWorkspace", "Previous frame"), this);
    auto* play_pause = new QPushButton(QCoreApplication::translate("ColorWorkspace", "Play / Pause"), this);
    auto* next = new QPushButton(QCoreApplication::translate("ColorWorkspace", "Next frame"), this);
    previous->setObjectName("colorPreviousFrameButton");
    play_pause->setObjectName("colorPlayPauseButton");
    next->setObjectName("colorNextFrameButton");
    transport->addWidget(previous);
    transport->addWidget(play_pause);
    transport->addWidget(next);
    transport->addStretch();
    root->addLayout(transport);
    connect(previous, &QPushButton::clicked, this, [this] {
        if (playback_handler_) playback_handler_(playback::PlaybackCommand::StepBackward);
    });
    connect(play_pause, &QPushButton::clicked, this, [this] {
        if (playback_handler_) playback_handler_(playback::PlaybackCommand::Play);
    });
    connect(next, &QPushButton::clicked, this, [this] {
        if (playback_handler_) playback_handler_(playback::PlaybackCommand::StepForward);
    });

    clips_ = new QListWidget(this);
    clips_->setObjectName("colorClipStrip");
    clips_->setViewMode(QListView::IconMode);
    clips_->setFlow(QListView::LeftToRight);
    clips_->setWrapping(false);
    clips_->setMovement(QListView::Static);
    clips_->setIconSize(QSize(112, 63));
    clips_->setFixedHeight(110);
    root->addWidget(clips_);

    auto* gallery_panel = new QWidget(this);
    gallery_panel->setObjectName("colorGalleryPanel");
    auto* gallery_layout = new QVBoxLayout(gallery_panel);
    gallery_layout->setContentsMargins(0, 0, 0, 0);
    auto* gallery_header = new QHBoxLayout;
    gallery_header->addWidget(new QLabel(QCoreApplication::translate("ColorWorkspace", "Gallery"), gallery_panel));
    gallery_header->addStretch();
    capture_still_button_ = new QPushButton(QCoreApplication::translate("ColorWorkspace", "Capture still"), gallery_panel);
    capture_still_button_->setObjectName("colorCaptureStillButton");
    auto* rename_still = new QPushButton(QCoreApplication::translate("ColorWorkspace", "Rename"), gallery_panel);
    rename_still->setObjectName("colorRenameStillButton");
    auto* remove_still = new QPushButton(QCoreApplication::translate("ColorWorkspace", "Remove still"), gallery_panel);
    remove_still->setObjectName("colorRemoveStillButton");
    gallery_header->addWidget(capture_still_button_);
    gallery_header->addWidget(rename_still);
    gallery_header->addWidget(remove_still);
    gallery_layout->addLayout(gallery_header);
    stills_ = new QListWidget(gallery_panel);
    stills_->setObjectName("colorGalleryStills");
    stills_->setViewMode(QListView::IconMode);
    stills_->setFlow(QListView::LeftToRight);
    stills_->setWrapping(false);
    stills_->setMovement(QListView::Static);
    stills_->setIconSize(QSize(112, 63));
    stills_->setFixedHeight(106);
    gallery_layout->addWidget(stills_);
    auto* gallery_versions_row = new QHBoxLayout;
    gallery_versions_row->addWidget(gallery_panel, 3);
    grade_versions_panel_ = new QWidget(this);
    grade_versions_panel_->setObjectName("colorGradeVersionsPanel");
    auto* grade_versions_layout = new QVBoxLayout(grade_versions_panel_);
    grade_versions_layout->setContentsMargins(0, 0, 0, 0);
    auto* grade_versions_header = new QHBoxLayout;
    grade_versions_header->addWidget(new QLabel(
        QCoreApplication::translate("ColorWorkspace", "Grade Versions"),
        grade_versions_panel_));
    grade_versions_header->addStretch();
    save_grade_version_button_ = new QPushButton(
        QCoreApplication::translate("ColorWorkspace", "Save Version"),
        grade_versions_panel_);
    save_grade_version_button_->setObjectName("colorSaveGradeVersionButton");
    grade_versions_header->addWidget(save_grade_version_button_);
    grade_versions_layout->addLayout(grade_versions_header);
    grade_versions_ = new QListWidget(grade_versions_panel_);
    grade_versions_->setObjectName("colorGradeVersions");
    grade_versions_->setFixedHeight(74);
    grade_versions_layout->addWidget(grade_versions_);
    auto* grade_version_actions = new QHBoxLayout;
    apply_grade_version_button_ = new QPushButton(
        QCoreApplication::translate("ColorWorkspace", "Apply Version"),
        grade_versions_panel_);
    apply_grade_version_button_->setObjectName("colorApplyGradeVersionButton");
    rename_grade_version_button_ = new QPushButton(
        QCoreApplication::translate("ColorWorkspace", "Rename Version"),
        grade_versions_panel_);
    rename_grade_version_button_->setObjectName("colorRenameGradeVersionButton");
    remove_grade_version_button_ = new QPushButton(
        QCoreApplication::translate("ColorWorkspace", "Remove Version"),
        grade_versions_panel_);
    remove_grade_version_button_->setObjectName("colorRemoveGradeVersionButton");
    grade_version_actions->addWidget(apply_grade_version_button_);
    grade_version_actions->addWidget(rename_grade_version_button_);
    grade_version_actions->addWidget(remove_grade_version_button_);
    grade_versions_layout->addLayout(grade_version_actions);
    gallery_versions_row->addWidget(grade_versions_panel_, 2);
    root->addLayout(gallery_versions_row);
    connect(save_grade_version_button_, &QPushButton::clicked,
        this, [this] { saveGradeVersion(); });
    connect(apply_grade_version_button_, &QPushButton::clicked,
        this, [this] { applySelectedGradeVersion(); });
    connect(rename_grade_version_button_, &QPushButton::clicked,
        this, [this] { renameSelectedGradeVersion(); });
    connect(remove_grade_version_button_, &QPushButton::clicked,
        this, [this] { removeSelectedGradeVersion(); });
    connect(grade_versions_, &QListWidget::currentRowChanged, this, [this](int row) {
        const bool selected = row >= 0;
        apply_grade_version_button_->setEnabled(selected);
        rename_grade_version_button_->setEnabled(selected);
        remove_grade_version_button_->setEnabled(selected);
    });
    connect(capture_still_button_, &QPushButton::clicked, this, [this] {
        if (capture_still_handler_) capture_still_handler_();
    });
    connect(rename_still, &QPushButton::clicked, this, [this] { renameSelectedStill(); });
    connect(remove_still, &QPushButton::clicked, this, [this] {
        const auto* item = stills_->currentItem();
        if (item && remove_still_handler_)
            remove_still_handler_(item->data(Qt::UserRole).toString());
    });

    auto* lower = new QHBoxLayout;
    controls_ = new QWidget(this);
    controls_->setObjectName("colorPrimaryControls");
    controls_->setMaximumWidth(480);
    auto* controls_outer = new QVBoxLayout(controls_);
    controls_outer->setContentsMargins(6, 6, 6, 6);
    controls_outer->setSpacing(6);
    primary_grade_mode_ = new QComboBox(controls_);
    primary_grade_mode_->setObjectName("colorPrimaryGradeMode");
    primary_grade_mode_->addItem(
        QCoreApplication::translate("ColorWorkspace", "Wheels"), QStringLiteral("wheels"));
    primary_grade_mode_->addItem(
        QCoreApplication::translate("ColorWorkspace", "Bars"), QStringLiteral("bars"));
    primary_grade_mode_->addItem(
        QCoreApplication::translate("ColorWorkspace", "Log"), QStringLiteral("log"));
    primary_grade_mode_->hide();
    controls_outer->addWidget(primary_grade_mode_);
    auto* controls_body = new QWidget(controls_);
    controls_layout_ = new QFormLayout(controls_body);
    controls_layout_->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
    controls_outer->addWidget(controls_body, 1);
    auto* controls_scroll = new QScrollArea(this);
    controls_scroll->setObjectName("colorPrimaryControlsScroll");
    controls_scroll->setFrameShape(QFrame::NoFrame);
    controls_scroll->setWidgetResizable(true);
    controls_scroll->setWidget(controls_);
    lower->addWidget(controls_scroll, 2);
    auto* scope_panel = new QWidget(this);
    auto* scope_layout = new QVBoxLayout(scope_panel);
    scope_layout->setContentsMargins(0, 0, 0, 0);
    scope_mode_ = new QComboBox(scope_panel);
    scope_mode_->setObjectName("colorScopeMode");
    scope_mode_->addItem(QCoreApplication::translate("ColorWorkspace", "RGB waveform"), QStringLiteral("waveform"));
    scope_mode_->addItem(QCoreApplication::translate("ColorWorkspace", "RGB Parade"), QStringLiteral("parade"));
    scope_mode_->addItem(QCoreApplication::translate("ColorWorkspace", "Vectorscope"), QStringLiteral("vectorscope"));
    scope_mode_->addItem(QCoreApplication::translate("ColorWorkspace", "RGB Histogram"), QStringLiteral("histogram"));
    scope_layout->addWidget(scope_mode_);
    waveform_ = new QLabel(QCoreApplication::translate("ColorWorkspace", "No frame"), scope_panel);
    waveform_->setObjectName("colorWaveform");
    waveform_->setMinimumSize(320, 180);
    waveform_->setAlignment(Qt::AlignCenter);
    waveform_->setScaledContents(true);
    scope_layout->addWidget(waveform_, 1);
    lower->addWidget(scope_panel, 1);
    root->addLayout(lower, 2);

    connect(clips_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row < 0 || !clip_selected_handler_) return;
        clip_selected_handler_(clips_->item(row)->data(Qt::UserRole).toULongLong());
    });
    clips_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(clips_, &QListWidget::customContextMenuRequested, this,
        [this](const QPoint& position) {
            auto* item = clips_->itemAt(position);
            if (item == nullptr) return;
            const auto clip_id = item->data(Qt::UserRole).toULongLong();
            if (clips_->currentItem() != item && clip_selected_handler_)
                clip_selected_handler_(clip_id);
            QMenu menu(clips_);
            auto* copy_grade = menu.addAction(
                QCoreApplication::translate("ColorWorkspace", "Copy Grade"));
            auto* apply_grade = menu.addAction(
                QCoreApplication::translate("ColorWorkspace", "Apply Grade"));
            apply_grade->setEnabled(can_apply_copied_grade_);
            connect(copy_grade, &QAction::triggered, this, [this, clip_id] {
                if (copy_grade_handler_) copy_grade_handler_(clip_id);
            });
            connect(apply_grade, &QAction::triggered, this, [this, clip_id] {
                if (apply_grade_handler_) apply_grade_handler_(clip_id);
            });
            menu.exec(clips_->viewport()->mapToGlobal(position));
        });
    connect(primary_grade_mode_, qOverload<int>(&QComboBox::currentIndexChanged),
        this, [this] { refreshControls(); });
    nodes_->setGraphChangedHandler([this](color::Graph graph) {
        submit(std::move(graph), nodes_->selectedNode());
    });
    nodes_->setSelectionChangedHandler([this](color::NodeId id) {
        selected_node_ = id;
        active_color_input_request_id_ = 0;
        latest_curves_input_ = {};
        refreshControls();
        if (active_ && preview_) {
            if (auto* viewer = qobject_cast<PreviewWidget*>(preview_))
                requestCurvesInputFrame(viewer->currentPayload());
        }
    });
    connect(add, &QPushButton::clicked, this, [this] {
        if (!selected_clip_ || !selected_node_) return;
        auto graph = graph_;
        const auto node_id = graph.next_id;
        const auto effect = creative_suite::effects::makeDefaultInstance(
            node_kind_->currentData().toString().toStdString());
        if (color::insertEffectAfter(graph, *selected_node_, effect))
            submit(std::move(graph), node_id);
    });
    connect(add_branch_button_, &QPushButton::clicked, this, [this] {
        if (!selected_clip_ || !selected_node_) return;
        auto graph = graph_;
        const auto node_id = graph.next_id;
        const auto effect = creative_suite::effects::makeDefaultInstance(
            node_kind_->currentData().toString().toStdString());
        if (color::addParallelBranch(graph, *selected_node_, effect))
            submit(std::move(graph), node_id);
    });
    connect(toggle, &QPushButton::clicked, this, [this] {
        if (!selected_node_) return;
        auto graph = graph_;
        const auto* node = color::findNode(graph, *selected_node_);
        if (!node || !node->effect) return;
        const auto node_id = node->id;
        if (color::setEffectEnabled(graph, node_id, !node->effect->enabled))
            submit(std::move(graph), node_id);
    });
    connect(remove, &QPushButton::clicked, this, [this] {
        if (!selected_node_) return;
        auto graph = graph_;
        const auto removed = *selected_node_;
        const auto* node = color::findNode(graph, removed);
        const bool removed_node = node && (node->type == color::NodeType::Effect
            ? color::removeEffect(graph, removed)
            : node->type == color::NodeType::ParallelMixer &&
                color::removeParallelMixer(graph, removed));
        if (!removed_node) return;
        const auto input = std::find_if(graph.nodes.begin(), graph.nodes.end(),
            [](const color::Node& node) { return node.type == color::NodeType::Input; });
        const auto next_selection = input == graph.nodes.end()
            ? std::optional<color::NodeId>{}
            : std::optional<color::NodeId>{input->id};
        submit(std::move(graph), next_selection);
    });
    waveform_timer_ = new QTimer(this);
    waveform_timer_->setInterval(100);
    connect(waveform_timer_, &QTimer::timeout, this, [this] { scheduleWaveform(); });
    connect(scope_mode_, &QComboBox::currentTextChanged, this, [this] {
        ++waveform_generation_;
        waveform_pending_ = false;
        active_scope_request_id_ = 0;
        waveform_->setPixmap({});
        waveform_->setText(QCoreApplication::translate("ColorWorkspace", "No frame"));
        latest_scope_source_ = {};
        refreshMattePreview();
        if (active_) scheduleWaveform();
    });
    refreshNodes();
}

void ColorWorkspace::setPreviewWidget(QWidget* preview) {
    if (preview_ == preview) return;
    preview_ = preview;
    if (preview_) preview_container_->layout()->addWidget(preview_);
}

QWidget* ColorWorkspace::takePreviewWidget() {
    auto* result = preview_;
    if (result) preview_container_->layout()->removeWidget(result);
    preview_ = nullptr;
    return result;
}

void ColorWorkspace::setActive(bool active) {
    active_ = active;
    ++waveform_generation_;
    if (active) waveform_timer_->start();
    else {
        waveform_timer_->stop();
        waveform_pending_ = false;
        active_scope_request_id_ = 0;
        active_color_input_request_id_ = 0;
        waveform_->setPixmap({});
        waveform_->setText(QCoreApplication::translate("ColorWorkspace", "No frame"));
        latest_scope_source_ = {};
        latest_curves_input_ = {};
        refreshMattePreview();
    }
}

void ColorWorkspace::setClips(std::vector<ClipItem> clips, timeline::ClipId selected,
    color::Graph graph) {
    if (selected_clip_ != selected || graph_ != graph) {
        latest_scope_source_ = {};
        latest_curves_input_ = {};
        active_color_input_request_id_ = 0;
        refreshMattePreview();
    }
    const auto old_selected = selected_node_;
    const auto old_version_id = selected_clip_ == selected && grade_versions_->currentItem()
        ? grade_versions_->currentItem()->data(Qt::UserRole).toString() : QString{};
    selected_clip_ = selected;
    graph_ = color::validate(graph) ? std::move(graph) : color::makePassthroughGraph();
    selected_grade_versions_.clear();
    {
        const QSignalBlocker blocker(clips_);
        clips_->clear();
        int selected_row = -1;
        for (const auto& clip : clips) {
            auto* item = new QListWidgetItem(clip.name, clips_);
            if (!clip.thumbnail.isNull()) item->setIcon(QIcon(QPixmap::fromImage(clip.thumbnail)));
            item->setSizeHint(QSize(128, 96));
            item->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(clip.id));
            if (clip.id == selected) {
                selected_row = clips_->count() - 1;
                selected_grade_versions_ = clip.grade_versions;
            }
        }
        clips_->setCurrentRow(selected_row);
    }
    {
        const QSignalBlocker blocker(grade_versions_);
        grade_versions_->clear();
        int selected_row = -1;
        for (const auto& version : selected_grade_versions_) {
            auto* item = new QListWidgetItem(
                QString::fromUtf8(version.name.data(),
                    static_cast<qsizetype>(version.name.size())), grade_versions_);
            item->setData(Qt::UserRole, QString::fromUtf8(version.id.data(),
                static_cast<qsizetype>(version.id.size())));
            if (item->data(Qt::UserRole).toString() == old_version_id)
                selected_row = grade_versions_->count() - 1;
        }
        if (selected_row >= 0) grade_versions_->setCurrentRow(selected_row);
    }
    save_grade_version_button_->setEnabled(selected_clip_ != 0 && color::validate(graph_));
    const bool has_version = grade_versions_->currentRow() >= 0;
    apply_grade_version_button_->setEnabled(has_version);
    rename_grade_version_button_->setEnabled(has_version);
    remove_grade_version_button_->setEnabled(has_version);
    refreshNodes(old_selected);
}

void ColorWorkspace::setClipSelectedHandler(std::function<void(timeline::ClipId)> handler) {
    clip_selected_handler_ = std::move(handler);
}

void ColorWorkspace::setEffectsChangedHandler(std::function<void(timeline::ClipId,
    color::Graph)> handler) {
    effects_changed_handler_ = std::move(handler);
}

void ColorWorkspace::setCopyGradeHandler(std::function<void(timeline::ClipId)> handler) {
    copy_grade_handler_ = std::move(handler);
}

void ColorWorkspace::setApplyGradeHandler(std::function<void(timeline::ClipId)> handler) {
    apply_grade_handler_ = std::move(handler);
}

void ColorWorkspace::setGradeVersionsChangedHandler(std::function<void(timeline::ClipId,
    std::vector<timeline::ColorGradeVersion>)> handler) {
    grade_versions_changed_handler_ = std::move(handler);
}

void ColorWorkspace::setCanApplyCopiedGrade(bool available) {
    can_apply_copied_grade_ = available;
}

void ColorWorkspace::setGradeEditBatchHandlers(std::function<void()> started,
    std::function<void()> finished) {
    grade_edit_started_handler_ = std::move(started);
    grade_edit_finished_handler_ = std::move(finished);
}

void ColorWorkspace::setPlaybackHandler(std::function<void(playback::PlaybackCommand)> handler) {
    playback_handler_ = std::move(handler);
}

void ColorWorkspace::setScopeFrameRequestHandler(
    std::function<void(quint64, qint64, quint64, quint64)> handler) {
    scope_frame_request_handler_ = std::move(handler);
}

void ColorWorkspace::setColorInputFrameRequestHandler(
    std::function<void(quint64, timeline::ClipId, color::NodeId, qint64,
        quint64, quint64, quint64, color::Graph)> handler) {
    color_input_frame_request_handler_ = std::move(handler);
}

void ColorWorkspace::acceptScopeFrame(quint64 request_id, playback::VideoFramePtr frame,
    qint64 timeline_frame, quint64 composition_revision, quint64 playback_generation) {
    if (!active_ || !waveform_pending_ || request_id != active_scope_request_id_) return;
    if (timeline_frame != active_scope_identity_.timeline_frame ||
        composition_revision != active_scope_identity_.composition_revision ||
        playback_generation != active_scope_identity_.playback_generation) {
        waveform_pending_ = false;
        latest_scope_source_ = {};
        refreshMattePreview();
        return;
    }
    if (!frame) {
        waveform_pending_ = false;
        waveform_->setPixmap({});
        waveform_->setText(QCoreApplication::translate("ColorWorkspace", "No frame"));
        latest_scope_source_ = {};
        refreshMattePreview();
        return;
    }
    renderScopeFrame(active_scope_identity_, std::move(frame), request_id);
}

void ColorWorkspace::acceptColorInputFrame(quint64 request_id,
    timeline::ClipId clip_id, color::NodeId node_id,
    playback::VideoFramePtr frame, qint64 timeline_frame,
    quint64 delivery_epoch, quint64 composition_revision,
    quint64 playback_generation) {
    if (!active_ || request_id != active_color_input_request_id_ ||
        clip_id != selected_clip_ || !selected_node_ || node_id != *selected_node_)
        return;
    active_color_input_request_id_ = 0;
    auto* viewer = qobject_cast<PreviewWidget*>(preview_);
    if (!viewer || !frame || frame->width <= 0 || frame->height <= 0 ||
        frame->stride < frame->width * 4 ||
        frame->rgba_pixels.size() < static_cast<std::size_t>(frame->stride) * frame->height) {
        latest_curves_input_ = {};
        refreshMattePreview();
        return;
    }
    const auto current = viewer->currentPayload();
    if (!current.valid() || current.timeline_frame != timeline_frame ||
        current.composition_revision != composition_revision ||
        current.playback_generation != playback_generation ||
        current.delivery_epoch != delivery_epoch ||
        current.delivery_epoch != active_color_input_identity_.delivery_epoch ||
        current.timeline_frame != active_color_input_identity_.timeline_frame ||
        current.composition_revision != active_color_input_identity_.composition_revision ||
        current.playback_generation != active_color_input_identity_.playback_generation) {
        latest_curves_input_ = {};
        refreshMattePreview();
        return;
    }
    QImage source(frame->rgba_pixels.data(), frame->width, frame->height,
        frame->stride, QImage::Format_RGBA8888);
    latest_curves_input_ = source.isNull() ? QImage{} :
        source.scaled(256, 144, Qt::IgnoreAspectRatio,
            Qt::FastTransformation).convertToFormat(QImage::Format_RGB32);
    refreshMattePreview();
}

void ColorWorkspace::setGalleryStills(std::vector<StillItem> stills) {
    const auto selected_id = stills_->currentItem()
        ? stills_->currentItem()->data(Qt::UserRole).toString() : QString{};
    const QSignalBlocker blocker(stills_);
    stills_->clear();
    int selected_row = -1;
    for (const auto& still : stills) {
        auto* item = new QListWidgetItem(still.available
            ? still.name
            : QCoreApplication::translate("ColorWorkspace", "%1 (Unavailable)").arg(still.name),
            stills_);
        item->setData(Qt::UserRole, still.id);
        item->setToolTip(still.available
            ? QCoreApplication::translate("ColorWorkspace", "%1 · Frame %2").arg(still.source).arg(still.frame)
            : QCoreApplication::translate("ColorWorkspace", "Still image unavailable · %1 · Frame %2").arg(still.source).arg(still.frame));
        if (!still.thumbnail.isNull()) item->setIcon(QIcon(QPixmap::fromImage(still.thumbnail)));
        if (!still.available) item->setForeground(palette().brush(QPalette::Disabled, QPalette::Text));
        item->setSizeHint(QSize(128, 92));
        if (still.id == selected_id) selected_row = stills_->count() - 1;
    }
    if (selected_row >= 0) stills_->setCurrentRow(selected_row);
}

void ColorWorkspace::setCapturePending(bool pending) {
    capture_still_button_->setEnabled(!pending);
}

void ColorWorkspace::setCaptureStillHandler(std::function<void()> handler) {
    capture_still_handler_ = std::move(handler);
}

void ColorWorkspace::setRenameStillHandler(
    std::function<void(const QString&, const QString&)> handler) {
    rename_still_handler_ = std::move(handler);
}

void ColorWorkspace::setRemoveStillHandler(std::function<void(const QString&)> handler) {
    remove_still_handler_ = std::move(handler);
}

void ColorWorkspace::renameSelectedStill() {
    const auto* item = stills_->currentItem();
    if (!item || !rename_still_handler_) return;
    bool accepted = false;
    const auto name = QInputDialog::getText(this,
        QCoreApplication::translate("ColorWorkspace", "Rename still"),
        QCoreApplication::translate("ColorWorkspace", "Name"),
        QLineEdit::Normal, item->text(), &accepted).trimmed();
    if (accepted && !name.isEmpty())
        rename_still_handler_(item->data(Qt::UserRole).toString(), name);
}

void ColorWorkspace::saveGradeVersion() {
    if (selected_clip_ == 0 || !grade_versions_changed_handler_ ||
        !color::validate(graph_)) return;
    const auto name_exists = [this](const QString& candidate) {
        const auto utf8 = candidate.toUtf8().toStdString();
        return std::any_of(selected_grade_versions_.begin(), selected_grade_versions_.end(),
            [&utf8](const timeline::ColorGradeVersion& version) {
                return version.name == utf8;
            });
    };
    std::size_t next_number = 1;
    QString default_name;
    do {
        default_name = QCoreApplication::translate("ColorWorkspace", "Version %1")
            .arg(static_cast<qulonglong>(next_number++));
    } while (name_exists(default_name));
    bool accepted = false;
    const auto name = QInputDialog::getText(this,
        QCoreApplication::translate("ColorWorkspace", "Save Grade Version"),
        QCoreApplication::translate("ColorWorkspace", "Name"),
        QLineEdit::Normal, default_name, &accepted).trimmed();
    if (!accepted) return;
    if (name.isEmpty() || name_exists(name)) {
        QMessageBox::warning(this,
            QCoreApplication::translate("ColorWorkspace", "Invalid Grade Version Name"),
            QCoreApplication::translate("ColorWorkspace",
                "Version names must be non-empty and unique for this clip."));
        return;
    }
    auto versions = selected_grade_versions_;
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces).toUtf8();
    versions.push_back({
        std::string(id.constData(), static_cast<std::size_t>(id.size())),
        name.toUtf8().toStdString(), graph_});
    grade_versions_changed_handler_(selected_clip_, std::move(versions));
}

void ColorWorkspace::applySelectedGradeVersion() {
    const auto* item = grade_versions_->currentItem();
    if (!item || selected_clip_ == 0 || !effects_changed_handler_) return;
    const auto id = item->data(Qt::UserRole).toString().toUtf8().toStdString();
    const auto version = std::find_if(selected_grade_versions_.begin(),
        selected_grade_versions_.end(), [&id](const timeline::ColorGradeVersion& entry) {
            return entry.id == id;
        });
    if (version != selected_grade_versions_.end())
        effects_changed_handler_(selected_clip_, version->graph);
}

void ColorWorkspace::renameSelectedGradeVersion() {
    const auto* item = grade_versions_->currentItem();
    if (!item || selected_clip_ == 0 || !grade_versions_changed_handler_) return;
    const auto id = item->data(Qt::UserRole).toString().toUtf8().toStdString();
    const auto current = std::find_if(selected_grade_versions_.begin(),
        selected_grade_versions_.end(), [&id](const timeline::ColorGradeVersion& entry) {
            return entry.id == id;
        });
    if (current == selected_grade_versions_.end()) return;
    bool accepted = false;
    const auto name = QInputDialog::getText(this,
        QCoreApplication::translate("ColorWorkspace", "Rename Grade Version"),
        QCoreApplication::translate("ColorWorkspace", "Name"), QLineEdit::Normal,
        QString::fromUtf8(current->name.data(),
            static_cast<qsizetype>(current->name.size())), &accepted).trimmed();
    if (!accepted) return;
    const auto utf8_name = name.toUtf8().toStdString();
    const bool duplicate = std::any_of(selected_grade_versions_.begin(),
        selected_grade_versions_.end(), [&id, &utf8_name](
            const timeline::ColorGradeVersion& entry) {
                return entry.id != id && entry.name == utf8_name;
            });
    if (name.isEmpty() || duplicate) {
        QMessageBox::warning(this,
            QCoreApplication::translate("ColorWorkspace", "Invalid Grade Version Name"),
            QCoreApplication::translate("ColorWorkspace",
                "Version names must be non-empty and unique for this clip."));
        return;
    }
    auto versions = selected_grade_versions_;
    const auto updated = std::find_if(versions.begin(), versions.end(),
        [&id](const timeline::ColorGradeVersion& entry) { return entry.id == id; });
    if (updated != versions.end()) updated->name = utf8_name;
    grade_versions_changed_handler_(selected_clip_, std::move(versions));
}

void ColorWorkspace::removeSelectedGradeVersion() {
    const auto* item = grade_versions_->currentItem();
    if (!item || selected_clip_ == 0 || !grade_versions_changed_handler_) return;
    const auto id = item->data(Qt::UserRole).toString().toUtf8().toStdString();
    auto versions = selected_grade_versions_;
    const auto old_size = versions.size();
    std::erase_if(versions, [&id](const timeline::ColorGradeVersion& entry) {
        return entry.id == id;
    });
    if (versions.size() != old_size)
        grade_versions_changed_handler_(selected_clip_, std::move(versions));
}

void ColorWorkspace::refreshNodes(std::optional<color::NodeId> selected) {
    const auto previous_selection = selected_node_;
    if (selected && color::findNode(graph_, *selected)) selected_node_ = selected;
    else if (!selected_node_ || !color::findNode(graph_, *selected_node_)) {
        const auto input = std::find_if(graph_.nodes.begin(), graph_.nodes.end(),
            [](const color::Node& node) { return node.type == color::NodeType::Input; });
        selected_node_ = input == graph_.nodes.end()
            ? std::optional<color::NodeId>{}
            : std::optional<color::NodeId>{input->id};
    }
    nodes_->setGraph(graph_, selected_node_);
    if (previous_selection != selected_node_) {
        active_color_input_request_id_ = 0;
        latest_curves_input_ = {};
    }
    refreshControls();
}

void ColorWorkspace::refreshControls() {
    matte_preview_ = nullptr;
    matte_preview_mode_ = nullptr;
    while (auto* item = controls_layout_->takeAt(0)) {
        if (auto* widget = item->widget()) {
            widget->hide();
            widget->deleteLater();
        }
        delete item;
    }
    const auto* node = selected_node_ ? color::findNode(graph_, *selected_node_) : nullptr;
    const bool effect_selected = node && node->type == color::NodeType::Effect && node->effect;
    const bool curves_selected = node && node->type == color::NodeType::Curves && node->curves;
    auto* toggle = findChild<QPushButton*>(QStringLiteral("colorToggleNodeButton"));
    auto* remove = findChild<QPushButton*>(QStringLiteral("colorRemoveNodeButton"));
    auto* add = findChild<QPushButton*>(QStringLiteral("colorAddNodeButton"));
    const bool primary_grade_selected = effect_selected &&
        node->effect->id == "video.primary_grade";
    if (primary_grade_mode_ != nullptr)
        primary_grade_mode_->setVisible(primary_grade_selected);
    if (toggle) toggle->setEnabled(effect_selected);
    if (remove) remove->setEnabled(effect_selected ||
        (node && node->type == color::NodeType::ParallelMixer));
    if (add) add->setEnabled(node && node->type != color::NodeType::Output);
    if (add_branch_button_) add_branch_button_->setEnabled(node &&
        (node->type == color::NodeType::Input || node->type == color::NodeType::Effect));

    if (effect_selected) {
        const auto* definition = creative_suite::effects::findDefinition(node->effect->id);
        if (primary_grade_selected) {
            const auto add_wheel = [this, node](const QString& label, const QString& key,
                std::array<std::string, 3> parameter_ids, double minimum, double maximum,
                double neutral) {
                auto* wheel = new PrimaryGradeWheel(key, minimum, maximum, neutral, controls_);
                const std::array<double, 3> values{
                    creative_suite::effects::parameterValue(*node->effect, parameter_ids[0]),
                    creative_suite::effects::parameterValue(*node->effect, parameter_ids[1]),
                    creative_suite::effects::parameterValue(*node->effect, parameter_ids[2])};
                wheel->setValues(values);
                const auto node_id = node->id;
                wheel->setHandlers(
                    [this] { if (grade_edit_started_handler_) grade_edit_started_handler_(); },
                    [this] { if (grade_edit_finished_handler_) grade_edit_finished_handler_(); },
                    [this, node_id, parameter_ids](std::array<double, 3> adjusted) {
                        auto graph = graph_;
                        bool changed = false;
                        for (int channel = 0; channel < 3; ++channel)
                            changed = color::setEffectParameter(graph, node_id,
                                parameter_ids[channel], adjusted[channel]) || changed;
                        if (changed) submit(std::move(graph), node_id, false);
                    });
                controls_layout_->addRow(label, wheel);
            };
            const auto add_bar = [this, node](const QString& label, const QString& key,
                std::array<std::string, 3> parameter_ids, double minimum, double maximum,
                double neutral) {
                auto* bar = new PrimaryGradeBars(key, minimum, maximum, neutral, controls_);
                const std::array<double, 3> values{
                    creative_suite::effects::parameterValue(*node->effect, parameter_ids[0]),
                    creative_suite::effects::parameterValue(*node->effect, parameter_ids[1]),
                    creative_suite::effects::parameterValue(*node->effect, parameter_ids[2])};
                bar->setValues(values);
                const auto node_id = node->id;
                bar->setHandlers(
                    [this] { if (grade_edit_started_handler_) grade_edit_started_handler_(); },
                    [this] { if (grade_edit_finished_handler_) grade_edit_finished_handler_(); },
                    [this, node_id, parameter_ids](std::array<double, 3> adjusted) {
                        auto graph = graph_;
                        bool changed = false;
                        for (int channel = 0; channel < 3; ++channel)
                            changed = color::setEffectParameter(graph, node_id,
                                parameter_ids[channel], adjusted[channel]) || changed;
                        if (changed) submit(std::move(graph), node_id, false);
                    });
                controls_layout_->addRow(label, bar);
            };
            const auto mode = primary_grade_mode_->currentData().toString();
            using PrimaryGroupAdder = std::function<void(const QString&, const QString&,
                std::array<std::string, 3>, double, double, double)>;
            const PrimaryGroupAdder add_primary_groups = mode == QLatin1String("bars")
                ? PrimaryGroupAdder(add_bar) : PrimaryGroupAdder(add_wheel);
            if (mode == QLatin1String("log")) {
                add_wheel(QCoreApplication::translate("ColorWorkspace", "Shadows"),
                    QStringLiteral("log_shadows"),
                    {"log_shadows_red", "log_shadows_green", "log_shadows_blue"},
                    -100.0, 100.0, 0.0);
                add_wheel(QCoreApplication::translate("ColorWorkspace", "Midtones"),
                    QStringLiteral("log_midtones"),
                    {"log_midtones_red", "log_midtones_green", "log_midtones_blue"},
                    -100.0, 100.0, 0.0);
                add_wheel(QCoreApplication::translate("ColorWorkspace", "Highlights"),
                    QStringLiteral("log_highlights"),
                    {"log_highlights_red", "log_highlights_green", "log_highlights_blue"},
                    -100.0, 100.0, 0.0);
                add_wheel(QCoreApplication::translate("ColorWorkspace", "Offset"), QStringLiteral("offset"),
                    {"offset_red", "offset_green", "offset_blue"}, -100.0, 100.0, 0.0);
            } else {
                add_primary_groups(QCoreApplication::translate("ColorWorkspace", "Lift"), QStringLiteral("lift"),
                    {"lift_red", "lift_green", "lift_blue"}, -100.0, 100.0, 0.0);
                add_primary_groups(QCoreApplication::translate("ColorWorkspace", "Gamma"), QStringLiteral("gamma"),
                    {"gamma_red", "gamma_green", "gamma_blue"}, 25.0, 400.0, 100.0);
                add_primary_groups(QCoreApplication::translate("ColorWorkspace", "Gain"), QStringLiteral("gain"),
                    {"red_gain", "green_gain", "blue_gain"}, 0.0, 200.0, 100.0);
                add_primary_groups(QCoreApplication::translate("ColorWorkspace", "Offset"), QStringLiteral("offset"),
                    {"offset_red", "offset_green", "offset_blue"}, -100.0, 100.0, 0.0);
            }
        }
        const auto channel_wheel_parameter = [](std::string_view id) {
            return id == "lift_red" || id == "lift_green" || id == "lift_blue" ||
                id == "gamma_red" || id == "gamma_green" || id == "gamma_blue" ||
                id == "red_gain" || id == "green_gain" || id == "blue_gain" ||
                id == "offset_red" || id == "offset_green" || id == "offset_blue" ||
                id == "log_shadows_red" || id == "log_shadows_green" || id == "log_shadows_blue" ||
                id == "log_midtones_red" || id == "log_midtones_green" || id == "log_midtones_blue" ||
                id == "log_highlights_red" || id == "log_highlights_green" || id == "log_highlights_blue";
        };
        if (definition) for (const auto& parameter : definition->parameters) {
            if (node->effect->id == "video.primary_grade" &&
                channel_wheel_parameter(parameter.id)) continue;
            auto* spin = new QDoubleSpinBox(controls_);
            spin->setObjectName(QStringLiteral("colorParameter_%1").arg(
                QString::fromLatin1(parameter.id.data(), parameter.id.size())));
            spin->setRange(parameter.minimum, parameter.maximum);
            spin->setSingleStep(parameter.step);
            spin->setDecimals(0);
            spin->setFixedWidth(110);
            spin->setValue(creative_suite::effects::parameterValue(*node->effect, parameter.id));
            controls_layout_->addRow(parameterLabel(parameter.id), spin);
            const auto parameter_id = std::string(parameter.id);
            const auto node_id = node->id;
            connect(spin, &QDoubleSpinBox::editingFinished, this,
                [this, spin, node_id, parameter_id] {
                    auto graph = graph_;
                    if (!color::setEffectParameter(graph, node_id, parameter_id, spin->value())) return;
                    submit(std::move(graph), node_id);
                });
        }
    } else if (curves_selected) {
        using creative_suite::effects::TonalQualifier;
        using creative_suite::effects::VideoCurvesParameters;
        const auto node_id = node->id;
        auto* curve_kind = new QComboBox(controls_);
        curve_kind->setObjectName(QStringLiteral("colorCurveKind"));
        const std::array<std::pair<QString, color::CurveKind>, 9> kinds{{
            {QCoreApplication::translate("ColorWorkspace", "Master"), color::CurveKind::Master},
            {QCoreApplication::translate("ColorWorkspace", "Red"), color::CurveKind::Red},
            {QCoreApplication::translate("ColorWorkspace", "Green"), color::CurveKind::Green},
            {QCoreApplication::translate("ColorWorkspace", "Blue"), color::CurveKind::Blue},
            {QCoreApplication::translate("ColorWorkspace", "Hue vs Hue"), color::CurveKind::HueVsHue},
            {QCoreApplication::translate("ColorWorkspace", "Hue vs Saturation"), color::CurveKind::HueVsSaturation},
            {QCoreApplication::translate("ColorWorkspace", "Hue vs Luminance"), color::CurveKind::HueVsLuminance},
            {QCoreApplication::translate("ColorWorkspace", "Luminance vs Saturation"), color::CurveKind::LuminanceVsSaturation},
            {QCoreApplication::translate("ColorWorkspace", "Saturation vs Saturation"), color::CurveKind::SaturationVsSaturation}}};
        for (const auto& [label, kind] : kinds)
            curve_kind->addItem(label, static_cast<int>(kind));
        controls_layout_->addRow(QCoreApplication::translate("ColorWorkspace", "Curve"), curve_kind);
        auto* editor = new CurveEditor(controls_);
        const auto initial_kind = static_cast<color::CurveKind>(curve_kind->currentData().toInt());
        editor->setPoints(initial_kind, curvePointsFor(node->curves->parameters, initial_kind));
        editor->setHandlers(
            [this] { if (grade_edit_started_handler_) grade_edit_started_handler_(); },
            [this] { if (grade_edit_finished_handler_) grade_edit_finished_handler_(); },
            [this, node_id, curve_kind](creative_suite::effects::CurvesPointList points) {
                auto graph = graph_;
                const auto kind = static_cast<color::CurveKind>(curve_kind->currentData().toInt());
                if (color::setCurvePoints(graph, node_id, kind, std::move(points)))
                    submit(std::move(graph), node_id, false);
            });
        controls_layout_->addRow(editor);
        connect(curve_kind, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this, editor, node_id, curve_kind] {
                const auto* selected = color::findNode(graph_, node_id);
                if (!selected || !selected->curves) return;
                const auto kind = static_cast<color::CurveKind>(curve_kind->currentData().toInt());
                editor->setPoints(kind, curvePointsFor(selected->curves->parameters, kind));
            });

        auto* enabled = new QCheckBox(
            QCoreApplication::translate("ColorWorkspace", "Enable tonal qualifier"), controls_);
        enabled->setObjectName(QStringLiteral("colorCurvesQualifierEnabled"));
        enabled->setChecked(node->curves->parameters.qualifier.enabled);
        controls_layout_->addRow(enabled);
        connect(enabled, &QCheckBox::toggled, this, [this, node_id](bool checked) {
            auto graph = graph_;
            auto* selected = color::findNode(graph, node_id);
            if (!selected || !selected->curves) return;
            auto qualifier = selected->curves->parameters.qualifier;
            qualifier.enabled = checked;
            if (grade_edit_started_handler_) grade_edit_started_handler_();
            if (color::setCurvesQualifier(graph, node_id, qualifier))
                submit(std::move(graph), node_id, false);
            if (grade_edit_finished_handler_) grade_edit_finished_handler_();
            refreshMattePreview();
        });
        struct QualifierControl {
            const char* id;
            const char* label;
            double minimum;
            double maximum;
            double step;
        };
        const std::array<QualifierControl, 14> qualifier_controls{{
            {"hue_min", "Hue minimum", 0, 360, 1}, {"hue_max", "Hue maximum", 0, 360, 1},
            {"saturation_min", "Saturation minimum", 0, 100, 1},
            {"saturation_max", "Saturation maximum", 0, 100, 1},
            {"luminance_min", "Luminance minimum", 0, 100, 1},
            {"luminance_max", "Luminance maximum", 0, 100, 1},
            {"hue_softness", "Hue softness", 0, 50, 1},
            {"saturation_softness", "Saturation softness", 0, 50, 1},
            {"luminance_softness", "Luminance softness", 0, 50, 1},
            {"denoise_percent", "Denoise", 0, 100, 1},
            {"clean_black_percent", "Clean Black", 0, 99, 1},
            {"clean_white_percent", "Clean White", 1, 100, 1},
            {"in_out_percent", "In/Out", -100, 100, 1},
            {nullptr, nullptr, 0, 0, 0}}};
        const auto qualifierValue = [](const TonalQualifier& q, const char* id) {
            if (std::strcmp(id, "hue_min") == 0) return q.hue_min;
            if (std::strcmp(id, "hue_max") == 0) return q.hue_max;
            if (std::strcmp(id, "saturation_min") == 0) return q.saturation_min;
            if (std::strcmp(id, "saturation_max") == 0) return q.saturation_max;
            if (std::strcmp(id, "luminance_min") == 0) return q.luminance_min;
            if (std::strcmp(id, "luminance_max") == 0) return q.luminance_max;
            if (std::strcmp(id, "hue_softness") == 0) return q.hue_softness;
            if (std::strcmp(id, "saturation_softness") == 0) return q.saturation_softness;
            if (std::strcmp(id, "luminance_softness") == 0) return q.luminance_softness;
            if (std::strcmp(id, "denoise_percent") == 0) return q.denoise_percent;
            if (std::strcmp(id, "clean_black_percent") == 0) return q.clean_black_percent;
            if (std::strcmp(id, "clean_white_percent") == 0) return q.clean_white_percent;
            return q.in_out_percent;
        };
        for (const auto& control : qualifier_controls) {
            if (!control.id) continue;
            auto* spin = new QDoubleSpinBox(controls_);
            spin->setObjectName(QStringLiteral("colorQualifier_%1").arg(
                QString::fromLatin1(control.id)));
            spin->setRange(control.minimum, control.maximum);
            spin->setSingleStep(control.step);
            spin->setDecimals(0);
            spin->setFixedWidth(92);
            spin->setValue(qualifierValue(node->curves->parameters.qualifier, control.id));
            QString translated_label = QCoreApplication::translate("ColorWorkspace", control.label);
            if (std::strcmp(control.id, "denoise_percent") == 0)
                translated_label = QCoreApplication::translate("ColorWorkspace", "Denoise");
            else if (std::strcmp(control.id, "clean_black_percent") == 0)
                translated_label = QCoreApplication::translate("ColorWorkspace", "Clean Black");
            else if (std::strcmp(control.id, "clean_white_percent") == 0)
                translated_label = QCoreApplication::translate("ColorWorkspace", "Clean White");
            else if (std::strcmp(control.id, "in_out_percent") == 0)
                translated_label = QCoreApplication::translate("ColorWorkspace", "In/Out");
            controls_layout_->addRow(translated_label, spin);
            const auto key = std::string(control.id);
            connect(spin, &QDoubleSpinBox::editingFinished, this,
                [this, spin, node_id, key] {
                    auto graph = graph_;
                    auto* selected = color::findNode(graph, node_id);
                    if (!selected || !selected->curves) return;
                    auto q = selected->curves->parameters.qualifier;
                    if (key == "hue_min") q.hue_min = spin->value();
                    else if (key == "hue_max") q.hue_max = spin->value();
                    else if (key == "saturation_min") q.saturation_min = spin->value();
                    else if (key == "saturation_max") q.saturation_max = spin->value();
                    else if (key == "luminance_min") q.luminance_min = spin->value();
                    else if (key == "luminance_max") q.luminance_max = spin->value();
                    else if (key == "hue_softness") q.hue_softness = spin->value();
                    else if (key == "saturation_softness") q.saturation_softness = spin->value();
                    else if (key == "luminance_softness") q.luminance_softness = spin->value();
                    else if (key == "denoise_percent") q.denoise_percent = spin->value();
                    else if (key == "clean_black_percent") q.clean_black_percent = spin->value();
                    else if (key == "clean_white_percent") q.clean_white_percent = spin->value();
                    else if (key == "in_out_percent") q.in_out_percent = spin->value();
                    if (q.saturation_min > q.saturation_max) q.saturation_max = q.saturation_min;
                    if (q.luminance_min > q.luminance_max) q.luminance_max = q.luminance_min;
                    if (q.clean_black_percent >= q.clean_white_percent) {
                        if (key == "clean_black_percent") q.clean_black_percent = q.clean_white_percent - 1.0;
                        else q.clean_white_percent = q.clean_black_percent + 1.0;
                    }
                    if (grade_edit_started_handler_) grade_edit_started_handler_();
                    if (color::setCurvesQualifier(graph, node_id, q))
                        submit(std::move(graph), node_id, false);
                    if (grade_edit_finished_handler_) grade_edit_finished_handler_();
                    refreshControls();
                    refreshMattePreview();
                });
        }
        auto* blur = new QDoubleSpinBox(controls_);
        blur->setObjectName(QStringLiteral("colorQualifier_blur_radius_pixels"));
        blur->setRange(0.0, 8.0);
        blur->setSingleStep(0.25);
        blur->setDecimals(2);
        blur->setFixedWidth(92);
        blur->setValue(node->curves->parameters.qualifier.blur_radius_pixels);
        controls_layout_->addRow(QCoreApplication::translate("ColorWorkspace", "Blur"), blur);
        connect(blur, &QDoubleSpinBox::editingFinished, this, [this, blur, node_id] {
            auto graph = graph_;
            auto* selected = color::findNode(graph, node_id);
            if (!selected || !selected->curves) return;
            auto q = selected->curves->parameters.qualifier;
            q.blur_radius_pixels = blur->value();
            if (grade_edit_started_handler_) grade_edit_started_handler_();
            if (color::setCurvesQualifier(graph, node_id, q))
                submit(std::move(graph), node_id, false);
            if (grade_edit_finished_handler_) grade_edit_finished_handler_();
            refreshMattePreview();
        });
        matte_preview_mode_ = new QComboBox(controls_);
        matte_preview_mode_->setObjectName(QStringLiteral("colorQualifierPreviewMode"));
        matte_preview_mode_->addItem(QCoreApplication::translate("ColorWorkspace", "Matte"), "matte");
        matte_preview_mode_->addItem(QCoreApplication::translate("ColorWorkspace", "Highlight"), "highlight");
        controls_layout_->addRow(QCoreApplication::translate("ColorWorkspace", "Preview"), matte_preview_mode_);
        connect(matte_preview_mode_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this] { refreshMattePreview(); });
        matte_preview_ = new QLabel(controls_);
        matte_preview_->setObjectName(QStringLiteral("colorQualifierMattePreview"));
        matte_preview_->setMinimumSize(160, 88);
        matte_preview_->setMaximumHeight(110);
        matte_preview_->setAlignment(Qt::AlignCenter);
        matte_preview_->setScaledContents(true);
        matte_preview_->setText(QCoreApplication::translate("ColorWorkspace", "No frame"));
        controls_layout_->addRow(QCoreApplication::translate("ColorWorkspace", "Matte preview"), matte_preview_);
        refreshMattePreview();
    } else if (node && node->type == color::NodeType::ParallelMixer) {
        for (const auto& edge : graph_.connections) {
            if (edge.to != node->id) continue;
            auto* spin = new QDoubleSpinBox(controls_);
            spin->setObjectName(QStringLiteral("colorMixerWeight_%1").arg(edge.input));
            spin->setRange(0.0, 1.0);
            spin->setSingleStep(0.05);
            spin->setDecimals(2);
            spin->setFixedWidth(110);
            spin->setValue(edge.weight);
            controls_layout_->addRow(
                QCoreApplication::translate("ColorWorkspace", "Branch %1 weight")
                    .arg(edge.input + 1), spin);
            const auto node_id = node->id;
            const auto input = edge.input;
            connect(spin, &QDoubleSpinBox::editingFinished, this,
                [this, spin, node_id, input] {
                    auto graph = graph_;
                    if (color::setMixerInputWeight(graph, node_id, input, spin->value()))
                        submit(std::move(graph), node_id);
                });
        }
    }
}

void ColorWorkspace::submit(color::Graph graph, std::optional<color::NodeId> selected,
    bool rebuild_controls) {
    if (graph == graph_) return;
    bool preserve_curves_input = false;
    if (selected && selected_node_ == selected && !latest_curves_input_.isNull()) {
        auto comparable_graph = graph;
        auto* comparable_node = color::findNode(comparable_graph, *selected);
        const auto* previous_node = color::findNode(graph_, *selected);
        if (comparable_node && previous_node && comparable_node->curves &&
            previous_node->curves && comparable_node->type == color::NodeType::Curves &&
            previous_node->type == color::NodeType::Curves) {
            comparable_node->curves = previous_node->curves;
            preserve_curves_input = comparable_graph == graph_;
        }
    }
    graph_ = std::move(graph);
    if (!preserve_curves_input) latest_curves_input_ = {};
    active_color_input_request_id_ = 0;
    selected_node_ = selected && color::findNode(graph_, *selected)
        ? selected : std::optional<color::NodeId>{};
    if (rebuild_controls) refreshNodes(selected_node_);
    else nodes_->setGraph(graph_, selected_node_);
    if (preserve_curves_input) refreshMattePreview();
    if (selected_clip_ && effects_changed_handler_)
        effects_changed_handler_(selected_clip_, graph_);
}

void ColorWorkspace::scheduleWaveform() {
    if (!active_ || waveform_pending_ || !preview_) return;
    auto* viewer = qobject_cast<PreviewWidget*>(preview_);
    if (!viewer) return;
    const auto payload = viewer->currentPayload();
    if (!payload.valid()) {
        waveform_->setPixmap({});
        waveform_->setText(QCoreApplication::translate("ColorWorkspace", "No frame"));
        latest_scope_source_ = {};
        latest_curves_input_ = {};
        active_color_input_request_id_ = 0;
        refreshMattePreview();
        return;
    }
    if (next_scope_request_id_ == 0) ++next_scope_request_id_;
    const auto request_id = next_scope_request_id_++;
    waveform_pending_ = true;
    active_scope_request_id_ = request_id;
    active_scope_identity_ = payload;
    if (payload.rgba) {
        const auto& frame = *payload.rgba;
        if (frame.width <= 0 || frame.height <= 0 || frame.stride < frame.width * 4 ||
            frame.rgba_pixels.size() < static_cast<std::size_t>(frame.stride) * frame.height) {
            waveform_pending_ = false;
            waveform_->setPixmap({});
            waveform_->setText(QCoreApplication::translate("ColorWorkspace", "No frame"));
            latest_scope_source_ = {};
            latest_curves_input_ = {};
            active_color_input_request_id_ = 0;
            refreshMattePreview();
            return;
        }
        renderScopeFrame(payload, payload.rgba, request_id);
        return;
    }
    if (!scope_frame_request_handler_) {
        waveform_pending_ = false;
        waveform_->setPixmap({});
        waveform_->setText(QCoreApplication::translate("ColorWorkspace", "No frame"));
        latest_scope_source_ = {};
        latest_curves_input_ = {};
        active_color_input_request_id_ = 0;
        refreshMattePreview();
        return;
    }
    scope_frame_request_handler_(request_id, payload.timeline_frame,
        payload.composition_revision, payload.playback_generation);
}

void ColorWorkspace::renderScopeFrame(rendering::PreviewFramePayload identity,
    playback::VideoFramePtr frame, quint64 request_id) {
    if (!frame || frame->width <= 0 || frame->height <= 0 || frame->stride < frame->width * 4 ||
        frame->rgba_pixels.size() < static_cast<std::size_t>(frame->stride) * frame->height) {
        if (request_id == active_scope_request_id_) {
            waveform_pending_ = false;
            waveform_->setPixmap({});
            waveform_->setText(QCoreApplication::translate("ColorWorkspace", "No frame"));
            latest_curves_input_ = {};
            active_color_input_request_id_ = 0;
            refreshMattePreview();
        }
        return;
    }
    QImage source(frame->rgba_pixels.data(), frame->width, frame->height,
        frame->stride, QImage::Format_RGBA8888);
    if (source.isNull()) {
        waveform_pending_ = false;
        waveform_->setPixmap({});
        waveform_->setText(QCoreApplication::translate("ColorWorkspace", "No frame"));
        latest_scope_source_ = {};
        latest_curves_input_ = {};
        active_color_input_request_id_ = 0;
        refreshMattePreview();
        return;
    }
    latest_scope_source_ = source.scaled(256, 144, Qt::IgnoreAspectRatio,
        Qt::FastTransformation).convertToFormat(QImage::Format_RGB32);
    refreshMattePreview();
    requestCurvesInputFrame(identity);
    const auto generation = waveform_generation_;
    const auto scope_mode = scope_mode_->currentData().toString();
    const QPointer<ColorWorkspace> target(this);
    const auto frame_owner = frame;
    QThreadPool::globalInstance()->start([target, generation, identity = std::move(identity),
        request_id, source = std::move(source), frame_owner, scope_mode] {
        auto image = calculateScope(source, scope_mode);
        if (!target) return;
        QMetaObject::invokeMethod(target, [target, generation, identity, request_id,
            image = std::move(image), frame_owner] {
            if (!target || generation != target->waveform_generation_ ||
                request_id != target->active_scope_request_id_) return;
            target->waveform_pending_ = false;
            if (!target->active_) return;
            auto* viewer = qobject_cast<PreviewWidget*>(target->preview_);
            if (!viewer) return;
            const auto current = viewer->currentPayload();
            const bool same_frame = current.valid() &&
                current.delivery_epoch == identity.delivery_epoch &&
                current.composition_revision == identity.composition_revision &&
                current.playback_generation == identity.playback_generation &&
                current.timeline_frame == identity.timeline_frame &&
                current.rgba == identity.rgba && current.gpu == identity.gpu;
            if (same_frame) {
                target->waveform_->setText({});
                target->waveform_->setPixmap(QPixmap::fromImage(image));
            }
            (void)frame_owner;
        }, Qt::QueuedConnection);
    });
}

void ColorWorkspace::requestCurvesInputFrame(
    const rendering::PreviewFramePayload& identity) {
    const auto* node = selected_node_ ? color::findNode(graph_, *selected_node_) : nullptr;
    if (!identity.valid() || !node || node->type != color::NodeType::Curves ||
        !node->curves || selected_clip_ == 0 || !color_input_frame_request_handler_) {
        active_color_input_request_id_ = 0;
        latest_curves_input_ = {};
        refreshMattePreview();
        if (identity.valid() && selected_clip_ != 0 &&
            color_input_frame_request_handler_) {
            if (next_scope_request_id_ == 0) ++next_scope_request_id_;
            const auto cancel_id = next_scope_request_id_++;
            color_input_frame_request_handler_(cancel_id, selected_clip_,
                node ? node->id : color::NodeId{}, identity.timeline_frame,
                identity.delivery_epoch, identity.composition_revision,
                identity.playback_generation, graph_);
        }
        return;
    }
    const bool same_identity =
        active_color_input_identity_.delivery_epoch == identity.delivery_epoch &&
        active_color_input_identity_.composition_revision == identity.composition_revision &&
        active_color_input_identity_.playback_generation == identity.playback_generation &&
        active_color_input_identity_.timeline_frame == identity.timeline_frame;
    if ((active_color_input_request_id_ != 0 || !latest_curves_input_.isNull()) &&
        same_identity) return;
    if (next_scope_request_id_ == 0) ++next_scope_request_id_;
    const auto request_id = next_scope_request_id_++;
    active_color_input_request_id_ = request_id;
    active_color_input_identity_ = identity;
    latest_curves_input_ = {};
    refreshMattePreview();
    color_input_frame_request_handler_(request_id, selected_clip_, node->id,
        identity.timeline_frame, identity.delivery_epoch,
        identity.composition_revision,
        identity.playback_generation, graph_);
}

void ColorWorkspace::refreshMattePreview() {
    if (!matte_preview_) return;
    const auto* node = selected_node_ ? color::findNode(graph_, *selected_node_) : nullptr;
    if (!node || node->type != color::NodeType::Curves || !node->curves ||
        latest_curves_input_.isNull()) {
        matte_preview_->setPixmap({});
        matte_preview_->setText(QCoreApplication::translate("ColorWorkspace", "No frame"));
        return;
    }
    auto qualifier = node->curves->parameters.qualifier;
    qualifier.enabled = true;
    const int width = latest_curves_input_.width();
    const int height = latest_curves_input_.height();
    const int stride = width * 4;
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(stride) * height);
    for (int y = 0; y < height; ++y) {
        const auto* source = reinterpret_cast<const QRgb*>(
            latest_curves_input_.constScanLine(y));
        auto* target = rgba.data() + static_cast<std::size_t>(y) * stride;
        for (int x = 0; x < width; ++x) {
            target[x * 4] = static_cast<std::uint8_t>(qRed(source[x]));
            target[x * 4 + 1] = static_cast<std::uint8_t>(qGreen(source[x]));
            target[x * 4 + 2] = static_cast<std::uint8_t>(qBlue(source[x]));
            target[x * 4 + 3] = 255;
        }
    }
    auto coverage = creative_suite::effects::refinedTonalQualifierMatte(
        qualifier, rgba, width, height, stride);
    if (coverage.size() != static_cast<std::size_t>(width) * height) {
        matte_preview_->setPixmap({});
        matte_preview_->setText(QCoreApplication::translate("ColorWorkspace", "No frame"));
        return;
    }
    QImage matte(width, height, QImage::Format_RGB32);
    const bool highlight = matte_preview_mode_ &&
        matte_preview_mode_->currentData().toString() == QLatin1String("highlight");
    for (int y = 0; y < height; ++y) {
        const auto* source = reinterpret_cast<const QRgb*>(
            latest_curves_input_.constScanLine(y));
        auto* target = reinterpret_cast<QRgb*>(matte.scanLine(y));
        for (int x = 0; x < width; ++x) {
            const double amount = coverage[static_cast<std::size_t>(y) * width + x] / 255.0;
            if (!highlight) {
                const int value = static_cast<int>(std::lround(amount * 255.0));
                target[x] = qRgb(value, value, value);
            } else {
                const auto pixel = source[x];
                const double shade = 0.18 + amount * 0.82;
                target[x] = qRgb(static_cast<int>(qRed(pixel) * shade),
                    static_cast<int>(qGreen(pixel) * shade),
                    static_cast<int>(qBlue(pixel) * shade));
            }
        }
    }
    matte_preview_->setText({});
    matte_preview_->setPixmap(QPixmap::fromImage(matte));
}

}  // namespace ui

#include "workspaces/audio/ui/audio_workspace.h"
#include "workspaces/audio/ui/audio_effect_chain_dialog.h"
#include "workspaces/audio/ui/audio_ducking_dialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPaintEvent>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>
#include <unordered_set>

namespace ui {
namespace {

QString translated(const char* text) {
    return QCoreApplication::translate("AudioWorkspace", text);
}

QString gainText(int percent) {
    if (percent == 0) return QStringLiteral("-inf dB");
    return QString::number(20.0 * std::log10(percent / 100.0), 'f', 1) +
        QStringLiteral(" dB");
}

QString panText(int percent) {
    if (percent == 0) return translated("Center");
    return (percent < 0 ? translated("L") : translated("R")) +
        QStringLiteral(" ") + QString::number(std::abs(percent)) + QStringLiteral("%");
}

QProgressBar* createMeter(QWidget* parent, const char* object_name, int width = 10) {
    auto* meter = new QProgressBar(parent);
    meter->setObjectName(object_name);
    meter->setRange(0, 100);
    meter->setValue(0);
    meter->setOrientation(Qt::Vertical);
    meter->setTextVisible(false);
    meter->setFixedWidth(width);
    meter->setStyleSheet(
        "QProgressBar { background: #0e1014; border: 1px solid #262a33; border-radius: 1px; }"
        "QProgressBar::chunk { background: qlineargradient(x1:0, y1:1, x2:0, y2:0, "
        "stop:0 #27ae60, stop:0.65 #2ecc71, stop:0.85 #e6b422, stop:1.0 #e74c3c); }");
    return meter;
}

class DbScaleWidget final : public QWidget {
public:
    explicit DbScaleWidget(bool show_labels, QWidget* parent = nullptr)
        : QWidget(parent), show_labels_(show_labels) {
        setFixedWidth(show_labels_ ? 22 : 8);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, false);
        const int h = height();
        if (h < 24) return;
        QFont f = font();
        f.setPixelSize(8);
        p.setFont(f);
        static constexpr const char* kLabels[] = {"0", "-5", "-10", "-15", "-20", "-30", "-40", "-50"};
        static constexpr double kPos[] = {0.03, 0.14, 0.26, 0.38, 0.50, 0.66, 0.82, 0.96};
        for (int i = 0; i < 8; ++i) {
            const int y = static_cast<int>(kPos[i] * (h - 1));
            p.setPen(QColor("#3c424f"));
            p.drawLine(width() - 4, y, width() - 1, y);
            if (show_labels_) {
                p.setPen(QColor("#6d7585"));
                p.drawText(0, y - 5, width() - 5, 10, Qt::AlignRight | Qt::AlignVCenter,
                           QString::fromLatin1(kLabels[i]));
            }
        }
    }

private:
    bool show_labels_ = false;
};

class MiniCurveWidget final : public QWidget {
public:
    enum class Kind { Dynamics, Eq };
    explicit MiniCurveWidget(Kind kind, QWidget* parent = nullptr)
        : QWidget(parent), kind_(kind) {
        setFixedHeight(kind_ == Kind::Dynamics ? 38 : 26);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.fillRect(rect(), QColor("#0f1115"));
        p.setPen(QColor("#232730"));
        p.drawRect(rect().adjusted(0, 0, -1, -1));
        const int w = width();
        const int h = height();
        p.setPen(QColor("#1a1d24"));
        p.drawLine(w / 2, 1, w / 2, h - 2);
        p.drawLine(1, h / 2, w - 2, h / 2);

        p.setRenderHint(QPainter::Antialiasing, true);
        if (kind_ == Kind::Dynamics) {
            p.setPen(QPen(QColor("#9cb048"), 1.3));
            p.drawLine(QPointF(2, h - 2), QPointF(w - 2, 2));
        } else {
            p.setPen(QPen(QColor("#3bb2c7"), 1.4));
            p.drawLine(QPointF(2, h / 2.0), QPointF(w - 2, h / 2.0));
        }
    }

private:
    Kind kind_;
};

class LoudnessScaleWidget final : public QWidget {
public:
    explicit LoudnessScaleWidget(QWidget* parent = nullptr) : QWidget(parent) {
        setMinimumSize(78, 96);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    }

    void setLevels(float momentary_norm, float integrated_norm) {
        m_level_ = std::clamp(momentary_norm, 0.0F, 1.0F);
        i_level_ = std::clamp(integrated_norm, 0.0F, 1.0F);
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        const int w = width();
        const int h = height();
        const int top_y = 18;
        const int bot_y = h - 8;
        const int bar_h = std::max(10, bot_y - top_y);
        const int bar_w = 8;
        const int left_x = 10;
        const int right_x = w - 18;

        QFont f = font();
        f.setPixelSize(10);
        p.setFont(f);
        p.setPen(QColor("#b7c0ce"));
        p.drawText(left_x - 3, 1, 14, 14, Qt::AlignCenter, QStringLiteral("M"));
        p.setPen(QColor("#4f94d4"));
        p.drawText(right_x - 5, 1, 18, 14, Qt::AlignCenter, QStringLiteral("---"));

        auto draw_bar = [&](int x, float level) {
            p.fillRect(x, top_y, bar_w, bar_h, QColor("#0c0d10"));
            p.setPen(QColor("#2c303a"));
            p.drawRect(x, top_y, bar_w, bar_h);
            if (level > 0.005F) {
                const int fill_h = static_cast<int>(level * (bar_h - 2));
                QLinearGradient grad(x, top_y + bar_h, x, top_y);
                grad.setColorAt(0.0, QColor("#27ae60"));
                grad.setColorAt(0.7, QColor("#2ecc71"));
                grad.setColorAt(0.88, QColor("#e6b422"));
                grad.setColorAt(1.0, QColor("#e74c3c"));
                p.fillRect(x + 1, top_y + bar_h - 1 - fill_h, bar_w - 1, fill_h, grad);
            }
        };
        draw_bar(left_x, m_level_);
        draw_bar(right_x, i_level_);

        // Tick marks and center +9, 0, -9, -18 labels
        f.setPixelSize(8);
        p.setFont(f);
        static constexpr const char* kLabels[] = {"+9", "0", "-9", "-18"};
        static constexpr double kPos[] = {0.04, 0.36, 0.68, 0.96};
        for (int i = 0; i < 4; ++i) {
            const int y = top_y + static_cast<int>(kPos[i] * (bar_h - 1));
            p.setPen(QColor("#4a505e"));
            p.drawLine(left_x - 4, y, left_x - 2, y);
            p.drawLine(left_x + bar_w + 3, y, left_x + bar_w + 5, y);
            p.setPen(i == 1 ? QColor("#cfd6e0") : QColor("#6d7585"));
            p.drawText(left_x + bar_w + 6, y - 5, right_x - (left_x + bar_w + 8), 10,
                       Qt::AlignCenter, QString::fromLatin1(kLabels[i]));
        }
    }

private:
    float m_level_ = 0.0F;
    float i_level_ = 0.0F;
};

QWidget* createMeterSlot(QWidget* parent, const QString& badge, const QString& title,
                         QProgressBar* meter, bool show_scale, const QString& accent_hex) {
    auto* slot = new QWidget(parent);
    slot->setObjectName("audioMeterSlot");
    slot->setFixedWidth(show_scale ? 44 : 26);
    auto* layout = new QVBoxLayout(slot);
    layout->setContentsMargins(1, 4, 1, 4);
    layout->setSpacing(2);

    auto* accent = new QFrame(slot);
    accent->setFixedHeight(3);
    accent->setFixedWidth(14);
    accent->setStyleSheet(accent_hex.isEmpty()
        ? QStringLiteral("background: transparent;")
        : QStringLiteral("background: %1; border-radius: 1px;").arg(accent_hex));
    layout->addWidget(accent, 0, show_scale ? Qt::AlignRight : Qt::AlignHCenter);

    auto* badge_label = new QLabel(badge, slot);
    badge_label->setObjectName("audioMeterBadge");
    badge_label->setAlignment(Qt::AlignCenter);
    badge_label->setToolTip(title);
    layout->addWidget(badge_label, 0, show_scale ? Qt::AlignRight : Qt::AlignHCenter);

    auto* body = new QHBoxLayout;
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(2);
    if (show_scale) {
        body->addWidget(new DbScaleWidget(true, slot));
    }
    if (meter != nullptr) {
        body->addWidget(meter, 0, Qt::AlignHCenter);
    } else {
        auto* dummy = new QFrame(slot);
        dummy->setFixedWidth(7);
        dummy->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        dummy->setStyleSheet(QStringLiteral(
            "background: #0e1014; border: 1px solid #262a33;"));
        body->addWidget(dummy, 0, Qt::AlignHCenter);
    }
    body->addWidget(new DbScaleWidget(false, slot));
    layout->addLayout(body, 1);
    return slot;
}

void setGainSlider(QSlider* slider, QLabel* label, double gain) {
    const auto percent = static_cast<int>(std::lround(
        std::clamp(gain, 0.0, 2.0) * 100.0));
    const QSignalBlocker blocker(slider);
    slider->setValue(percent);
    label->setText(gainText(percent));
}

void setPanSlider(QSlider* slider, QLabel* label, double pan) {
    const auto percent = static_cast<int>(std::lround(
        std::clamp(pan, -1.0, 1.0) * 100.0));
    const QSignalBlocker blocker(slider);
    slider->setValue(percent);
    label->setText(panText(percent));
}

QSlider* createGainSlider(QWidget* parent, const char* object_name) {
    auto* slider = new QSlider(Qt::Vertical, parent);
    slider->setObjectName(object_name);
    slider->setRange(0, 200);
    slider->setMinimumHeight(105);
    return slider;
}

QSlider* createSendGainSlider(QWidget* parent, const char* object_name) {
    auto* slider = new QSlider(Qt::Horizontal, parent);
    slider->setObjectName(object_name);
    slider->setRange(0, 200);
    return slider;
}

QSlider* createPanSlider(QWidget* parent, const char* object_name) {
    auto* slider = new QSlider(Qt::Horizontal, parent);
    slider->setObjectName(object_name);
    slider->setRange(-100, 100);
    slider->setValue(0);
    return slider;
}

void clearLayout(QLayout* layout) {
    while (auto* item = layout->takeAt(0)) {
        if (item->widget() != nullptr) item->widget()->deleteLater();
        delete item;
    }
}

void fillBusCombo(QComboBox* combo,
                  const std::vector<timeline::AudioBus>& buses,
                  std::optional<timeline::AudioBusId> selected,
                  std::optional<timeline::AudioBusId> excluded = std::nullopt) {
    const QSignalBlocker blocker(combo);
    combo->clear();
    combo->addItem(translated("Master"), QVariant::fromValue<qulonglong>(0));
    for (const auto& bus : buses) {
        if (excluded == bus.bus_id) continue;
        combo->addItem(QString::fromUtf8(bus.name.c_str()),
                       QVariant::fromValue<qulonglong>(bus.bus_id));
    }
    const auto desired = selected.value_or(0);
    const auto index = combo->findData(QVariant::fromValue<qulonglong>(desired));
    combo->setCurrentIndex(index >= 0 ? index : 0);
}



}  // namespace

AudioWorkspace::AudioWorkspace(QObject* parent) : QObject(parent) {}

void AudioWorkspace::createPanels(QWidget* parent) {
    meter_page_ = new QWidget(parent);
    meter_page_->setObjectName("audioMeterPage");
    meter_page_->setStyleSheet(QStringLiteral(R"(
        QWidget#audioMeterPage { background: #16181c; color: #cfd6e0; }
        QFrame#audioTopBar, QFrame#audioTransportBar {
            background: #1b1d22; border-bottom: 1px solid #282c34;
        }
        QFrame#audioTransportBar {
            border-bottom: none; border-top: 1px solid #282c34;
        }
        QWidget#audioFxBrowser {
            background: #181a1f; border-right: 1px solid #282c34;
        }
        QFrame#audioMeterSlot, QFrame#audioControlRoomCard, QFrame#audioLoudnessCard {
            background: #191b20; border: 1px solid #272b33; border-radius: 3px;
        }
        QLabel#audioMeterBadge {
            background: transparent; color: #7f8794;
            font-size: 10px; font-weight: 600; padding: 0px;
        }
        QLabel#audioSectionTitle {
            color: #9ba5b5; font-size: 11px; font-weight: 600;
        }
        QLabel#audioTimecodeDisplay {
            color: #d8dee9; font-family: Consolas, "Courier New", monospace;
            font-size: 15px; font-weight: 600; letter-spacing: 1px;
        }
        QWidget#audioMeterPage QPushButton {
            background: #232730; color: #cfd6e0; border: 1px solid #323743;
            border-radius: 3px; padding: 3px 8px; font-size: 11px;
        }
        QWidget#audioMeterPage QPushButton:hover { background: #2c313c; }
        QWidget#audioMeterPage QPushButton:checked {
            background: #285685; border-color: #4f88c2; color: #ffffff;
        }
        QWidget#audioMeterPage QListWidget {
            background: #14161a; color: #c4ccd8; border: none; outline: 0; font-size: 11px;
        }
        QWidget#audioMeterPage QListWidget::item {
            padding: 5px 8px; border-bottom: 1px solid #1e2128;
        }
        QWidget#audioMeterPage QListWidget::item:selected {
            background: #263344; color: #ffffff;
        }
        QWidget#audioMeterPage QScrollArea { background: #14161a; border: none; }
    )"));

    auto* meter_outer = new QVBoxLayout(meter_page_);
    meter_outer->setContentsMargins(0, 0, 0, 0);
    meter_outer->setSpacing(0);

    // Top studio bar
    auto* top_bar = new QFrame(meter_page_);
    top_bar->setObjectName("audioTopBar");
    top_bar->setFixedHeight(32);
    auto* top_bar_layout = new QHBoxLayout(top_bar);
    top_bar_layout->setContentsMargins(8, 3, 8, 3);
    top_bar_layout->setSpacing(6);
    auto* meters_title = new QLabel(translated("Meters"), top_bar);
    meters_title->setObjectName("audioSectionTitle");
    top_bar_layout->addWidget(meters_title);
    top_bar_layout->addStretch(1);
    auto* dim_pill = new QPushButton(QStringLiteral("DIM"), top_bar);
    dim_pill->setCheckable(true);
    dim_pill->setFixedWidth(40);
    top_bar_layout->addWidget(dim_pill);
    meter_outer->addWidget(top_bar);

    // Main horizontal body: Meter bridge | Control Room & Loudness
    auto* body_layout = new QHBoxLayout;
    body_layout->setContentsMargins(0, 0, 0, 0);
    body_layout->setSpacing(0);

    // Standalone Audio FX Browser Panel (hosted in its own dock)
    fx_browser_panel_ = new QWidget(parent);
    fx_browser_panel_->setObjectName("audioFxBrowser");
    fx_browser_panel_->setStyleSheet(QStringLiteral(R"(
        QWidget#audioFxBrowser { background: #181a1f; color: #cfd6e0; }
        QFrame#audioFxTopBar {
            background: #1b1d22; border-bottom: 1px solid #282c34;
        }
        QWidget#audioFxSidebar {
            background: #16181c; border-right: 1px solid #282c34;
        }
        QLabel#audioFxSectionTitle {
            color: #b7c0ce; font-size: 11px; font-weight: 600;
        }
        QLabel#audioFxCategory {
            color: #8f98a8; font-size: 11px; padding: 3px 6px;
        }
        QLabel#audioFxCategoryActive {
            background: #232730; color: #e2e8f0; font-size: 11px;
            font-weight: 600; padding: 3px 6px; border-radius: 2px;
        }
        QLineEdit#audioFxSearch {
            background: #121418; color: #d8dee9; border: 1px solid #2b2f3a;
            border-radius: 2px; padding: 2px 6px; font-size: 11px;
        }
        QFrame#audioFxCard {
            background: #1c1e24; border: 1px solid #2a2e38; border-radius: 2px;
        }
        QFrame#audioFxCard:hover {
            background: #242832; border-color: #3a404e;
        }
        QLabel#audioFxIcon {
            background: #15171c; color: #7c8798; border-right: 1px solid #2a2e38;
            font-size: 10px; font-weight: 700;
        }
        QLabel#audioFxCardName {
            color: #cfd6e0; font-size: 11px; padding-left: 6px;
        }
    )"));
    auto* fx_outer = new QVBoxLayout(fx_browser_panel_);
    fx_outer->setContentsMargins(0, 0, 0, 0);
    fx_outer->setSpacing(0);

    auto* fx_top_bar = new QFrame(fx_browser_panel_);
    fx_top_bar->setObjectName("audioFxTopBar");
    fx_top_bar->setFixedHeight(30);
    auto* fx_top_layout = new QHBoxLayout(fx_top_bar);
    fx_top_layout->setContentsMargins(8, 3, 8, 3);
    fx_top_layout->setSpacing(6);
    auto* fx_header = new QLabel(translated("Audio FX"), fx_top_bar);
    fx_header->setObjectName("audioFxSectionTitle");
    fx_top_layout->addWidget(fx_header);
    fx_top_layout->addStretch(1);
    auto* fx_search = new QLineEdit(fx_top_bar);
    fx_search->setObjectName("audioFxSearch");
    fx_search->setPlaceholderText(translated("Search..."));
    fx_search->setMaximumWidth(150);
    fx_top_layout->addWidget(fx_search);
    fx_outer->addWidget(fx_top_bar);

    auto* fx_body = new QHBoxLayout;
    fx_body->setContentsMargins(0, 0, 0, 0);
    fx_body->setSpacing(0);

    auto* fx_sidebar = new QWidget(fx_browser_panel_);
    fx_sidebar->setObjectName("audioFxSidebar");
    fx_sidebar->setFixedWidth(134);
    auto* fx_side_layout = new QVBoxLayout(fx_sidebar);
    fx_side_layout->setContentsMargins(6, 8, 6, 8);
    fx_side_layout->setSpacing(3);
    auto* cat_toolbox = new QLabel(QStringLiteral("v  Toolbox"), fx_sidebar);
    cat_toolbox->setObjectName("audioFxCategory");
    fx_side_layout->addWidget(cat_toolbox);
    auto* cat_trans = new QLabel(QStringLiteral("    Audio Transitions"), fx_sidebar);
    cat_trans->setObjectName("audioFxCategory");
    fx_side_layout->addWidget(cat_trans);
    auto* cat_audio_fx = new QLabel(QStringLiteral("v  Audio FX"), fx_sidebar);
    cat_audio_fx->setObjectName("audioFxCategoryActive");
    fx_side_layout->addWidget(cat_audio_fx);
    auto* cat_builtin = new QLabel(QStringLiteral("    Built-in FX"), fx_sidebar);
    cat_builtin->setObjectName("audioFxCategory");
    fx_side_layout->addWidget(cat_builtin);
    auto* cat_vst = new QLabel(QStringLiteral("    VST Effects"), fx_sidebar);
    cat_vst->setObjectName("audioFxCategory");
    fx_side_layout->addWidget(cat_vst);
    fx_side_layout->addStretch(1);
    fx_body->addWidget(fx_sidebar);

    class ResponsiveFxScrollArea final : public QScrollArea {
    public:
        explicit ResponsiveFxScrollArea(QWidget* parent = nullptr) : QScrollArea(parent) {
            setWidgetResizable(true);
            setFrameShape(QFrame::NoFrame);
            setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        }
        QGridLayout* grid = nullptr;
        std::vector<std::pair<QString, QFrame*>> cards;
        QString filter;
        int current_cols = 0;

        void reflow(bool force = false) {
            if (grid == nullptr) return;
            const int avail = viewport() ? viewport()->width() : width();
            const int cols = (avail >= 320) ? 2 : 1;
            if (!force && cols == current_cols) return;
            current_cols = cols;
            while (auto* item = grid->takeAt(0)) {
                delete item;
            }
            int visible_idx = 0;
            const QString q = filter.trimmed();
            for (const auto& entry : cards) {
                const bool show = q.isEmpty() || entry.first.contains(q, Qt::CaseInsensitive);
                entry.second->setVisible(show);
                if (show) {
                    grid->addWidget(entry.second, visible_idx / cols, visible_idx % cols);
                    ++visible_idx;
                }
            }
            grid->setColumnStretch(0, 1);
            grid->setColumnStretch(1, cols == 2 ? 1 : 0);
        }

    protected:
        void resizeEvent(QResizeEvent* event) override {
            QScrollArea::resizeEvent(event);
            reflow(false);
        }
    };

    auto* fx_scroll = new ResponsiveFxScrollArea(fx_browser_panel_);
    fx_scroll->setStyleSheet(QStringLiteral("QScrollArea { background: #14161a; border: none; }"));
    auto* fx_content = new QWidget(fx_scroll);
    fx_content->setObjectName("audioFxContent");
    fx_content->setStyleSheet(QStringLiteral("QWidget#audioFxContent { background: #14161a; }"));
    auto* fx_content_layout = new QVBoxLayout(fx_content);
    fx_content_layout->setContentsMargins(8, 8, 8, 8);
    fx_content_layout->setSpacing(6);

    auto* builtin_title = new QLabel(translated("Built-in FX"), fx_content);
    builtin_title->setObjectName("audioFxSectionTitle");
    fx_content_layout->addWidget(builtin_title);

    auto* fx_grid = new QGridLayout;
    fx_grid->setContentsMargins(0, 0, 0, 0);
    fx_grid->setSpacing(5);
    fx_scroll->grid = fx_grid;

    struct FxEntry { const char* badge; const char* name; };
    static constexpr FxEntry kStudioFx[] = {
        {"FX", "Chain FX"}, {"CH", "Chorus"},
        {"SS", "De-Esser"}, {"MM", "De-Hummer"},
        {"DL", "Delay"}, {"DP", "Dialogue Processor"},
        {"DS", "Distortion"}, {"EC", "Echo"},
        {"EQ", "Studio EQ"}, {"FL", "Flanger"},
        {"FS", "Foley Sampler"}, {"FA", "Frequency Analyzer"},
        {"GN", "Gain"}, {"LF", "LFE Filter"},
        {"LM", "Limiter"}, {"LU", "Loudness Meter"},
        {"MT", "Meter"}, {"MD", "Modulation"},
        {"MN", "Monitor"}, {"MB", "Multiband Compressor"},
        {"NR", "Noise Reduction"}, {"PM", "Phase Meter"},
        {"PT", "Pitch"}, {"RV", "Reverb"},
        {"SC", "Soft Clipper"}, {"SF", "Stereo Fixer"},
        {"SW", "Stereo Width"}, {"SA", "Surround Analyzer"},
        {"VC", "Vocal Channel"}
    };
    for (const auto& entry : kStudioFx) {
        auto* card = new QFrame(fx_content);
        card->setObjectName("audioFxCard");
        card->setFixedHeight(26);
        card->setMinimumWidth(0);
        auto* card_layout = new QHBoxLayout(card);
        card_layout->setContentsMargins(0, 0, 6, 0);
        card_layout->setSpacing(0);
        auto* icon = new QLabel(QString::fromLatin1(entry.badge), card);
        icon->setObjectName("audioFxIcon");
        icon->setFixedWidth(30);
        icon->setAlignment(Qt::AlignCenter);
        auto* name = new QLabel(QString::fromLatin1(entry.name), card);
        name->setObjectName("audioFxCardName");
        name->setMinimumWidth(0);
        card_layout->addWidget(icon);
        card_layout->addWidget(name, 1);
        fx_scroll->cards.emplace_back(QString::fromLatin1(entry.name), card);
    }
    fx_scroll->reflow(true);
    fx_content_layout->addLayout(fx_grid);
    fx_content_layout->addStretch(1);
    fx_scroll->setWidget(fx_content);
    fx_body->addWidget(fx_scroll, 1);
    fx_outer->addLayout(fx_body, 1);

    connect(fx_search, &QLineEdit::textChanged, fx_scroll, [fx_scroll](const QString& text) {
        fx_scroll->filter = text;
        fx_scroll->reflow(true);
    });

    // Center meter bank (24-channel console bridge)
    auto* meter_scroll = new QScrollArea(meter_page_);
    meter_scroll->setWidgetResizable(true);
    meter_scroll->setFrameShape(QFrame::NoFrame);
    meter_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* meter_content = new QWidget(meter_scroll);
    meter_content->setStyleSheet(QStringLiteral("background: #14161a;"));
    auto* meter_bank_row = new QHBoxLayout(meter_content);
    meter_bank_row->setContentsMargins(8, 4, 8, 4);
    meter_bank_row->setSpacing(2);
    meter_bank_row->setAlignment(Qt::AlignLeft);

    meter_layout_ = new QHBoxLayout;
    meter_layout_->setContentsMargins(0, 0, 0, 0);
    meter_layout_->setSpacing(2);
    meter_bank_row->addLayout(meter_layout_);

    meter_filler_layout_ = new QHBoxLayout;
    meter_filler_layout_->setContentsMargins(0, 0, 0, 0);
    meter_filler_layout_->setSpacing(2);
    meter_bank_row->addLayout(meter_filler_layout_);
    meter_bank_row->addStretch(1);

    meter_scroll->setWidget(meter_content);
    body_layout->addWidget(meter_scroll, 1);

    // Right studio monitoring bay: Master + Control Room + Loudness
    auto* monitor_bay = new QWidget(meter_page_);
    monitor_bay->setStyleSheet(QStringLiteral("background: #16181c; border-left: 1px solid #262a33;"));
    auto* monitor_layout = new QHBoxLayout(monitor_bay);
    monitor_layout->setContentsMargins(0, 0, 0, 0);
    monitor_layout->setSpacing(0);

    auto* master_col = new QWidget(monitor_bay);
    master_col->setStyleSheet(QStringLiteral("background: #16181c; border-right: 1px solid #262a33;"));
    auto* master_col_layout = new QHBoxLayout(master_col);
    master_col_layout->setContentsMargins(6, 4, 6, 4);
    master_meter_ = createMeter(master_col, "audioOutputMeter", 12);
    auto* master_slot = createMeterSlot(
        master_col, QStringLiteral("Master"), translated("Master"), master_meter_, true, QStringLiteral("#3b82c4"));
    master_slot->setFixedWidth(52);
    master_col_layout->addWidget(master_slot);
    monitor_layout->addWidget(master_col);

    auto* control_room = new QWidget(monitor_bay);
    control_room->setObjectName("audioControlRoomCard");
    control_room->setFixedWidth(82);
    control_room->setStyleSheet(QStringLiteral("QWidget#audioControlRoomCard { background: #16181c; border-right: 1px solid #262a33; }"));
    auto* cr_layout = new QVBoxLayout(control_room);
    cr_layout->setContentsMargins(6, 5, 6, 6);
    cr_layout->setSpacing(4);
    auto* cr_title = new QLabel(translated("Control Room"), control_room);
    cr_title->setObjectName("audioSectionTitle");
    cr_title->setAlignment(Qt::AlignCenter);
    cr_layout->addWidget(cr_title);
    auto* cr_sep = new QFrame(control_room);
    cr_sep->setFixedHeight(1);
    cr_sep->setStyleSheet(QStringLiteral("background: #262a33;"));
    cr_layout->addWidget(cr_sep);

    auto* tp_row = new QHBoxLayout;
    tp_row->setSpacing(4);
    auto* tp_lbl = new QLabel(QStringLiteral("TP"), control_room);
    tp_lbl->setStyleSheet(QStringLiteral("color: #cfd6e0; font-size: 10px; font-weight: 600;"));
    tp_value_label_ = new QLabel(QStringLiteral("-100"), control_room);
    tp_value_label_->setStyleSheet(QStringLiteral("color: #4f94d4; font-size: 10px; font-weight: 600;"));
    tp_row->addStretch(1);
    tp_row->addWidget(tp_lbl);
    tp_row->addWidget(tp_value_label_);
    tp_row->addStretch(1);
    cr_layout->addLayout(tp_row);

    auto* cr_bar_row = new QHBoxLayout;
    cr_bar_row->setSpacing(2);
    cr_bar_row->addWidget(new DbScaleWidget(true, control_room));
    control_room_meter_ = createMeter(control_room, "audioControlRoomMeter", 10);
    cr_bar_row->addWidget(control_room_meter_);
    cr_bar_row->addWidget(new DbScaleWidget(false, control_room));
    cr_layout->addLayout(cr_bar_row, 1);
    monitor_layout->addWidget(control_room);

    auto* loudness_card = new QWidget(monitor_bay);
    loudness_card->setObjectName("audioLoudnessCard");
    loudness_card->setFixedWidth(182);
    loudness_card->setStyleSheet(QStringLiteral("QWidget#audioLoudnessCard { background: #16181c; }"));
    auto* ld_layout = new QVBoxLayout(loudness_card);
    ld_layout->setContentsMargins(8, 5, 8, 6);
    ld_layout->setSpacing(5);
    auto* ld_header = new QHBoxLayout;
    auto* ld_title = new QLabel(translated("Loudness"), loudness_card);
    ld_title->setObjectName("audioSectionTitle");
    auto* ld_std = new QLabel(QStringLiteral("BS.1770-1 (LU)"), loudness_card);
    ld_std->setStyleSheet(QStringLiteral("color: #798294; font-size: 9px;"));
    ld_header->addWidget(ld_title);
    ld_header->addStretch(1);
    ld_header->addWidget(ld_std);
    ld_layout->addLayout(ld_header);

    auto* ld_sep = new QFrame(loudness_card);
    ld_sep->setFixedHeight(1);
    ld_sep->setStyleSheet(QStringLiteral("background: #262a33;"));
    ld_layout->addWidget(ld_sep);

    auto* ld_body = new QHBoxLayout;
    ld_body->setSpacing(8);
    auto* ld_meter = new LoudnessScaleWidget(loudness_card);
    loudness_meter_widget_ = ld_meter;
    ld_body->addWidget(ld_meter);
    auto* ld_stats = new QVBoxLayout;
    ld_stats->setSpacing(2);
    auto add_stat = [&](const QString& title) -> QLabel* {
        auto* t = new QLabel(title, loudness_card);
        t->setStyleSheet(QStringLiteral("color: #8f98a8; font-size: 9px;"));
        auto* v = new QLabel(QStringLiteral("--"), loudness_card);
        v->setStyleSheet(QStringLiteral("color: #4f94d4; font-size: 9px; font-weight: 600;"));
        ld_stats->addWidget(t);
        ld_stats->addWidget(v);
        return v;
    };
    loudness_short_value_ = add_stat(translated("Short"));
    loudness_short_max_value_ = add_stat(translated("Short Max"));
    loudness_range_value_ = add_stat(translated("Range"));
    loudness_integrated_value_ = add_stat(translated("Integrated"));
    ld_stats->addStretch(1);
    ld_body->addLayout(ld_stats, 1);
    ld_layout->addLayout(ld_body, 1);

    auto* ld_sep2 = new QFrame(loudness_card);
    ld_sep2->setFixedHeight(1);
    ld_sep2->setStyleSheet(QStringLiteral("background: #262a33;"));
    ld_layout->addWidget(ld_sep2);

    auto* ld_btns = new QHBoxLayout;
    ld_btns->setSpacing(6);
    auto* ld_pause = new QPushButton(translated("Pause"), loudness_card);
    ld_pause->setCheckable(true);
    ld_pause->setStyleSheet(QStringLiteral(
        "QPushButton { border-radius: 10px; padding: 3px 12px; }"
        "QPushButton:checked { background: #285685; border-color: #4f88c2; color: #ffffff; }"));
    auto* ld_reset = new QPushButton(translated("Reset"), loudness_card);
    ld_reset->setStyleSheet(QStringLiteral(
        "QPushButton { border-radius: 10px; padding: 3px 12px; }"));
    ld_btns->addWidget(ld_pause);
    ld_btns->addWidget(ld_reset);
    ld_layout->addLayout(ld_btns);
    monitor_layout->addWidget(loudness_card);

    connect(ld_pause, &QPushButton::toggled, this, [this](bool paused) {
        loudness_paused_ = paused;
    });
    connect(ld_reset, &QPushButton::clicked, this, [this] {
        loudness_short_max_lu_ = -100.0F;
        loudness_min_lu_ = 0.0F;
        loudness_max_lu_ = -100.0F;
        loudness_sum_ = 0.0;
        loudness_samples_ = 0;
        if (tp_value_label_) tp_value_label_->setText(QStringLiteral("-100"));
        if (loudness_short_value_) loudness_short_value_->setText(QStringLiteral("--"));
        if (loudness_short_max_value_) loudness_short_max_value_->setText(QStringLiteral("--"));
        if (loudness_range_value_) loudness_range_value_->setText(QStringLiteral("--"));
        if (loudness_integrated_value_) loudness_integrated_value_->setText(QStringLiteral("--"));
        if (auto* w = static_cast<LoudnessScaleWidget*>(loudness_meter_widget_)) {
            w->setLevels(0.0F, 0.0F);
        }
    });

    body_layout->addWidget(monitor_bay);
    meter_outer->addLayout(body_layout, 1);

    // Bottom transport / timecode bar
    auto* transport_bar = new QFrame(meter_page_);
    transport_bar->setObjectName("audioTransportBar");
    transport_bar->setFixedHeight(30);
    auto* tr_layout = new QHBoxLayout(transport_bar);
    tr_layout->setContentsMargins(10, 3, 10, 3);
    tr_layout->setSpacing(12);
    auto* tc_label = new QLabel(QStringLiteral("00:00:00:00"), transport_bar);
    tc_label->setObjectName("audioTimecodeDisplay");
    tr_layout->addWidget(tc_label);
    auto* tl_pill = new QLabel(translated("Timeline 1"), transport_bar);
    tl_pill->setStyleSheet(QStringLiteral(
        "background: #22252d; color: #aeb6c4; border: 1px solid #303540; "
        "border-radius: 3px; padding: 2px 8px; font-size: 11px;"));
    tr_layout->addWidget(tl_pill);
    tr_layout->addStretch(1);
    auto* bus_route_hint = new QLabel(QStringLiteral("Master  •  48 kHz"), transport_bar);
    bus_route_hint->setStyleSheet(QStringLiteral("color: #7c8596; font-size: 10px;"));
    tr_layout->addWidget(bus_route_hint);
    meter_outer->addWidget(transport_bar);

    // Console Mixer Panel (right dock)
    mixer_panel_ = new QWidget(parent);
    mixer_panel_->setObjectName("audioMixerPanel");
    mixer_panel_->setStyleSheet(QStringLiteral(R"(
        QWidget#audioMixerPanel { background: #16181c; color: #cfd6e0; }
        QFrame#audioMixerHeader {
            background: #1b1d22; border-bottom: 1px solid #282c34;
        }
        QFrame#audioTrackMixerCard, QFrame#audioBusMixerCard, QFrame#audioOutputMixerCard {
            background: #191b20; border: 1px solid #282c34; border-radius: 3px;
        }
        QLabel#audioStripName {
            color: #d8dee9; font-size: 11px; font-weight: 600;
        }
        QLabel#audioStripSubLabel {
            color: #7e8796; font-size: 9px;
        }
        QLabel#audioGainReadout {
            color: #c5ccd8; font-family: Consolas, "Courier New", monospace;
            font-size: 10px; background: #111317; border: 1px solid #262a33;
            border-radius: 2px; padding: 1px 3px;
        }
        QWidget#audioMixerPanel QPushButton,
        QWidget#audioMixerPanel QComboBox {
            background: #232730; color: #cfd6e0; border: 1px solid #323743;
            border-radius: 2px; padding: 2px 5px; font-size: 10px;
        }
        QWidget#audioMixerPanel QPushButton:hover { background: #2d323e; }
        QCheckBox#audioTrackMute, QCheckBox#audioBusMute, QCheckBox#audioMasterMute,
        QCheckBox#audioTrackSolo, QCheckBox#audioBusSolo {
            background: #252830; color: #b7c0ce; border: 1px solid #353a46;
            border-radius: 2px; padding: 2px 5px; font-size: 10px; font-weight: 700;
            min-width: 22px; min-height: 18px;
        }
        QCheckBox#audioTrackMute::indicator, QCheckBox#audioBusMute::indicator,
        QCheckBox#audioMasterMute::indicator, QCheckBox#audioTrackSolo::indicator,
        QCheckBox#audioBusSolo::indicator {
            width: 0px; height: 0px;
        }
        QCheckBox#audioTrackMute:checked, QCheckBox#audioBusMute:checked,
        QCheckBox#audioMasterMute:checked {
            background: #a83232; border-color: #d14b4b; color: #ffffff;
        }
        QCheckBox#audioTrackSolo:checked, QCheckBox#audioBusSolo:checked {
            background: #b8821c; border-color: #e0a736; color: #ffffff;
        }
        QSlider::groove:vertical {
            background: #0d0f13; border: 1px solid #252932; width: 4px; border-radius: 2px;
        }
        QSlider::handle:vertical {
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                stop:0 #778091, stop:0.5 #4b525f, stop:1 #383d47);
            border: 1px solid #8f98a8; height: 12px; width: 16px;
            margin: 0 -7px; border-radius: 2px;
        }
        QSlider::groove:horizontal {
            background: #0d0f13; border: 1px solid #252932; height: 4px; border-radius: 2px;
        }
        QSlider::handle:horizontal {
            background: #778091; border: 1px solid #8f98a8;
            width: 8px; height: 10px; margin: -4px 0; border-radius: 2px;
        }
    )"));

    mixer_layout_ = new QVBoxLayout(mixer_panel_);
    mixer_layout_->setContentsMargins(0, 0, 0, 0);
    mixer_layout_->setSpacing(0);

    // Mixer Header
    auto* mixer_header = new QFrame(mixer_panel_);
    mixer_header->setObjectName("audioMixerHeader");
    mixer_header->setFixedHeight(32);
    auto* mixer_header_layout = new QHBoxLayout(mixer_header);
    mixer_header_layout->setContentsMargins(8, 3, 8, 3);
    mixer_header_layout->setSpacing(6);
    auto* mixer_title = new QLabel(translated("Mixer"), mixer_header);
    mixer_title->setObjectName("audioSectionTitle");
    mixer_header_layout->addWidget(mixer_title);
    mixer_header_layout->addStretch(1);
    auto* clip_fx_toggle = new QPushButton(translated("Clip / Isolation"), mixer_header);
    clip_fx_toggle->setCheckable(true);
    clip_fx_toggle->setChecked(false);
    mixer_header_layout->addWidget(clip_fx_toggle);
    add_bus_button_ = new QPushButton(translated("Add bus"), mixer_header);
    add_bus_button_->setObjectName("audioAddBus");
    mixer_header_layout->addWidget(add_bus_button_);
    mixer_layout_->addWidget(mixer_header);

    connect(add_bus_button_, &QPushButton::clicked, this, [this] {
        bool accepted = false;
        const auto name = QInputDialog::getText(
            mixer_panel_, translated("Add bus"), translated("Bus name"),
            QLineEdit::Normal, translated("Submix"), &accepted);
        if (accepted && !name.trimmed().isEmpty())
            emit createBusRequested(name.trimmed());
    });

    // Horizontal Console Strip Area
    auto* console_scroll = new QScrollArea(mixer_panel_);
    console_scroll->setWidgetResizable(true);
    console_scroll->setFrameShape(QFrame::NoFrame);
    console_scroll->setStyleSheet(QStringLiteral("background: #14161a; border: none;"));
    auto* console_content = new QWidget(console_scroll);
    console_content->setStyleSheet(QStringLiteral("background: #14161a;"));
    auto* console_row = new QHBoxLayout(console_content);
    console_row->setContentsMargins(0, 0, 4, 0);
    console_row->setSpacing(0);
    console_row->setAlignment(Qt::AlignLeft);

    // Left Row-Labels Column (aligns with every channel strip row)
    auto* label_col = new QFrame(console_content);
    label_col->setFixedWidth(68);
    label_col->setStyleSheet(QStringLiteral(
        "QFrame { background: #16181c; border-right: 1px solid #262a33; }"
        "QLabel { color: #7e8796; font-size: 10px; border: none; }"));
    auto* lbl_layout = new QVBoxLayout(label_col);
    lbl_layout->setContentsMargins(4, 4, 6, 6);
    lbl_layout->setSpacing(4);
    auto add_row_label = [&](const QString& text, int height) {
        auto* l = new QLabel(text, label_col);
        l->setFixedHeight(height);
        l->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        lbl_layout->addWidget(l);
    };
    add_row_label(QString(), 24);  // header
    add_row_label(translated("Input"), 22);
    add_row_label(translated("Track FX"), 40);
    add_row_label(translated("Order"), 20);
    add_row_label(translated("Effects"), 20);
    add_row_label(translated("Effects In"), 20);
    add_row_label(translated("Dynamics"), 42);
    add_row_label(translated("EQ"), 28);
    add_row_label(translated("Bus Sends"), 22);
    add_row_label(translated("Pan"), 26);
    lbl_layout->addStretch(1);
    console_row->addWidget(label_col);

    track_mixer_layout_ = new QHBoxLayout;
    track_mixer_layout_->setContentsMargins(0, 0, 0, 0);
    track_mixer_layout_->setSpacing(0);
    console_row->addLayout(track_mixer_layout_);

    bus_mixer_layout_ = new QHBoxLayout;
    bus_mixer_layout_->setContentsMargins(0, 0, 0, 0);
    bus_mixer_layout_->setSpacing(0);
    console_row->addLayout(bus_mixer_layout_);

    // Master Channel Strip
    auto* output_card = new QFrame(console_content);
    output_card->setObjectName("audioOutputMixerCard");
    output_card->setFixedWidth(84);
    auto* output_layout = new QVBoxLayout(output_card);
    output_layout->setContentsMargins(4, 4, 4, 6);
    output_layout->setSpacing(4);

    auto* master_hdr = new QWidget(output_card);
    master_hdr->setFixedHeight(24);
    auto* master_hdr_layout = new QVBoxLayout(master_hdr);
    master_hdr_layout->setContentsMargins(0, 0, 0, 0);
    master_hdr_layout->setSpacing(2);
    auto* master_accent = new QFrame(master_hdr);
    master_accent->setFixedHeight(3);
    master_accent->setStyleSheet(QStringLiteral("background: #3b82c4; border-radius: 1px;"));
    master_hdr_layout->addWidget(master_accent);
    auto* master_id = new QLabel(QStringLiteral("M"), master_hdr);
    master_id->setObjectName("audioStripName");
    master_id->setAlignment(Qt::AlignCenter);
    master_hdr_layout->addWidget(master_id);
    output_layout->addWidget(master_hdr);

    auto* master_input_sp = new QFrame(output_card);
    master_input_sp->setFixedHeight(22);
    master_input_sp->setStyleSheet(QStringLiteral("background: #14161a; border: 1px solid #262a33;"));
    output_layout->addWidget(master_input_sp);

    auto* master_fx_sp = new QWidget(output_card);
    master_fx_sp->setFixedHeight(40);
    output_layout->addWidget(master_fx_sp);

    auto* master_order = new QLabel(
        QStringLiteral("<span style='color:#d48f2a'>FX</span> <span style='color:#9cb048'>DY</span> <span style='color:#3bb2c7'>EQ</span>"),
        output_card);
    master_order->setFixedHeight(20);
    master_order->setAlignment(Qt::AlignCenter);
    master_order->setStyleSheet(QStringLiteral("background: #121418; border: 1px solid #262a33; font-size: 9px; font-weight: 700;"));
    output_layout->addWidget(master_order);

    auto* master_fx_plus = new QPushButton(QStringLiteral("+"), output_card);
    master_fx_plus->setFixedHeight(20);
    output_layout->addWidget(master_fx_plus);

    auto* master_fx_in = new QPushButton(QStringLiteral("In"), output_card);
    master_fx_in->setCheckable(true);
    master_fx_in->setFixedHeight(20);
    output_layout->addWidget(master_fx_in);

    auto* master_dyn = new MiniCurveWidget(MiniCurveWidget::Kind::Dynamics, output_card);
    master_dyn->setFixedHeight(42);
    output_layout->addWidget(master_dyn);
    auto* master_eq = new MiniCurveWidget(MiniCurveWidget::Kind::Eq, output_card);
    master_eq->setFixedHeight(28);
    output_layout->addWidget(master_eq);

    auto* master_sends_sp = new QWidget(output_card);
    master_sends_sp->setFixedHeight(22);
    output_layout->addWidget(master_sends_sp);

    auto* master_pan_sp = new QWidget(output_card);
    master_pan_sp->setFixedHeight(26);
    output_layout->addWidget(master_pan_sp);

    auto* master_sep = new QFrame(output_card);
    master_sep->setFixedHeight(1);
    master_sep->setStyleSheet(QStringLiteral("background: #2b2f38;"));
    output_layout->addWidget(master_sep);

    auto* master_name = new QLabel(translated("Master"), output_card);
    master_name->setStyleSheet(QStringLiteral("color: #cfd6e0; font-size: 10px; font-weight: 600;"));
    master_name->setAlignment(Qt::AlignCenter);
    output_layout->addWidget(master_name);

    master_mute_ = new QCheckBox(QStringLiteral("M"), output_card);
    master_mute_->setObjectName("audioMasterMute");
    master_mute_->setToolTip(translated("Mute"));
    output_layout->addWidget(master_mute_, 0, Qt::AlignCenter);

    auto* master_auto = new QLabel(QStringLiteral("~"), output_card);
    master_auto->setAlignment(Qt::AlignCenter);
    master_auto->setStyleSheet(QStringLiteral("color: #7c8596; font-size: 12px;"));
    output_layout->addWidget(master_auto);

    master_gain_value_ = new QLabel(gainText(100), output_card);
    master_gain_value_->setObjectName("audioGainReadout");
    master_gain_value_->setAlignment(Qt::AlignCenter);
    output_layout->addWidget(master_gain_value_);

    auto* master_fader_row = new QHBoxLayout;
    master_fader_row->setSpacing(2);
    master_gain_ = createGainSlider(output_card, "audioMasterGain");
    master_fader_row->addWidget(master_gain_);
    master_fader_row->addWidget(new DbScaleWidget(true, output_card));
    mixer_master_meter_ = createMeter(output_card, "audioOutputMixerMeter", 9);
    master_fader_row->addWidget(mixer_master_meter_);
    output_layout->addLayout(master_fader_row, 1);

    console_row->addWidget(output_card);
    console_row->addStretch(1);
    console_scroll->setWidget(console_content);
    mixer_layout_->addWidget(console_scroll, 1);

    connect(master_gain_, &QSlider::sliderPressed, this,
            &AudioWorkspace::gainEditStarted);
    connect(master_gain_, &QSlider::valueChanged, this, [this](int value) {
        master_gain_value_->setText(gainText(value));
        emit masterMixChanged(value / 100.0, master_mute_->isChecked());
    });
    connect(master_gain_, &QSlider::sliderReleased, this,
            &AudioWorkspace::gainEditFinished);
    connect(master_mute_, &QCheckBox::toggled, this, [this](bool muted) {
        emit masterMixChanged(master_gain_->value() / 100.0, muted);
    });

    // Collapsible Clip & Track FX / Voice Isolation Drawer
    clip_fx_drawer_ = new QWidget(mixer_panel_);
    clip_fx_drawer_->setObjectName("audioClipFxDrawer");
    clip_fx_drawer_->setStyleSheet(QStringLiteral(
        "QWidget#audioClipFxDrawer { background: #181a1f; border-top: 1px solid #282c34; }"));
    auto* drawer_scroll = new QScrollArea(clip_fx_drawer_);
    drawer_scroll->setWidgetResizable(true);
    drawer_scroll->setFrameShape(QFrame::NoFrame);
    drawer_scroll->setMaximumHeight(190);
    auto* drawer_content = new QWidget(drawer_scroll);
    auto* drawer_layout = new QVBoxLayout(drawer_content);
    drawer_layout->setContentsMargins(8, 6, 8, 6);
    drawer_layout->setSpacing(6);

    selected_clip_effects_ = new QPushButton(
        translated("Select an audio clip to edit its effects"), drawer_content);
    selected_clip_effects_->setObjectName("audioSelectedClipEffects");
    selected_clip_effects_->setEnabled(false);
    drawer_layout->addWidget(selected_clip_effects_);
    connect(selected_clip_effects_, &QPushButton::clicked, this, [this] {
        if (selected_clip_id_ == 0) return;
        const auto id = selected_clip_id_;
        AudioEffectChainDialog dialog(
            translated("Clip audio effects — %1").arg(selected_clip_name_),
            selected_clip_chain_, mixer_panel_);
        if (dialog.exec() == QDialog::Accepted)
            emit clipAudioEffectsChanged(id, dialog.effects());
    });

    selected_clip_isolation_ = new VoiceIsolationPanel(drawer_content);
    selected_clip_isolation_->setObjectName("audioSelectedClipIsolation");
    selected_clip_isolation_->setEnabled(selected_clip_id_ != 0);
    drawer_layout->addWidget(selected_clip_isolation_);
    selected_clip_isolation_->apply = [this](auto settings) {
        if (selected_clip_id_) emit voiceIsolationChanged(true, selected_clip_id_, std::move(settings));
    };
    selected_clip_isolation_->regenerate = [this] {
        if (selected_clip_id_) emit voiceIsolationRegenerate(true, selected_clip_id_);
    };
    selected_clip_isolation_->cancel = [this] { emit voiceIsolationCancel(); };

    auto* effects_section = new QWidget(drawer_content);
    effects_section->setObjectName("audioTrackEffectsSection");
    auto* effects_section_layout = new QVBoxLayout(effects_section);
    effects_section_layout->setContentsMargins(0, 0, 0, 0);
    effects_section_layout->addWidget(
        new QLabel(translated("Track audio effects"), effects_section));
    track_effects_layout_ = new QVBoxLayout;
    effects_section_layout->addLayout(track_effects_layout_);
    drawer_layout->addWidget(effects_section);

    drawer_scroll->setWidget(drawer_content);
    auto* drawer_outer = new QVBoxLayout(clip_fx_drawer_);
    drawer_outer->setContentsMargins(0, 0, 0, 0);
    drawer_outer->addWidget(drawer_scroll);
    clip_fx_drawer_->setVisible(false);
    mixer_layout_->addWidget(clip_fx_drawer_);
    connect(clip_fx_toggle, &QPushButton::toggled, clip_fx_drawer_, &QWidget::setVisible);
}

void AudioWorkspace::refreshTracks(
    const std::vector<timeline::TimelineTrack>& tracks,
    const std::vector<timeline::AudioBus>& buses,
    const timeline::MasterAudioMix& master) {
    if (meter_page_ == nullptr || mixer_panel_ == nullptr) return;
    ducking_sources_.clear();
    for (const auto& track : tracks) ducking_sources_.emplace_back(track.track_id, QString::fromUtf8(track.name.c_str()));
    std::vector<const timeline::TimelineTrack*> audio_tracks;
    for (const auto& track : tracks) {
        if (track.kind == timeline::TrackKind::Audio) audio_tracks.push_back(&track);
    }

    const bool same_tracks = audio_tracks.size() == strips_.size() &&
        std::equal(audio_tracks.begin(), audio_tracks.end(), strips_.begin(),
            [](const auto* track, const Strip& strip) {
                return track->track_id == strip.id;
            });
    if (!same_tracks) {
        for (auto& strip : strips_) {
            delete strip.meter_card;
            delete strip.mixer_card;
        }
        strips_.clear();
        int track_idx = 0;
        for (const auto* track : audio_tracks) {
            ++track_idx;
            Strip strip;
            strip.id = track->track_id;
            strip.meter = createMeter(meter_page_, "audioTrackMeter", 7);
            strip.meter_card = createMeterSlot(
                meter_page_, QString::number(track_idx),
                QString::fromUtf8(track->name.c_str()), strip.meter, track_idx == 1,
                QStringLiteral("#39ad77"));
            strip.meter_name = strip.meter_card->findChild<QLabel*>("audioMeterBadge");
            meter_layout_->addWidget(strip.meter_card);

            strip.mixer_card = new QFrame(mixer_panel_);
            strip.mixer_card->setObjectName("audioTrackMixerCard");
            strip.mixer_card->setFixedWidth(84);
            auto* layout = new QVBoxLayout(strip.mixer_card);
            layout->setContentsMargins(4, 4, 4, 6);
            layout->setSpacing(4);

            auto* hdr = new QWidget(strip.mixer_card);
            hdr->setFixedHeight(24);
            auto* hdr_layout = new QVBoxLayout(hdr);
            hdr_layout->setContentsMargins(0, 0, 0, 0);
            hdr_layout->setSpacing(2);
            auto* accent = new QFrame(hdr);
            accent->setFixedHeight(3);
            accent->setStyleSheet(QStringLiteral("background: #39ad77; border-radius: 1px;"));
            hdr_layout->addWidget(accent);
            auto* short_id = new QLabel(QStringLiteral("A%1").arg(track_idx), hdr);
            short_id->setObjectName("audioStripName");
            short_id->setAlignment(Qt::AlignCenter);
            hdr_layout->addWidget(short_id);
            layout->addWidget(hdr);

            strip.output = new QComboBox(strip.mixer_card);
            strip.output->setObjectName("audioTrackOutput");
            strip.output->setFixedHeight(22);
            strip.output->setToolTip(translated("Output"));
            layout->addWidget(strip.output);

            auto* track_fx_box = new QWidget(strip.mixer_card);
            track_fx_box->setFixedHeight(40);
            auto* tfx_layout = new QVBoxLayout(track_fx_box);
            tfx_layout->setContentsMargins(0, 0, 0, 0);
            tfx_layout->setSpacing(2);
            auto* voice_iso_pill = new QPushButton(QStringLiteral("Voice Iso"), track_fx_box);
            voice_iso_pill->setCheckable(true);
            voice_iso_pill->setFixedHeight(19);
            auto* dial_lev_pill = new QPushButton(QStringLiteral("Dial Lev"), track_fx_box);
            dial_lev_pill->setCheckable(true);
            dial_lev_pill->setFixedHeight(19);
            tfx_layout->addWidget(voice_iso_pill);
            tfx_layout->addWidget(dial_lev_pill);
            layout->addWidget(track_fx_box);

            auto* order_label = new QLabel(
                QStringLiteral("<span style='color:#d48f2a'>FX</span> <span style='color:#9cb048'>DY</span> <span style='color:#3bb2c7'>EQ</span>"),
                strip.mixer_card);
            order_label->setFixedHeight(20);
            order_label->setAlignment(Qt::AlignCenter);
            order_label->setStyleSheet(QStringLiteral("background: #121418; border: 1px solid #262a33; font-size: 9px; font-weight: 700;"));
            layout->addWidget(order_label);

            auto* fx_btn = new QPushButton(QStringLiteral("+"), strip.mixer_card);
            fx_btn->setFixedHeight(20);
            fx_btn->setToolTip(translated("Track audio effects"));
            layout->addWidget(fx_btn);
            connect(fx_btn, &QPushButton::clicked, this, [this, id = strip.id] {
                for (const auto& row : track_effects_rows_) {
                    if (row.id == id && row.edit != nullptr) {
                        row.edit->click();
                        break;
                    }
                }
            });

            auto* fx_in_btn = new QPushButton(QStringLiteral("In"), strip.mixer_card);
            fx_in_btn->setCheckable(true);
            fx_in_btn->setFixedHeight(20);
            layout->addWidget(fx_in_btn);

            auto* dyn_w = new MiniCurveWidget(MiniCurveWidget::Kind::Dynamics, strip.mixer_card);
            dyn_w->setFixedHeight(42);
            layout->addWidget(dyn_w);
            auto* eq_w = new MiniCurveWidget(MiniCurveWidget::Kind::Eq, strip.mixer_card);
            eq_w->setFixedHeight(28);
            layout->addWidget(eq_w);

            auto* sends_header = new QHBoxLayout;
            sends_header->setSpacing(2);
            strip.send_destination = new QComboBox(strip.mixer_card);
            strip.send_destination->setObjectName("audioTrackSendDestination");
            strip.send_destination->setFixedHeight(22);
            strip.send_destination->setToolTip(translated("Sends"));
            sends_header->addWidget(strip.send_destination, 1);
            strip.add_send = new QPushButton(QStringLiteral("+"), strip.mixer_card);
            strip.add_send->setObjectName("audioTrackAddSend");
            strip.add_send->setFixedSize(20, 22);
            strip.add_send->setToolTip(translated("Add send"));
            sends_header->addWidget(strip.add_send);
            layout->addLayout(sends_header);
            strip.sends_layout = new QVBoxLayout;
            strip.sends_layout->setSpacing(2);
            layout->addLayout(strip.sends_layout);

            auto* pan_box = new QWidget(strip.mixer_card);
            pan_box->setFixedHeight(26);
            auto* pan_layout = new QVBoxLayout(pan_box);
            pan_layout->setContentsMargins(0, 0, 0, 0);
            pan_layout->setSpacing(1);
            strip.pan = createPanSlider(pan_box, "audioTrackPan");
            strip.pan_value = new QLabel(pan_box);
            strip.pan_value->setObjectName("audioStripSubLabel");
            strip.pan_value->setAlignment(Qt::AlignCenter);
            pan_layout->addWidget(strip.pan);
            pan_layout->addWidget(strip.pan_value);
            layout->addWidget(pan_box);

            auto* sep = new QFrame(strip.mixer_card);
            sep->setFixedHeight(1);
            sep->setStyleSheet(QStringLiteral("background: #2b2f38;"));
            layout->addWidget(sep);

            strip.mixer_name = new QLabel(strip.mixer_card);
            strip.mixer_name->setStyleSheet(QStringLiteral("color: #d4a72c; font-size: 10px; font-weight: 600;"));
            strip.mixer_name->setAlignment(Qt::AlignCenter);
            layout->addWidget(strip.mixer_name);

            auto* rec_btn = new QPushButton(QStringLiteral("R"), strip.mixer_card);
            rec_btn->setCheckable(true);
            rec_btn->setFixedSize(22, 18);
            strip.solo = new QCheckBox(QStringLiteral("S"), strip.mixer_card);
            strip.solo->setObjectName("audioTrackSolo");
            strip.solo->setToolTip(translated("Solo"));
            strip.mute = new QCheckBox(QStringLiteral("M"), strip.mixer_card);
            strip.mute->setObjectName("audioTrackMute");
            strip.mute->setToolTip(translated("Mute"));
            auto* toggle_row = new QHBoxLayout;
            toggle_row->setSpacing(3);
            toggle_row->addStretch(1);
            toggle_row->addWidget(rec_btn);
            toggle_row->addWidget(strip.solo);
            toggle_row->addWidget(strip.mute);
            toggle_row->addStretch(1);
            layout->addLayout(toggle_row);

            auto* auto_icon = new QLabel(QStringLiteral("~"), strip.mixer_card);
            auto_icon->setAlignment(Qt::AlignCenter);
            auto_icon->setStyleSheet(QStringLiteral("color: #7c8596; font-size: 12px;"));
            layout->addWidget(auto_icon);

            strip.gain_value = new QLabel(strip.mixer_card);
            strip.gain_value->setObjectName("audioGainReadout");
            strip.gain_value->setAlignment(Qt::AlignCenter);
            layout->addWidget(strip.gain_value);

            auto* fader_row = new QHBoxLayout;
            fader_row->setSpacing(2);
            strip.gain = createGainSlider(strip.mixer_card, "audioTrackGain");
            fader_row->addWidget(strip.gain);
            fader_row->addWidget(new DbScaleWidget(true, strip.mixer_card));
            strip.mixer_meter = createMeter(strip.mixer_card, "audioTrackMixerMeter", 9);
            fader_row->addWidget(strip.mixer_meter);
            layout->addLayout(fader_row, 1);

            connect(strip.gain, &QSlider::sliderPressed, this,
                    &AudioWorkspace::gainEditStarted);
            connect(strip.gain, &QSlider::valueChanged, this,
                [this, id = strip.id](int value) {
                    const auto found = std::find_if(strips_.begin(), strips_.end(),
                        [id](const Strip& item) { return item.id == id; });
                    if (found == strips_.end()) return;
                    found->gain_value->setText(gainText(value));
                    emitTrackMix(id);
                });
            connect(strip.gain, &QSlider::sliderReleased, this,
                    &AudioWorkspace::gainEditFinished);
            connect(strip.pan, &QSlider::sliderPressed, this,
                    &AudioWorkspace::gainEditStarted);
            connect(strip.pan, &QSlider::valueChanged, this,
                [this, id = strip.id](int value) {
                    const auto found = std::find_if(strips_.begin(), strips_.end(),
                        [id](const Strip& item) { return item.id == id; });
                    if (found == strips_.end()) return;
                    found->pan_value->setText(panText(value));
                    emitTrackMix(id);
                });
            connect(strip.pan, &QSlider::sliderReleased, this,
                    &AudioWorkspace::gainEditFinished);
            connect(strip.mute, &QCheckBox::toggled, this,
                    [this, id = strip.id](bool) { emitTrackMix(id); });
            connect(strip.solo, &QCheckBox::toggled, this,
                    [this, id = strip.id](bool checked) {
                        auto& ids = solo_track_ids_;
                        ids.erase(std::remove(ids.begin(), ids.end(), id), ids.end());
                        if (checked) ids.push_back(id);
                        emitSoloState();
                    });
            connect(strip.output, qOverload<int>(&QComboBox::currentIndexChanged),
                    this, [this, id = strip.id](int) { emitTrackMix(id); });
            connect(strip.add_send, &QPushButton::clicked, this,
                    [this, id = strip.id] {
                        const auto found = std::find_if(strips_.begin(), strips_.end(),
                            [id](const Strip& item) { return item.id == id; });
                        if (found == strips_.end() || found->send_destination->currentIndex() < 0)
                            return;
                        const auto bus_id = found->send_destination->currentData().toULongLong();
                        const auto existing = std::find_if(found->sends.begin(), found->sends.end(),
                            [bus_id](const SendRow& row) { return row.bus_id == bus_id; });
                        if (existing != found->sends.end()) return;
                        const auto output_value = found->output->currentData().toULongLong();
                        const auto output = output_value == 0 ? std::nullopt :
                            std::optional<timeline::AudioBusId>{
                                static_cast<timeline::AudioBusId>(output_value)};
                        std::vector<timeline::AudioSend> sends;
                        for (const auto& row : found->sends) {
                            sends.push_back(timeline::AudioSend{
                                static_cast<timeline::AudioBusId>(
                                    row.destination->currentData().toULongLong()),
                                row.gain->value() / 100.0,
                                row.pre_fader->isChecked()
                                    ? timeline::AudioSendTap::PreFader
                                    : timeline::AudioSendTap::PostFader});
                        }
                        if (!output.has_value() || *output != bus_id) {
                            sends.push_back(timeline::AudioSend{bus_id, 1.0,
                                timeline::AudioSendTap::PostFader});
                            emit trackMixChanged(id, found->gain->value() / 100.0,
                                found->mute->isChecked(), found->pan->value() / 100.0,
                                output, std::move(sends));
                        }
                    });
            track_mixer_layout_->addWidget(strip.mixer_card);
            strips_.push_back(std::move(strip));
        }
    }

    std::vector<const timeline::AudioBus*> audio_buses;
    for (const auto& bus : buses) audio_buses.push_back(&bus);
    const bool same_buses = audio_buses.size() == bus_strips_.size() &&
        std::equal(audio_buses.begin(), audio_buses.end(), bus_strips_.begin(),
            [](const auto* bus, const BusStrip& strip) { return bus->bus_id == strip.id; });
    if (!same_buses) {
        for (auto& strip : bus_strips_) delete strip.meter_card;
        clearLayout(bus_mixer_layout_);
        bus_strips_.clear();
        int bus_idx = 0;
        for (const auto* bus : audio_buses) {
            ++bus_idx;
            BusStrip strip;
            strip.id = bus->bus_id;
            strip.meter = createMeter(meter_page_, "audioBusMeter", 7);
            strip.meter_card = createMeterSlot(
                meter_page_, QStringLiteral("B%1").arg(bus_idx),
                QString::fromUtf8(bus->name.c_str()), strip.meter, false,
                QStringLiteral("#3b82c4"));
            strip.meter_name = strip.meter_card->findChild<QLabel*>("audioMeterBadge");
            meter_layout_->addWidget(strip.meter_card);

            strip.mixer_card = new QFrame(mixer_panel_);
            strip.mixer_card->setObjectName("audioBusMixerCard");
            strip.mixer_card->setFixedWidth(84);
            auto* layout = new QVBoxLayout(strip.mixer_card);
            layout->setContentsMargins(4, 4, 4, 6);
            layout->setSpacing(4);

            auto* hdr = new QWidget(strip.mixer_card);
            hdr->setFixedHeight(24);
            auto* hdr_layout = new QVBoxLayout(hdr);
            hdr_layout->setContentsMargins(0, 0, 0, 0);
            hdr_layout->setSpacing(2);
            auto* accent = new QFrame(hdr);
            accent->setFixedHeight(3);
            accent->setStyleSheet(QStringLiteral("background: #3b82c4; border-radius: 1px;"));
            hdr_layout->addWidget(accent);
            auto* short_id = new QLabel(QStringLiteral("Bus %1").arg(bus_idx), hdr);
            short_id->setObjectName("audioStripName");
            short_id->setAlignment(Qt::AlignCenter);
            hdr_layout->addWidget(short_id);
            layout->addWidget(hdr);

            auto* bus_in_sp = new QFrame(strip.mixer_card);
            bus_in_sp->setFixedHeight(22);
            bus_in_sp->setStyleSheet(QStringLiteral("background: #14161a; border: 1px solid #262a33;"));
            layout->addWidget(bus_in_sp);

            auto* bus_tfx_sp = new QWidget(strip.mixer_card);
            bus_tfx_sp->setFixedHeight(40);
            layout->addWidget(bus_tfx_sp);

            auto* order_label = new QLabel(
                QStringLiteral("<span style='color:#d48f2a'>FX</span> <span style='color:#9cb048'>DY</span> <span style='color:#3bb2c7'>EQ</span>"),
                strip.mixer_card);
            order_label->setFixedHeight(20);
            order_label->setAlignment(Qt::AlignCenter);
            order_label->setStyleSheet(QStringLiteral("background: #121418; border: 1px solid #262a33; font-size: 9px; font-weight: 700;"));
            layout->addWidget(order_label);

            auto* bus_fx_plus = new QPushButton(QStringLiteral("+"), strip.mixer_card);
            bus_fx_plus->setFixedHeight(20);
            layout->addWidget(bus_fx_plus);

            auto* bus_fx_in = new QPushButton(QStringLiteral("In"), strip.mixer_card);
            bus_fx_in->setCheckable(true);
            bus_fx_in->setFixedHeight(20);
            layout->addWidget(bus_fx_in);

            auto* dyn_w = new MiniCurveWidget(MiniCurveWidget::Kind::Dynamics, strip.mixer_card);
            dyn_w->setFixedHeight(42);
            layout->addWidget(dyn_w);
            auto* eq_w = new MiniCurveWidget(MiniCurveWidget::Kind::Eq, strip.mixer_card);
            eq_w->setFixedHeight(28);
            layout->addWidget(eq_w);

            auto* bus_sends_sp = new QWidget(strip.mixer_card);
            bus_sends_sp->setFixedHeight(22);
            layout->addWidget(bus_sends_sp);

            auto* pan_box = new QWidget(strip.mixer_card);
            pan_box->setFixedHeight(26);
            auto* pan_layout = new QVBoxLayout(pan_box);
            pan_layout->setContentsMargins(0, 0, 0, 0);
            pan_layout->setSpacing(1);
            strip.pan = createPanSlider(pan_box, "audioBusPan");
            strip.pan_value = new QLabel(pan_box);
            strip.pan_value->setObjectName("audioStripSubLabel");
            strip.pan_value->setAlignment(Qt::AlignCenter);
            pan_layout->addWidget(strip.pan);
            pan_layout->addWidget(strip.pan_value);
            layout->addWidget(pan_box);

            auto* sep = new QFrame(strip.mixer_card);
            sep->setFixedHeight(1);
            sep->setStyleSheet(QStringLiteral("background: #2b2f38;"));
            layout->addWidget(sep);

            strip.mixer_name = new QLabel(strip.mixer_card);
            strip.mixer_name->setStyleSheet(QStringLiteral("color: #cfd6e0; font-size: 10px; font-weight: 600;"));
            strip.mixer_name->setAlignment(Qt::AlignCenter);
            layout->addWidget(strip.mixer_name);

            auto* toggle_row = new QHBoxLayout;
            toggle_row->setSpacing(3);
            strip.solo = new QCheckBox(QStringLiteral("S"), strip.mixer_card);
            strip.solo->setObjectName("audioBusSolo");
            strip.solo->setToolTip(translated("Solo"));
            strip.mute = new QCheckBox(QStringLiteral("M"), strip.mixer_card);
            strip.mute->setObjectName("audioBusMute");
            strip.mute->setToolTip(translated("Mute"));
            auto* rename = new QPushButton(QStringLiteral("Ren"), strip.mixer_card);
            rename->setObjectName("audioBusRename");
            rename->setFixedSize(24, 18);
            rename->setToolTip(translated("Rename"));
            auto* remove = new QPushButton(QStringLiteral("×"), strip.mixer_card);
            remove->setObjectName("audioBusRemove");
            remove->setFixedSize(18, 18);
            remove->setToolTip(translated("Remove"));
            toggle_row->addWidget(strip.solo);
            toggle_row->addWidget(strip.mute);
            toggle_row->addWidget(rename);
            toggle_row->addWidget(remove);
            layout->addLayout(toggle_row);

            auto* auto_icon = new QLabel(QStringLiteral("~"), strip.mixer_card);
            auto_icon->setAlignment(Qt::AlignCenter);
            auto_icon->setStyleSheet(QStringLiteral("color: #7c8596; font-size: 12px;"));
            layout->addWidget(auto_icon);

            strip.gain_value = new QLabel(strip.mixer_card);
            strip.gain_value->setObjectName("audioGainReadout");
            strip.gain_value->setAlignment(Qt::AlignCenter);
            layout->addWidget(strip.gain_value);

            auto* fader_row = new QHBoxLayout;
            fader_row->setSpacing(2);
            strip.gain = createGainSlider(strip.mixer_card, "audioBusGain");
            fader_row->addWidget(strip.gain);
            fader_row->addWidget(new DbScaleWidget(true, strip.mixer_card));
            strip.mixer_meter = createMeter(strip.mixer_card, "audioBusMixerMeter", 9);
            fader_row->addWidget(strip.mixer_meter);
            layout->addLayout(fader_row, 1);
            connect(strip.gain, &QSlider::sliderPressed, this,
                    &AudioWorkspace::gainEditStarted);
            connect(strip.gain, &QSlider::valueChanged, this,
                [this, id = strip.id](int value) {
                    const auto found = std::find_if(bus_strips_.begin(), bus_strips_.end(),
                        [id](const BusStrip& item) { return item.id == id; });
                    if (found == bus_strips_.end()) return;
                    found->gain_value->setText(gainText(value));
                    emitBusMix(id);
                });
            connect(strip.gain, &QSlider::sliderReleased, this,
                    &AudioWorkspace::gainEditFinished);
            connect(strip.pan, &QSlider::sliderPressed, this,
                    &AudioWorkspace::gainEditStarted);
            connect(strip.pan, &QSlider::valueChanged, this,
                [this, id = strip.id](int value) {
                    const auto found = std::find_if(bus_strips_.begin(), bus_strips_.end(),
                        [id](const BusStrip& item) { return item.id == id; });
                    if (found == bus_strips_.end()) return;
                    found->pan_value->setText(panText(value));
                    emitBusMix(id);
                });
            connect(strip.pan, &QSlider::sliderReleased, this,
                    &AudioWorkspace::gainEditFinished);
            connect(strip.mute, &QCheckBox::toggled, this,
                    [this, id = strip.id](bool) { emitBusMix(id); });
            connect(strip.solo, &QCheckBox::toggled, this,
                    [this, id = strip.id](bool checked) {
                        auto& ids = solo_bus_ids_;
                        ids.erase(std::remove(ids.begin(), ids.end(), id), ids.end());
                        if (checked) ids.push_back(id);
                        emitSoloState();
                    });
            connect(rename, &QPushButton::clicked, this, [this, id = strip.id] {
                const auto found = std::find_if(bus_strips_.begin(), bus_strips_.end(),
                    [id](const BusStrip& item) { return item.id == id; });
                if (found == bus_strips_.end()) return;
                bool accepted = false;
                const auto name = QInputDialog::getText(
                    mixer_panel_, translated("Rename bus"), translated("Bus name"),
                    QLineEdit::Normal, found->mixer_name->text(), &accepted);
                if (accepted && !name.trimmed().isEmpty())
                    emit renameBusRequested(id, name.trimmed());
            });
            connect(remove, &QPushButton::clicked, this,
                    [this, id = strip.id] { emit removeBusRequested(id); });
            bus_mixer_layout_->addWidget(strip.mixer_card);
            bus_strips_.push_back(std::move(strip));
        }
    }

    const QSignalBlocker master_mute_blocker(master_mute_);
    setGainSlider(master_gain_, master_gain_value_, master.audio_gain);
    master_mute_->setChecked(master.audio_muted);
    for (auto* track : audio_tracks) {
        auto found = std::find_if(strips_.begin(), strips_.end(),
            [track](const Strip& strip) { return strip.id == track->track_id; });
        if (found == strips_.end()) continue;
        found->meter_name->setToolTip(QString::fromUtf8(track->name.c_str()));
        found->mixer_name->setText(QString::fromUtf8(track->name.c_str()));
        setGainSlider(found->gain, found->gain_value, track->audio_gain);
        setPanSlider(found->pan, found->pan_value, track->audio_pan);
        { const QSignalBlocker blocker(found->mute); found->mute->setChecked(track->audio_muted); }
        { const QSignalBlocker blocker(found->solo); found->solo->setChecked(
            std::find(solo_track_ids_.begin(), solo_track_ids_.end(), track->track_id) !=
                solo_track_ids_.end()); }
        fillBusCombo(found->output, buses, track->output_bus_id);
        {
            const QSignalBlocker blocker(found->send_destination);
            fillBusCombo(found->send_destination, buses, std::nullopt,
                         track->output_bus_id);
            found->send_destination->removeItem(0);
        }
        found->send_destination->setEnabled(found->send_destination->count() > 0);
        found->add_send->setEnabled(found->send_destination->count() > 0);
        rebuildTrackSendRows(*found, *track, buses);
    }

    const bool same_effect_tracks = tracks.size() == track_effects_rows_.size() &&
        std::equal(tracks.begin(), tracks.end(), track_effects_rows_.begin(),
            [](const timeline::TimelineTrack& track, const TrackEffectsRow& row) {
                return track.track_id == row.id;
            });
    if (!same_effect_tracks) {
        clearLayout(track_effects_layout_);
        track_effects_rows_.clear();
        for (const auto& track : tracks) {
            TrackEffectsRow row;
            row.id = track.track_id;
            row.effects = track.audio_effects;
            auto* widget = new QWidget(mixer_panel_);
            auto* layout = new QHBoxLayout(widget);
            layout->setContentsMargins(0, 0, 0, 0);
            row.name = new QLabel(QString::fromUtf8(track.name.c_str()), widget);
            row.edit = new QPushButton(translated("Effects…"), widget);
            row.edit->setObjectName("audioTrackEffects");
            row.edit->setProperty("trackId",
                QVariant::fromValue<qulonglong>(track.track_id));
            layout->addWidget(row.name, 1);
            layout->addWidget(row.edit);
            connect(row.edit, &QPushButton::clicked, this, [this, id = row.id] {
                const auto found = std::find_if(
                    track_effects_rows_.begin(), track_effects_rows_.end(),
                    [id](const TrackEffectsRow& item) { return item.id == id; });
                if (found == track_effects_rows_.end()) return;
                AudioEffectChainDialog dialog(
                    translated("Track audio effects — %1").arg(found->name->text()),
                    found->effects, mixer_panel_);
                if (dialog.exec() == QDialog::Accepted &&
                    timeline::validAudioEffectChain(dialog.effects())) {
                    emit trackAudioEffectsChanged(id, dialog.effects());
                }
            });
            row.ducking_button = new QPushButton(translated("Ducking"), widget);
            row.ducking_button->setObjectName("audioTrackDucking");
            widget->layout()->addWidget(row.ducking_button);
            connect(row.ducking_button, &QPushButton::clicked, this, [this, id = row.id] {
                const auto found = std::find_if(track_effects_rows_.begin(), track_effects_rows_.end(),
                    [id](const auto& item) { return item.id == id; });
                if (found == track_effects_rows_.end()) return;
                AudioDuckingDialog dialog(found->ducking, ducking_sources_, id, mixer_panel_);
                if (dialog.exec() != QDialog::Accepted) return;
                if (dialog.generateRequested()) emit generateDuckingCurveRequested(id, dialog.settings());
                else emit trackDuckingChanged(id, dialog.settings());
            });
            row.isolation = new VoiceIsolationPanel(mixer_panel_);
            row.isolation->setProperty("trackId", QVariant::fromValue<qulonglong>(row.id));
            row.isolation->apply = [this, id = row.id](auto settings) { emit voiceIsolationChanged(false, id, std::move(settings)); };
            row.isolation->regenerate = [this, id = row.id] { emit voiceIsolationRegenerate(false, id); };
            row.isolation->cancel = [this] { emit voiceIsolationCancel(); };
            track_effects_layout_->addWidget(widget);
            track_effects_layout_->addWidget(row.isolation);
            track_effects_rows_.push_back(std::move(row));
        }
    }
    for (const auto& track : tracks) {
        const auto found = std::find_if(track_effects_rows_.begin(),
            track_effects_rows_.end(), [&track](const TrackEffectsRow& row) {
                return row.id == track.track_id;
            });
        if (found == track_effects_rows_.end()) continue;
        found->name->setText(QString::fromUtf8(track.name.c_str()));
        found->effects = track.audio_effects;
        found->ducking = track.audio_ducking;
        found->isolation->setSettings(track.voice_isolation);
        const auto state = isolation_states_.find({false, track.track_id});
        found->isolation->setState(state == isolation_states_.end() ? (track.voice_isolation.enabled ? "Processing" : "Off") : state->second.first,
            state == isolation_states_.end() ? 0 : state->second.second);
    }

    for (const auto& bus : buses) {
        auto found = std::find_if(bus_strips_.begin(), bus_strips_.end(),
            [&bus](const BusStrip& strip) { return strip.id == bus.bus_id; });
        if (found == bus_strips_.end()) continue;
        const auto name = QString::fromUtf8(bus.name.c_str());
        found->meter_name->setToolTip(name);
        found->mixer_name->setText(name);
        setGainSlider(found->gain, found->gain_value, bus.audio_gain);
        setPanSlider(found->pan, found->pan_value, bus.audio_pan);
        { const QSignalBlocker blocker(found->mute); found->mute->setChecked(bus.audio_muted); }
        { const QSignalBlocker blocker(found->solo); found->solo->setChecked(
            std::find(solo_bus_ids_.begin(), solo_bus_ids_.end(), bus.bus_id) !=
                solo_bus_ids_.end()); }
    }

    const auto valid_track_solo = std::remove_if(
        solo_track_ids_.begin(), solo_track_ids_.end(), [&audio_tracks](auto id) {
            return std::none_of(audio_tracks.begin(), audio_tracks.end(),
                [id](const auto* track) { return track->track_id == id; });
        });
    const bool removed_track_solo = valid_track_solo != solo_track_ids_.end();
    solo_track_ids_.erase(valid_track_solo, solo_track_ids_.end());
    const auto valid_bus_solo = std::remove_if(
        solo_bus_ids_.begin(), solo_bus_ids_.end(), [&buses](auto id) {
            return std::none_of(buses.begin(), buses.end(),
                [id](const auto& bus) { return bus.bus_id == id; });
        });
    const bool removed_bus_solo = valid_bus_solo != solo_bus_ids_.end();
    solo_bus_ids_.erase(valid_bus_solo, solo_bus_ids_.end());
    if (removed_track_solo || removed_bus_solo) emitSoloState();

    if (meter_filler_layout_ != nullptr) {
        constexpr int kMinBridgeChannels = 24;
        const int active = static_cast<int>(strips_.size() + bus_strips_.size());
        const int needed_fillers = std::max(0, kMinBridgeChannels - active);
        if (meter_filler_layout_->count() != needed_fillers) {
            clearLayout(meter_filler_layout_);
            for (int i = 0; i < needed_fillers; ++i) {
                const bool show_scale = (active == 0 && i == 0);
                meter_filler_layout_->addWidget(createMeterSlot(
                    meter_page_, QString(), QString(), nullptr, show_scale, QString()));
            }
        }
    }
}

void AudioWorkspace::rebuildTrackSendRows(
    Strip& strip, const timeline::TimelineTrack& track,
    const std::vector<timeline::AudioBus>& buses) {
    const bool same_rows = strip.sends.size() == track.audio_sends.size() &&
        std::equal(strip.sends.begin(), strip.sends.end(), track.audio_sends.begin(),
            [](const SendRow& row, const timeline::AudioSend& send) {
                return row.bus_id == send.destination_bus_id;
            });
    if (!same_rows) {
        clearLayout(strip.sends_layout);
        strip.sends.clear();
        for (const auto& send : track.audio_sends) {
            SendRow row;
            row.bus_id = send.destination_bus_id;
            row.widget = new QWidget(strip.mixer_card);
            auto* layout = new QHBoxLayout(row.widget);
            layout->setContentsMargins(0, 0, 0, 0);
            row.destination = new QComboBox(row.widget);
            row.destination->setObjectName("audioTrackSendBus");
            fillBusCombo(row.destination, buses, send.destination_bus_id);
            row.destination->removeItem(0);
            row.gain = createSendGainSlider(row.widget, "audioTrackSendGain");
            row.gain->setRange(0, 200);
            row.gain_value = new QLabel(row.widget);
            row.gain_value->setMinimumWidth(48);
            row.pre_fader = new QCheckBox(translated("Pre"), row.widget);
            row.pre_fader->setObjectName("audioTrackSendPreFader");
            row.remove = new QPushButton(QStringLiteral("×"), row.widget);
            row.remove->setObjectName("audioTrackRemoveSend");
            layout->addWidget(row.destination, 1);
            layout->addWidget(row.gain, 2);
            layout->addWidget(row.gain_value);
            layout->addWidget(row.pre_fader);
            layout->addWidget(row.remove);
            strip.sends_layout->addWidget(row.widget);
            const auto track_id = strip.id;
            auto* destination_control = row.destination;
            connect(row.destination, qOverload<int>(&QComboBox::currentIndexChanged),
                    this, [this, track_id, destination_control](int) {
                        const auto found = std::find_if(strips_.begin(), strips_.end(),
                            [track_id](const Strip& item) { return item.id == track_id; });
                        if (found == strips_.end()) return;
                        const auto row = std::find_if(found->sends.begin(), found->sends.end(),
                            [destination_control](const SendRow& value) {
                                return value.destination == destination_control;
                            });
                        if (row != found->sends.end()) row->bus_id = static_cast<timeline::AudioBusId>(
                            row->destination->currentData().toULongLong());
                        emitTrackMix(track_id);
                    });
            connect(row.gain, &QSlider::sliderPressed, this,
                    &AudioWorkspace::gainEditStarted);
            connect(row.gain, &QSlider::valueChanged, this,
                    [this, track_id = strip.id](int) { emitTrackMix(track_id); });
            connect(row.gain, &QSlider::sliderReleased, this,
                    &AudioWorkspace::gainEditFinished);
            connect(row.pre_fader, &QCheckBox::toggled, this,
                    [this, track_id = strip.id](bool) { emitTrackMix(track_id); });
            auto* row_widget = row.widget;
            connect(row.remove, &QPushButton::clicked, this,
                    [this, track_id = strip.id, row_widget] {
                        auto found = std::find_if(strips_.begin(), strips_.end(),
                            [track_id](const Strip& item) { return item.id == track_id; });
                        if (found == strips_.end()) return;
                        std::erase_if(found->sends, [row_widget](const SendRow& value) {
                            return value.widget == row_widget;
                        });
                        emitTrackMix(track_id);
                    });
            strip.sends.push_back(row);
        }
    }
    for (std::size_t index = 0; index < strip.sends.size(); ++index) {
        auto& row = strip.sends[index];
        const auto& send = track.audio_sends[index];
        {
            const QSignalBlocker blocker(row.destination);
            fillBusCombo(row.destination, buses, send.destination_bus_id);
            row.destination->removeItem(0);
        }
        setGainSlider(row.gain, row.gain_value, send.gain);
        { const QSignalBlocker blocker(row.pre_fader);
          row.pre_fader->setChecked(send.tap == timeline::AudioSendTap::PreFader); }
    }
}

void AudioWorkspace::emitTrackMix(timeline::TrackId track_id) {
    const auto found = std::find_if(strips_.begin(), strips_.end(),
        [track_id](const Strip& strip) { return strip.id == track_id; });
    if (found == strips_.end()) return;
    const auto output_id = found->output->currentData().toULongLong();
    const std::optional<timeline::AudioBusId> output = output_id == 0
        ? std::nullopt
        : std::optional<timeline::AudioBusId>{static_cast<timeline::AudioBusId>(output_id)};
    std::vector<timeline::AudioSend> sends;
    sends.reserve(found->sends.size());
    for (const auto& row : found->sends) {
        if (row.destination->currentIndex() < 0) continue;
        const auto destination = static_cast<timeline::AudioBusId>(
            row.destination->currentData().toULongLong());
        if (destination == 0 || output == destination) continue;
        sends.push_back(timeline::AudioSend{
            destination, row.gain->value() / 100.0,
            row.pre_fader->isChecked() ? timeline::AudioSendTap::PreFader
                                       : timeline::AudioSendTap::PostFader});
    }
    emit trackMixChanged(track_id, found->gain->value() / 100.0,
        found->mute->isChecked(), found->pan->value() / 100.0, output,
        std::move(sends));
}

void AudioWorkspace::emitBusMix(timeline::AudioBusId bus_id) {
    const auto found = std::find_if(bus_strips_.begin(), bus_strips_.end(),
        [bus_id](const BusStrip& strip) { return strip.id == bus_id; });
    if (found == bus_strips_.end()) return;
    emit busMixChanged(bus_id, found->gain->value() / 100.0,
        found->mute->isChecked(), found->pan->value() / 100.0);
}

void AudioWorkspace::emitSoloState() {
    emit soloStateChanged(solo_track_ids_, solo_bus_ids_);
}

void AudioWorkspace::setPeaks(
    const std::vector<std::pair<timeline::TrackId, float>>& track_peaks,
    const std::vector<std::pair<timeline::AudioBusId, float>>& bus_peaks,
    float master_peak) {
    const auto set_peak = [](QProgressBar* meter, float peak) {
        if (meter != nullptr) meter->setValue(static_cast<int>(std::lround(
            std::clamp(peak, 0.0F, 1.0F) * 100.0F)));
    };
    set_peak(master_meter_, master_peak);
    set_peak(mixer_master_meter_, master_peak);
    if (!loudness_paused_) {
        set_peak(control_room_meter_, master_peak);
        const float clamped = std::clamp(master_peak, 0.0F, 1.0F);
        if (clamped > 1.0e-5F) {
            const float db = 20.0F * std::log10(clamped);
            if (tp_value_label_) {
                tp_value_label_->setText(QString::number(db, 'f', 1));
            }
            const float lu = db + 23.0F;
            loudness_short_max_lu_ = std::max(loudness_short_max_lu_, lu);
            loudness_min_lu_ = (loudness_samples_ == 0) ? lu : std::min(loudness_min_lu_, lu);
            loudness_max_lu_ = std::max(loudness_max_lu_, lu);
            loudness_sum_ += static_cast<double>(lu);
            ++loudness_samples_;
            const float integrated = static_cast<float>(loudness_sum_ / loudness_samples_);
            const float range = std::max(0.0F, loudness_max_lu_ - loudness_min_lu_);
            if (loudness_short_value_) loudness_short_value_->setText(QString::number(lu, 'f', 1));
            if (loudness_short_max_value_) loudness_short_max_value_->setText(QString::number(loudness_short_max_lu_, 'f', 1));
            if (loudness_range_value_) loudness_range_value_->setText(QString::number(range, 'f', 1));
            if (loudness_integrated_value_) loudness_integrated_value_->setText(QString::number(integrated, 'f', 1));
            if (auto* w = static_cast<LoudnessScaleWidget*>(loudness_meter_widget_)) {
                const float m_norm = std::clamp((lu + 18.0F) / 27.0F, 0.0F, 1.0F);
                const float i_norm = std::clamp((integrated + 18.0F) / 27.0F, 0.0F, 1.0F);
                w->setLevels(m_norm, i_norm);
            }
        } else {
            if (tp_value_label_) tp_value_label_->setText(QStringLiteral("-100"));
            if (auto* w = static_cast<LoudnessScaleWidget*>(loudness_meter_widget_)) {
                w->setLevels(0.0F, loudness_samples_ > 0
                    ? std::clamp((static_cast<float>(loudness_sum_ / loudness_samples_) + 18.0F) / 27.0F, 0.0F, 1.0F)
                    : 0.0F);
            }
        }
    }
    for (auto& strip : strips_) {
        const auto found = std::find_if(track_peaks.begin(), track_peaks.end(),
            [&strip](const auto& peak) { return peak.first == strip.id; });
        const auto peak = found == track_peaks.end() ? 0.0F : found->second;
        set_peak(strip.meter, peak);
        set_peak(strip.mixer_meter, peak);
    }
    for (auto& strip : bus_strips_) {
        const auto found = std::find_if(bus_peaks.begin(), bus_peaks.end(),
            [&strip](const auto& peak) { return peak.first == strip.id; });
        const auto peak = found == bus_peaks.end() ? 0.0F : found->second;
        set_peak(strip.meter, peak);
        set_peak(strip.mixer_meter, peak);
    }
}

void AudioWorkspace::clearPeaks() { setPeaks({}, {}, 0.0F); }

void AudioWorkspace::clearSoloState() {
    if (solo_track_ids_.empty() && solo_bus_ids_.empty()) return;
    solo_track_ids_.clear();
    solo_bus_ids_.clear();
    for (auto& strip : strips_) {
        const QSignalBlocker blocker(strip.solo);
        strip.solo->setChecked(false);
    }
    for (auto& strip : bus_strips_) {
        const QSignalBlocker blocker(strip.solo);
        strip.solo->setChecked(false);
    }
    emitSoloState();
}

void AudioWorkspace::setIsolationState(bool clip, quint64 id, QString state, int progress) {
    isolation_states_[{clip, id}] = {state, progress};
    if (clip && id == selected_clip_id_ && selected_clip_isolation_) selected_clip_isolation_->setState(state, progress);
    if (!clip) for (auto& row : track_effects_rows_) if (row.id == id && row.isolation) row.isolation->setState(state, progress);
}

void AudioWorkspace::setSelectedClip(const timeline::TimelineClip* clip) {
    const bool valid = clip && (clip->kind == timeline::ClipKind::Audio || clip->kind == timeline::ClipKind::Video);
    selected_clip_id_ = valid ? clip->clip_id : 0;
    selected_clip_isolation_settings_ = valid ? clip->voice_isolation : timeline::VoiceIsolationSettings{};
    if (selected_clip_isolation_) {
        selected_clip_isolation_->apply = [this, id = selected_clip_id_](auto settings) { if (id) emit voiceIsolationChanged(true, id, std::move(settings)); };
        selected_clip_isolation_->setEnabled(valid); selected_clip_isolation_->setSettings(selected_clip_isolation_settings_);
        const auto state = isolation_states_.find({true, selected_clip_id_});
        selected_clip_isolation_->setState(state == isolation_states_.end() ? (selected_clip_isolation_settings_.enabled ? "Processing" : "Off") : state->second.first,
            state == isolation_states_.end() ? 0 : state->second.second);
    }
    selected_clip_chain_ = valid ? clip->audio_effects : std::vector<timeline::AudioEffectInstance>{};
    selected_clip_name_ = valid ? QString::fromUtf8(clip->display_name.c_str()) : QString();
    if (selected_clip_effects_) {
        selected_clip_effects_->setEnabled(valid);
        selected_clip_effects_->setText(valid ? translated("Clip audio effects — %1").arg(selected_clip_name_) :
            translated("Select an audio clip to edit its effects"));
    }
}

}  // namespace ui

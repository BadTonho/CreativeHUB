#include "info_page.h"

#include <QCoreApplication>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

#include <array>

namespace ui {
namespace {

QString tr(const char* text) {
    return QCoreApplication::translate("InfoPage", text);
}

struct FieldSpec {
    const char* key;      // object-name stem
    const char* label;
    const char* default_value;
};

// Placeholder for clip data that is not available yet.
constexpr const char* kUnknown = "–";

constexpr std::array<FieldSpec, 10> kClipFields{{
    {"ClipFileName", "File Name", kUnknown},
    {"ClipReelName", "Reel Name", kUnknown},
    {"ClipStartTC", "Start T/C", kUnknown},
    {"ClipEndTC", "End T/C", kUnknown},
    {"ClipDuration", "Duration", kUnknown},
    {"ClipFrames", "Frames", kUnknown},
    {"ClipVersion", "Version", kUnknown},
    {"ClipFrameRate", "Frame Rate", kUnknown},
    {"ClipSourceRes", "Source Res", kUnknown},
    {"ClipCodec", "Codec", kUnknown},
}};

// System values are settings whose defaults are known.
constexpr std::array<FieldSpec, 9> kSystemFields{{
    {"SystemClips", "Clips", kUnknown},
    {"SystemProxy", "Proxy", "Off"},
    {"SystemClipCache", "Clip Cache", "Off"},
    {"SystemRefTransform", "Ref Transform", "Off"},
    {"SystemRefMode", "Ref Mode", "Gallery"},
    {"SystemWipeStyle", "Wipe Style", "Wipe-H"},
    {"SystemConvergence", "Convergence", "Opposite"},
    {"SystemStereoGrade", "Stereo Grade", "Left - Solo"},
    {"SystemStereoDisplay", "Stereo Display", "Mono"},
}};

template <std::size_t N>
QVBoxLayout* makeSection(QWidget* parent, const char* title, const std::array<FieldSpec, N>& fields) {
    auto* column = new QVBoxLayout;
    column->setSpacing(8);
    auto* heading = new QLabel(tr(title), parent);
    QFont heading_font = heading->font();
    heading_font.setBold(true);
    heading->setFont(heading_font);
    column->addWidget(heading);
    auto* grid = new QGridLayout;
    grid->setHorizontalSpacing(28);
    grid->setVerticalSpacing(10);
    for (std::size_t i = 0; i < N; ++i) {
        const FieldSpec& field = fields[i];
        auto* cell = new QVBoxLayout;
        cell->setSpacing(0);
        auto* label = new QLabel(tr(field.label), parent);
        label->setObjectName(QStringLiteral("colorInfo%1Label").arg(QLatin1String(field.key)));
        label->setStyleSheet(QStringLiteral("color: #8a93a0;"));
        auto* value = new QLabel(QString::fromUtf8(field.default_value), parent);
        value->setObjectName(QStringLiteral("colorInfo%1Value").arg(QLatin1String(field.key)));
        // Long values (for example a file name) are clipped, not widened.
        value->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        value->setMinimumWidth(40);
        value->setTextInteractionFlags(Qt::TextSelectableByMouse);
        value->setToolTip(value->text());
        cell->addWidget(label);
        cell->addWidget(value);
        grid->addLayout(cell, static_cast<int>(i / 2), static_cast<int>(i % 2));
    }
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);
    column->addLayout(grid);
    column->addStretch(1);
    return column;
}

}  // namespace

InfoPage::InfoPage(QWidget* parent) : QWidget(parent) {
    setObjectName("colorInfoPage");
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(8);

    auto* title = new QLabel(tr("Info"), this);
    title->setObjectName("colorInfoTitle");
    QFont title_font = title->font();
    title_font.setBold(true);
    title->setFont(title_font);
    root->addWidget(title);

    auto* body = new QHBoxLayout;
    body->setSpacing(14);
    body->addLayout(makeSection(this, "Clip", kClipFields), 1);
    auto* line = new QFrame(this);
    line->setFrameShape(QFrame::VLine);
    line->setFrameShadow(QFrame::Plain);
    body->addWidget(line);
    body->addLayout(makeSection(this, "System", kSystemFields), 1);
    root->addLayout(body, 1);
}

void InfoPage::setValue(const QString& key, const QString& text) {
    if (auto* value = findChild<QLabel*>(QStringLiteral("colorInfo%1Value").arg(key))) {
        value->setText(text);
        value->setToolTip(text);
    }
}

void InfoPage::restoreDefaults() {
    for (const auto& field : kClipFields) setValue(QLatin1String(field.key), QString::fromUtf8(field.default_value));
    for (const auto& field : kSystemFields) setValue(QLatin1String(field.key), QString::fromUtf8(field.default_value));
}

}  // namespace ui

#include "input_sizing_page.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace ui {
namespace {

QString tr(const char* text) {
    return QCoreApplication::translate("InputSizingPage", text);
}

QPushButton* makeButton(QWidget* parent, const QString& text, const QString& name,
    const QString& accessible_name, int width = 28) {
    auto* button = new QPushButton(text, parent);
    button->setObjectName(name);
    button->setAccessibleName(accessible_name);
    button->setToolTip(accessible_name);
    button->setFixedSize(width, 26);
    button->setFocusPolicy(Qt::NoFocus);
    return button;
}

struct FieldSpec {
    const char* label;
    const char* name;
    double minimum;
    double maximum;
    double value;
};

QDoubleSpinBox* makeSpin(QWidget* parent, const QString& name, const FieldSpec& spec) {
    auto* box = new QDoubleSpinBox(parent);
    box->setObjectName(name);
    box->setRange(spec.minimum, spec.maximum);
    box->setDecimals(3);
    box->setValue(spec.value);
    box->setButtonSymbols(QAbstractSpinBox::NoButtons);
    box->setAlignment(Qt::AlignCenter);
    box->setFixedWidth(62);
    box->setKeyboardTracking(false);
    box->setProperty("defaultValue", spec.value);
    return box;
}

}  // namespace

InputSizingPage::InputSizingPage(QWidget* parent) : QWidget(parent) {
    setObjectName("colorInputSizingPage");
    setStyleSheet(QStringLiteral(R"(
        QWidget#colorInputSizingPage QDoubleSpinBox { padding: 2px 2px; }
        QWidget#colorInputSizingPage QPushButton:checked { background: #2f5f8f; }
    )"));
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(6);

    // Header: title, five sizing-mode buttons, reset and more.
    auto* header = new QHBoxLayout;
    auto* title = new QLabel(tr("Sizing - Input Sizing"), this);
    title->setObjectName("colorInputSizingTitle");
    QFont title_font = title->font();
    title_font.setBold(true);
    title->setFont(title_font);
    header->addWidget(title);
    header->addStretch();
    auto* modes = new QButtonGroup(this);
    modes->setExclusive(true);
    const char* const mode_glyphs[] = {"□", "⇥", "▭", "≣", "▤"};
    for (int i = 0; i < 5; ++i) {
        auto* mode = makeButton(this, QString::fromUtf8(mode_glyphs[i]),
            QStringLiteral("colorInputSizingModeButton%1").arg(i + 1),
            tr("Sizing mode") + QStringLiteral(" %1").arg(i + 1), 34);
        mode->setCheckable(true);
        modes->addButton(mode, i);
        header->addWidget(mode);
    }
    // The reference layout highlights the second mode (Input Sizing).
    modes->button(1)->setChecked(true);
    auto* reset_all = makeButton(this, QStringLiteral("↺"),
        QStringLiteral("colorInputSizingResetButton"), tr("Reset Input Sizing"));
    header->addWidget(reset_all);
    header->addWidget(makeButton(this, QStringLiteral("…"),
        QStringLiteral("colorInputSizingMoreButton"), tr("More actions")));
    root->addLayout(header);

    auto* body = new QHBoxLayout;
    body->setSpacing(14);

    // Sizing section: two columns of fields and the Flip toggles.
    auto* sizing = new QVBoxLayout;
    auto* sizing_head = new QHBoxLayout;
    sizing_head->addWidget(new QLabel(tr("Sizing"), this));
    sizing_head->addStretch();
    auto* sizing_reset = makeButton(this, QStringLiteral("↺"),
        QStringLiteral("colorInputSizingSizingResetButton"), tr("Reset Sizing"), 24);
    sizing_head->addWidget(sizing_reset);
    sizing->addLayout(sizing_head);
    auto* columns = new QHBoxLayout;
    columns->setSpacing(14);
    const FieldSpec left[] = {
        {"Pan", "Pan", -10.0, 10.0, 0.0}, {"Tilt", "Tilt", -10.0, 10.0, 0.0},
        {"Zoom", "Zoom", 0.0, 10.0, 1.0}, {"Rotate", "Rotate", -360.0, 360.0, 0.0},
    };
    const FieldSpec right[] = {
        {"Width", "Width", 0.0, 10.0, 1.0}, {"Height", "Height", 0.0, 10.0, 1.0},
        {"Pitch", "Pitch", -90.0, 90.0, 0.0}, {"Yaw", "Yaw", -90.0, 90.0, 0.0},
    };
    for (const auto* set : {left, right}) {
        auto* grid = new QGridLayout;
        grid->setHorizontalSpacing(10);
        for (int i = 0; i < 4; ++i) {
            grid->addWidget(new QLabel(tr(set[i].label), this), i, 0, Qt::AlignRight | Qt::AlignVCenter);
            grid->addWidget(makeSpin(this,
                                 QStringLiteral("colorInputSizing%1").arg(QLatin1String(set[i].name)), set[i]),
                i, 1);
        }
        if (set == right) {
            grid->addWidget(new QLabel(tr("Flip"), this), 4, 0, Qt::AlignRight | Qt::AlignVCenter);
            auto* flips = new QHBoxLayout;
            flips->setSpacing(8);
            const struct { const char* glyph; const char* object; const char* label; } flip_specs[] = {
                {"↔", "colorInputSizingFlipHorizontal", "Flip horizontal"},
                {"↕", "colorInputSizingFlipVertical", "Flip vertical"},
            };
            for (const auto& spec : flip_specs) {
                auto* flip = makeButton(this, QString::fromUtf8(spec.glyph), QLatin1String(spec.object),
                    tr(spec.label), 34);
                flip->setCheckable(true);
                flips->addWidget(flip);
            }
            flips->addStretch();
            grid->addLayout(flips, 4, 1);
        }
        columns->addLayout(grid);
        if (set == left) {
            auto* line = new QFrame(this);
            line->setFrameShape(QFrame::VLine);
            line->setFrameShadow(QFrame::Plain);
            columns->addWidget(line);
        }
    }
    columns->addStretch();
    sizing->addLayout(columns);
    sizing->addStretch(1);
    body->addLayout(sizing, 3);

    auto* divider = new QFrame(this);
    divider->setFrameShape(QFrame::VLine);
    divider->setFrameShadow(QFrame::Plain);
    body->addWidget(divider);

    // Blanking section: disabled in this sizing mode.
    auto* blanking = new QVBoxLayout;
    auto* blanking_head = new QHBoxLayout;
    blanking_head->addWidget(new QLabel(tr("Blanking"), this));
    blanking_head->addStretch();
    auto* blanking_reset = makeButton(this, QStringLiteral("↺"),
        QStringLiteral("colorInputSizingBlankingResetButton"), tr("Reset Blanking"), 24);
    blanking_head->addWidget(blanking_reset);
    blanking->addLayout(blanking_head);
    auto* blanking_grid = new QGridLayout;
    blanking_grid->setHorizontalSpacing(10);
    const char* const edges[] = {"Top", "Right", "Bottom", "Left"};
    for (int i = 0; i < 4; ++i) {
        blanking_grid->addWidget(new QLabel(tr(edges[i]), this), i, 0, Qt::AlignRight | Qt::AlignVCenter);
        auto* box = makeSpin(this, QStringLiteral("colorInputSizingBlanking%1").arg(QLatin1String(edges[i])),
            {edges[i], edges[i], 0.0, 1000.0, 0.0});
        // Shown empty, as in the reference layout, until a value is entered.
        box->setSpecialValueText(QStringLiteral(" "));
        blanking_grid->addWidget(box, i, 1);
    }
    auto* smooth = new QCheckBox(tr("Smooth"), this);
    smooth->setObjectName("colorInputSizingBlankingSmooth");
    blanking_grid->addWidget(smooth, 4, 1);
    blanking->addLayout(blanking_grid);
    blanking->addStretch(1);
    auto* blanking_host = new QWidget(this);
    blanking_host->setObjectName("colorInputSizingBlanking");
    blanking_host->setLayout(blanking);
    blanking_host->setEnabled(false);
    body->addWidget(blanking_host, 2);
    root->addLayout(body, 1);

    connect(modes, &QButtonGroup::idClicked, this, [this, title](int id) {
        mode_ = id;
        title->setText(id == 3 ? tr("Sizing - Node Sizing") : tr("Sizing - Input Sizing"));
        emitEffect();
    });
    for (const char* name : {"colorInputSizingPan", "colorInputSizingTilt", "colorInputSizingZoom",
                             "colorInputSizingRotate", "colorInputSizingWidth", "colorInputSizingHeight",
                             "colorInputSizingPitch", "colorInputSizingYaw"}) {
        if (auto* b = findChild<QDoubleSpinBox*>(QLatin1String(name))) {
            connect(b, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                [this](double) { emitEffect(); });
        }
    }
    for (const char* name : {"colorInputSizingFlipHorizontal", "colorInputSizingFlipVertical"}) {
        if (auto* b = findChild<QPushButton*>(QLatin1String(name))) {
            connect(b, &QPushButton::toggled, this, [this](bool) { emitEffect(); });
        }
    }
    connect(sizing_reset, &QPushButton::clicked, this, [this] {
        for (auto* box : findChildren<QDoubleSpinBox*>(QRegularExpression(
                 QStringLiteral("^colorInputSizing(Pan|Tilt|Zoom|Rotate|Width|Height|Pitch|Yaw)$"))))
            box->setValue(box->property("defaultValue").toDouble());
        for (const char* name : {"colorInputSizingFlipHorizontal", "colorInputSizingFlipVertical"})
            findChild<QPushButton*>(QLatin1String(name))->setChecked(false);
        emitEffect();
    });
    connect(reset_all, &QPushButton::clicked, this, [this] {
        restoreDefaults();
        emitEffect();
    });
}

void InputSizingPage::setEffectChangedHandler(
    std::function<void(const creative_suite::effects::EffectInstance&)> on_changed) {
    on_changed_ = std::move(on_changed);
}

void InputSizingPage::setEffect(const creative_suite::effects::EffectInstance& effect) {
    updating_ = true;
    const auto val = [&effect](std::string_view id, double def) {
        for (const auto& p : effect.parameters) {
            if (p.id == id) return p.value;
        }
        return def;
    };
    mode_ = static_cast<int>(std::lround(val("mode", 1.0)));
    if (auto* btn = findChild<QPushButton*>(QStringLiteral("colorInputSizingModeButton%1").arg(mode_ + 1))) {
        const QSignalBlocker b(btn);
        btn->setChecked(true);
    }
    const auto set_spin = [this](const char* obj, double v) {
        if (auto* box = findChild<QDoubleSpinBox*>(QLatin1String(obj))) {
            const QSignalBlocker b(box);
            box->setValue(v);
        }
    };
    const auto set_tog = [this](const char* obj, bool v) {
        if (auto* btn = findChild<QPushButton*>(QLatin1String(obj))) {
            const QSignalBlocker b(btn);
            btn->setChecked(v);
        }
    };
    set_spin("colorInputSizingPan", val("pan", 0.0));
    set_spin("colorInputSizingTilt", val("tilt", 0.0));
    set_spin("colorInputSizingZoom", val("zoom", 1.0));
    set_spin("colorInputSizingRotate", val("rotate", 0.0));
    set_spin("colorInputSizingWidth", val("width", 1.0));
    set_spin("colorInputSizingHeight", val("height", 1.0));
    set_spin("colorInputSizingPitch", val("pitch", 0.0));
    set_spin("colorInputSizingYaw", val("yaw", 0.0));
    set_tog("colorInputSizingFlipHorizontal", val("flip_h", 0.0) >= 0.5);
    set_tog("colorInputSizingFlipVertical", val("flip_v", 0.0) >= 0.5);
    updating_ = false;
}

void InputSizingPage::emitEffect() {
    if (updating_ || !on_changed_) return;
    auto eff = creative_suite::effects::makeDefaultInstance("video.input_sizing");
    const auto spin = [this](const char* obj, double def) {
        auto* b = findChild<QDoubleSpinBox*>(QLatin1String(obj));
        return b ? b->value() : def;
    };
    const auto tog = [this](const char* obj) {
        auto* b = findChild<QPushButton*>(QLatin1String(obj));
        return (b && b->isChecked()) ? 1.0 : 0.0;
    };
    (void)creative_suite::effects::setParameterValue(eff, "mode", static_cast<double>(mode_));
    (void)creative_suite::effects::setParameterValue(eff, "pan", spin("colorInputSizingPan", 0.0));
    (void)creative_suite::effects::setParameterValue(eff, "tilt", spin("colorInputSizingTilt", 0.0));
    (void)creative_suite::effects::setParameterValue(eff, "zoom", spin("colorInputSizingZoom", 1.0));
    (void)creative_suite::effects::setParameterValue(eff, "rotate", spin("colorInputSizingRotate", 0.0));
    (void)creative_suite::effects::setParameterValue(eff, "width", spin("colorInputSizingWidth", 1.0));
    (void)creative_suite::effects::setParameterValue(eff, "height", spin("colorInputSizingHeight", 1.0));
    (void)creative_suite::effects::setParameterValue(eff, "pitch", spin("colorInputSizingPitch", 0.0));
    (void)creative_suite::effects::setParameterValue(eff, "yaw", spin("colorInputSizingYaw", 0.0));
    (void)creative_suite::effects::setParameterValue(eff, "flip_h", tog("colorInputSizingFlipHorizontal"));
    (void)creative_suite::effects::setParameterValue(eff, "flip_v", tog("colorInputSizingFlipVertical"));
    on_changed_(eff);
}

void InputSizingPage::restoreDefaults() {
    for (auto* box : findChildren<QDoubleSpinBox*>())
        box->setValue(box->property("defaultValue").toDouble());
    for (const char* name : {"colorInputSizingFlipHorizontal", "colorInputSizingFlipVertical"})
        if (auto* flip = findChild<QPushButton*>(QLatin1String(name))) flip->setChecked(false);
    if (auto* smooth = findChild<QCheckBox*>("colorInputSizingBlankingSmooth")) smooth->setChecked(false);
    if (auto* mode = findChild<QPushButton*>("colorInputSizingModeButton2")) mode->setChecked(true);
}

}  // namespace ui

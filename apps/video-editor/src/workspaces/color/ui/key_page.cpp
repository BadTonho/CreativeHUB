#include "key_page.h"

#include <QCoreApplication>
#include <QList>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QVBoxLayout>

#include <algorithm>

namespace ui {
namespace {

QString tr(const char* text) {
    return QCoreApplication::translate("KeyPage", text);
}

// Matte preview: a 16:9 white frame (no key applied) between black bars.
class MattePreview final : public QWidget {
public:
    explicit MattePreview(QWidget* parent) : QWidget(parent) {
        setMinimumSize(240, 150);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.fillRect(rect(), Qt::black);
        const double width = this->width();
        const double frame_height = std::min<double>(height(), width * 9.0 / 16.0);
        painter.fillRect(QRectF(0.0, (height() - frame_height) / 2.0, width, frame_height), Qt::white);
    }
};

QPushButton* makeButton(QWidget* parent, const QString& text, const QString& name,
    const QString& accessible_name, int width = 28) {
    auto* button = new QPushButton(text, parent);
    button->setObjectName(name);
    button->setAccessibleName(accessible_name);
    button->setToolTip(accessible_name);
    button->setFixedSize(width, 24);
    button->setFocusPolicy(Qt::NoFocus);
    return button;
}

QPushButton* makeToggle(QWidget* parent, const QString& glyph, const QString& name,
    const QString& accessible_name, bool checked) {
    auto* toggle = makeButton(parent, glyph, name, accessible_name, 34);
    toggle->setCheckable(true);
    toggle->setChecked(checked);
    return toggle;
}

struct FieldSpec {
    const char* label;
    const char* name;
    double minimum;
    double maximum;
    double value;
};

}  // namespace

KeyPage::KeyPage(QWidget* parent) : QWidget(parent) {
    setObjectName("colorKeyPage");
    setStyleSheet(QStringLiteral(R"(
        QWidget#colorKeyPage QDoubleSpinBox { padding: 2px 2px; }
        QWidget#colorKeyPage QPushButton:checked { background: #e6ebf0; color: #14181e; }
    )"));
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(6);

    auto* header = new QHBoxLayout;
    auto* title = new QLabel(tr("Key"), this);
    title->setObjectName("colorKeyTitle");
    QFont title_font = title->font();
    title_font.setBold(true);
    title->setFont(title_font);
    header->addWidget(title);
    header->addStretch();
    auto* node = new QLabel(tr("Node Key"), this);
    node->setObjectName("colorKeyNodeLabel");
    QFont node_font = node->font();
    node_font.setBold(true);
    node->setFont(node_font);
    header->addWidget(node);
    auto* reset_all = makeButton(this, QStringLiteral("↺"),
        QStringLiteral("colorKeyResetButton"), tr("Reset Key"));
    header->addWidget(reset_all);
    root->addLayout(header);

    auto* body = new QHBoxLayout;
    body->setSpacing(12);
    auto* preview = new MattePreview(this);
    preview->setObjectName("colorKeyPreview");
    body->addWidget(preview, 3);

    // Right: the three sections.
    auto* side = new QVBoxLayout;
    side->setSpacing(6);
    const auto addSection = [&](const char* label, const QList<QPushButton*>& toggles,
                                const QList<FieldSpec>& fields, bool divider_before) {
        if (divider_before) {
            auto* line = new QFrame(this);
            line->setFrameShape(QFrame::HLine);
            line->setFrameShadow(QFrame::Plain);
            side->addWidget(line);
        }
        auto* head = new QHBoxLayout;
        head->addWidget(new QLabel(tr(label), this));
        head->addStretch();
        for (auto* toggle : toggles) head->addWidget(toggle);
        side->addLayout(head);
        auto* grid = new QGridLayout;
        grid->setHorizontalSpacing(10);
        for (int i = 0; i < fields.size(); ++i) {
            const FieldSpec& spec = fields[i];
            const int row = i / 2;
            const int column = (i % 2) * 2;
            grid->addWidget(new QLabel(tr(spec.label), this), row, column,
                Qt::AlignRight | Qt::AlignVCenter);
            auto* box = new QDoubleSpinBox(this);
            box->setObjectName(QStringLiteral("colorKey%1").arg(QLatin1String(spec.name)));
            box->setRange(spec.minimum, spec.maximum);
            box->setDecimals(3);
            box->setValue(spec.value);
            box->setButtonSymbols(QAbstractSpinBox::NoButtons);
            box->setAlignment(Qt::AlignCenter);
            box->setFixedWidth(64);
            box->setKeyboardTracking(false);
            box->setProperty("defaultValue", spec.value);
            grid->addWidget(box, row, column + 1);
        }
        grid->setColumnStretch(0, 1);
        grid->setColumnStretch(2, 1);
        side->addLayout(grid);
    };
    addSection("Key Input",
        {makeToggle(this, QStringLiteral("●"), QStringLiteral("colorKeyInputEnable"),
             tr("Enable key input"), true),
            makeToggle(this, QStringLiteral("▨"), QStringLiteral("colorKeyInputView"),
                tr("Show key input"), true)},
        {{"Gain", "InputGain", 0.0, 10.0, 1.0}, {"Offset", "InputOffset", -1.0, 1.0, 0.0},
            {"Blur R.", "InputBlurRadius", 0.0, 100.0, 0.0}, {"Blur H/V", "InputBlurHV", 0.0, 100.0, 0.0}},
        false);
    addSection("Key Output",
        {makeToggle(this, QStringLiteral("●"), QStringLiteral("colorKeyOutputEnable"),
            tr("Enable key output"), false)},
        {{"Gain", "OutputGain", 0.0, 10.0, 1.0}, {"Offset", "OutputOffset", -1.0, 1.0, 0.0}}, true);
    addSection("Qualifier",
        {makeToggle(this, QStringLiteral("●"), QStringLiteral("colorKeyQualifierEnable"),
             tr("Enable qualifier key"), false),
            makeToggle(this, QStringLiteral("▨"), QStringLiteral("colorKeyQualifierView"),
                tr("Show qualifier key"), true)},
        {{"Gain", "QualifierGain", 0.0, 10.0, 1.0}, {"Offset", "QualifierOffset", -1.0, 1.0, 0.0}}, true);
    side->addStretch(1);
    auto* side_host = new QWidget(this);
    side_host->setLayout(side);
    side_host->setMinimumWidth(270);
    side_host->setMaximumWidth(360);
    body->addWidget(side_host, 2);
    root->addLayout(body, 1);

    connect(reset_all, &QPushButton::clicked, this, [this] { restoreDefaults(); });
}

void KeyPage::restoreDefaults() {
    for (auto* box : findChildren<QDoubleSpinBox*>())
        box->setValue(box->property("defaultValue").toDouble());
    // Toggle defaults: input on and shown; output off; qualifier off, shown.
    const struct { const char* name; bool checked; } toggles[] = {
        {"colorKeyInputEnable", true}, {"colorKeyInputView", true}, {"colorKeyOutputEnable", false},
        {"colorKeyQualifierEnable", false}, {"colorKeyQualifierView", true},
    };
    for (const auto& toggle : toggles) {
        if (auto* button = findChild<QPushButton*>(QLatin1String(toggle.name)))
            button->setChecked(toggle.checked);
    }
}

}  // namespace ui

#include "window_page.h"

#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <functional>
#include <vector>

namespace ui {
namespace {

QString tr(const char* text) {
    return QCoreApplication::translate("WindowPage", text);
}

enum class WindowShape { Linear, Circle, Polygon, Curve, Gradient };

// WindowShape icon on a dark tile.
class ShapeTile final : public QWidget {
public:
    ShapeTile(WindowShape shape, QWidget* parent) : QWidget(parent), shape_(shape) {
        setFixedSize(46, 40);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor("#14181e"));
        painter.drawRoundedRect(rect(), 4, 4);
        const QRectF box(width() / 2.0 - 9.0, height() / 2.0 - 9.0, 18.0, 18.0);
        painter.setPen(QPen(QColor("#f2f5f8"), 2.2));
        painter.setBrush(Qt::NoBrush);
        switch (shape_) {
        case WindowShape::Linear: painter.drawRect(box); break;
        case WindowShape::Circle: painter.drawEllipse(box); break;
        case WindowShape::Polygon:
            painter.drawLine(box.bottomLeft(), box.topRight());
            break;
        case WindowShape::Curve:
            painter.setFont(QFont(painter.font().family(), 14));
            painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("✒"));
            break;
        case WindowShape::Gradient: {
            QLinearGradient gradient(box.topLeft(), box.bottomRight());
            gradient.setColorAt(0.0, QColor("#f2f5f8"));
            gradient.setColorAt(1.0, QColor("#3a424e"));
            painter.setPen(Qt::NoPen);
            painter.setBrush(gradient);
            painter.drawRect(box);
            break;
        }
        }
    }

private:
    WindowShape shape_;
};

// One window row: shape tile plus two toggles that apply once it is selected.
class WindowRow final : public QFrame {
public:
    WindowRow(WindowShape shape, QWidget* parent) : QFrame(parent) {
        setObjectName("colorWindowRow");
        setProperty("shape", static_cast<int>(shape));
        setFixedHeight(54);
        setFrameShape(QFrame::NoFrame);
        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(6, 4, 8, 4);
        layout->addWidget(new ShapeTile(shape, this));
        layout->addStretch();
        enable_ = new QPushButton(QStringLiteral("◉"), this);
        enable_->setObjectName("colorWindowRowEnableButton");
        enable_->setAccessibleName(tr("Enable window"));
        enable_->setToolTip(tr("Enable window"));
        invert_ = new QPushButton(QStringLiteral("☰"), this);
        invert_->setObjectName("colorWindowRowInvertButton");
        invert_->setAccessibleName(tr("Invert window"));
        invert_->setToolTip(tr("Invert window"));
        for (auto* toggle : {enable_, invert_}) {
            toggle->setCheckable(true);
            toggle->setFixedSize(28, 26);
            toggle->setFocusPolicy(Qt::NoFocus);
            layout->addWidget(toggle);
        }
        enable_->setChecked(true);
        setSelected(false);
    }

    void setSelected(bool selected) {
        selected_ = selected;
        enable_->setEnabled(selected);
        invert_->setEnabled(selected);
        setStyleSheet(selected
                ? QStringLiteral("QFrame#colorWindowRow { background: #2a3442; border-bottom: 1px solid #39424e; }")
                : QStringLiteral("QFrame#colorWindowRow { background: #20242a; border-bottom: 1px solid #14181e; }"));
    }
    [[nodiscard]] bool isSelected() const noexcept { return selected_; }
    [[nodiscard]] QPushButton* invertButton() const noexcept { return invert_; }
    [[nodiscard]] QPushButton* enableButton() const noexcept { return enable_; }
    void setPressedHandler(std::function<void(WindowRow*)> handler) { pressed_ = std::move(handler); }

protected:
    void mousePressEvent(QMouseEvent*) override {
        if (pressed_) pressed_(this);
    }

private:
    QPushButton* enable_ = nullptr;
    QPushButton* invert_ = nullptr;
    bool selected_ = false;
    std::function<void(WindowRow*)> pressed_;
};

// The ordered list of window rows, with single selection.
class WindowList final : public QWidget {
public:
    explicit WindowList(QWidget* parent) : QWidget(parent) {
        layout_ = new QVBoxLayout(this);
        layout_->setContentsMargins(0, 0, 0, 0);
        layout_->setSpacing(0);
        layout_->addStretch(1);
        resetRows();
    }

    void addRow(WindowShape shape) {
        auto* row = new WindowRow(shape, this);
        row->setPressedHandler([this](WindowRow* pressed) { select(pressed); });
        layout_->insertWidget(static_cast<int>(rows_.size()), row);
        rows_.push_back(row);
        select(row);
    }
    void removeSelected() {
        for (auto it = rows_.begin(); it != rows_.end(); ++it) {
            if (!(*it)->isSelected()) continue;
            delete *it;
            rows_.erase(it);
            break;
        }
        select(nullptr);
    }
    // Back to the five sample rows (one per shape) with nothing selected.
    void resetRows() {
        for (auto* row : rows_) delete row;
        rows_.clear();
        for (const WindowShape shape : {WindowShape::Linear, WindowShape::Circle, WindowShape::Polygon, WindowShape::Curve,
                 WindowShape::Gradient}) {
            auto* row = new WindowRow(shape, this);
            row->setPressedHandler([this](WindowRow* pressed) { select(pressed); });
            layout_->insertWidget(static_cast<int>(rows_.size()), row);
            rows_.push_back(row);
        }
        select(nullptr);
    }
    void setSelectionChangedHandler(std::function<void(bool)> handler) {
        selection_changed_ = std::move(handler);
    }
    void setRowSelectedIdHandler(std::function<void(std::optional<color::PowerWindowId>)> handler) {
        selected_id_changed_ = std::move(handler);
    }
    void syncFromWindows(const std::vector<color::PowerWindow>& windows,
                         std::optional<color::PowerWindowId> selected_id,
                         std::function<void(color::PowerWindowId, bool)> on_invert) {
        for (auto* row : rows_) delete row;
        rows_.clear();
        WindowRow* to_select = nullptr;
        for (const auto& w : windows) {
            const auto shape = w.kind == creative_suite::composition::ShapeMaskKind::Ellipse
                ? WindowShape::Circle
                : WindowShape::Linear;
            auto* row = new WindowRow(shape, this);
            row->setProperty("windowId", QVariant::fromValue<qulonglong>(w.id));
            {
                const QSignalBlocker b(row->invertButton());
                row->invertButton()->setChecked(w.parameters.invert);
            }
            const auto wid = w.id;
            QObject::connect(row->invertButton(), &QPushButton::toggled, row,
                [wid, on_invert](bool checked) {
                    if (on_invert) on_invert(wid, checked);
                });
            row->setPressedHandler([this](WindowRow* pressed) { select(pressed); });
            layout_->insertWidget(static_cast<int>(rows_.size()), row);
            rows_.push_back(row);
            if (selected_id && *selected_id == wid) to_select = row;
        }
        if (!to_select && !rows_.empty()) to_select = rows_.front();
        select(to_select);
    }

private:
    void select(WindowRow* row) {
        int selected = -1;
        std::optional<color::PowerWindowId> wid;
        for (std::size_t i = 0; i < rows_.size(); ++i) {
            rows_[i]->setSelected(rows_[i] == row);
            if (rows_[i] == row) {
                selected = static_cast<int>(i);
                const QVariant prop = rows_[i]->property("windowId");
                if (prop.isValid()) wid = prop.toULongLong();
            }
        }
        // Counts and selection are exposed as properties for tests.
        setProperty("rowCount", static_cast<int>(rows_.size()));
        setProperty("selectedRow", selected);
        if (selection_changed_) selection_changed_(selected >= 0);
        if (selected_id_changed_) selected_id_changed_(wid);
    }

    QVBoxLayout* layout_ = nullptr;
    std::vector<WindowRow*> rows_;
    std::function<void(bool)> selection_changed_;
    std::function<void(std::optional<color::PowerWindowId>)> selected_id_changed_;
};

struct FieldSpec {
    const char* label;
    const char* name;
    double minimum;
    double maximum;
    double value;
};

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

QDoubleSpinBox* makeSpin(QWidget* parent, const QString& name, double minimum, double maximum,
    double value) {
    auto* box = new QDoubleSpinBox(parent);
    box->setObjectName(name);
    box->setRange(minimum, maximum);
    box->setDecimals(2);
    box->setValue(value);
    box->setButtonSymbols(QAbstractSpinBox::NoButtons);
    box->setAlignment(Qt::AlignCenter);
    box->setFixedWidth(62);
    box->setKeyboardTracking(false);
    box->setProperty("defaultValue", value);
    return box;
}

}  // namespace

WindowPage::WindowPage(QWidget* parent) : QWidget(parent) {
    setObjectName("colorWindowPage");
    setStyleSheet(QStringLiteral(R"(
        QWidget#colorWindowPage QDoubleSpinBox { padding: 2px 2px; }
        QWidget#colorWindowPage QPushButton:checked { background: #2f5f8f; }
    )"));
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(6);

    auto* header = new QHBoxLayout;
    auto* title = new QLabel(tr("Window"), this);
    title->setObjectName("colorWindowTitle");
    QFont title_font = title->font();
    title_font.setBold(true);
    title->setFont(title_font);
    header->addWidget(title);
    header->addStretch();
    auto* reset_all = makeButton(this, QStringLiteral("↺"),
        QStringLiteral("colorWindowResetButton"), tr("Reset Window"));
    header->addWidget(reset_all);
    header->addWidget(makeButton(this, QStringLiteral("…"),
        QStringLiteral("colorWindowMoreButton"), tr("More actions")));
    root->addLayout(header);

    // Add buttons and Delete.
    auto* actions = new QHBoxLayout;
    actions->setSpacing(4);
    const struct { const char* name; const char* label; WindowShape shape; } add_specs[] = {
        {"Linear", "Linear", WindowShape::Linear}, {"Circle", "Circle", WindowShape::Circle},
        {"Polygon", "Polygon", WindowShape::Polygon}, {"Curve", "Curve", WindowShape::Curve},
        {"Gradient", "Gradient", WindowShape::Gradient},
    };
    auto* list = new WindowList(this);
    list->setObjectName("colorWindowList");
    for (const auto& spec : add_specs) {
        auto* add = new QPushButton(QStringLiteral("+ ") + tr(spec.label), this);
        add->setObjectName(QStringLiteral("colorWindowAdd%1Button").arg(QLatin1String(spec.name)));
        add->setAccessibleName(tr("Add") + QStringLiteral(" ") + tr(spec.label));
        add->setFocusPolicy(Qt::NoFocus);
        actions->addWidget(add);
        const WindowShape shape = spec.shape;
        connect(add, &QPushButton::clicked, this, [this, list, shape] {
            if (bound_ && on_add_) {
                const auto kind = (shape == WindowShape::Circle)
                    ? creative_suite::composition::ShapeMaskKind::Ellipse
                    : creative_suite::composition::ShapeMaskKind::Rectangle;
                on_add_(kind);
                return;
            }
            list->addRow(shape);
        });
    }
    actions->addStretch();
    auto* delete_button = new QPushButton(tr("Delete"), this);
    delete_button->setObjectName("colorWindowDeleteButton");
    delete_button->setFocusPolicy(Qt::NoFocus);
    delete_button->setEnabled(false);
    actions->addWidget(delete_button);
    root->addLayout(actions);

    auto* body = new QHBoxLayout;
    body->setSpacing(12);
    auto* scroll = new QScrollArea(this);
    scroll->setObjectName("colorWindowListScroll");
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setMinimumHeight(230);
    scroll->setWidget(list);
    body->addWidget(scroll, 3);

    // Right: Transform and Softness fields, active only while a row is selected.
    auto* side = new QVBoxLayout;
    side->setSpacing(6);
    const auto addSection = [&](const char* label, const FieldSpec* fields, int count) {
        side->addWidget(new QLabel(tr(label), this));
        auto* grid = new QGridLayout;
        grid->setHorizontalSpacing(8);
        for (int i = 0; i < count; ++i) {
            const int row = i / 2;
            const int column = (i % 2) * 2;
            grid->addWidget(new QLabel(tr(fields[i].label), this), row, column,
                Qt::AlignRight | Qt::AlignVCenter);
            grid->addWidget(makeSpin(this,
                                 QStringLiteral("colorWindow%1").arg(QLatin1String(fields[i].name)),
                                 fields[i].minimum, fields[i].maximum, fields[i].value),
                row, column + 1);
        }
        side->addLayout(grid);
    };
    const FieldSpec transform[] = {
        {"Size", "Size", 0.0, 100.0, 50.0}, {"Aspect", "Aspect", 0.0, 100.0, 50.0},
        {"Pan", "Pan", 0.0, 100.0, 50.0}, {"Tilt", "Tilt", 0.0, 100.0, 50.0},
        {"Rotate", "Rotate", -180.0, 180.0, 0.0}, {"Opacity", "Opacity", 0.0, 100.0, 100.0},
    };
    const FieldSpec softness[] = {
        {"Soft 1", "Soft1", 0.0, 100.0, 2.07}, {"Soft 2", "Soft2", 0.0, 100.0, 50.0},
        {"Soft 3", "Soft3", 0.0, 100.0, 50.0}, {"Soft 4", "Soft4", 0.0, 100.0, 50.0},
        {"Inside", "Inside", 0.0, 100.0, 50.0}, {"Outside", "Outside", 0.0, 100.0, 50.0},
    };
    addSection("Transform", transform, 6);
    auto* divider = new QFrame(this);
    divider->setFrameShape(QFrame::HLine);
    divider->setFrameShadow(QFrame::Plain);
    side->addWidget(divider);
    addSection("Softness", softness, 6);
    side->addStretch(1);
    auto* side_host = new QWidget(this);
    side_host->setLayout(side);
    side_host->setMinimumWidth(250);
    side_host->setMaximumWidth(330);
    body->addWidget(side_host, 2);
    root->addLayout(body, 1);

    const auto setFieldsEnabled = [this, delete_button](bool selected) {
        for (auto* box : findChildren<QDoubleSpinBox*>()) box->setEnabled(selected);
        delete_button->setEnabled(selected);
    };
    list->setSelectionChangedHandler(setFieldsEnabled);
    list->setRowSelectedIdHandler([this](std::optional<color::PowerWindowId> id) {
        if (!updating_ && bound_ && on_select_) {
            selected_id_ = id;
            on_select_(id);
        }
    });
    for (const char* field : {"colorWindowSize", "colorWindowAspect", "colorWindowPan",
                              "colorWindowTilt", "colorWindowRotate", "colorWindowSoft1"}) {
        if (auto* box = findChild<QDoubleSpinBox*>(QLatin1String(field))) {
            connect(box, &QDoubleSpinBox::editingFinished, this, [this] {
                if (updating_ || !bound_ || !selected_id_) return;
                const auto val = [this](const char* n, double def) {
                    auto* b = findChild<QDoubleSpinBox*>(QLatin1String(n));
                    return b ? b->value() : def;
                };
                if (on_geometry_) {
                    const double size = std::clamp(val("colorWindowSize", 50.0) / 100.0, 0.02, 2.0);
                    const double aspect = std::clamp(val("colorWindowAspect", 50.0) / 50.0, 0.1, 4.0);
                    const double cx = std::clamp(val("colorWindowPan", 50.0) / 100.0, 0.0, 1.0);
                    const double cy = std::clamp(val("colorWindowTilt", 50.0) / 100.0, 0.0, 1.0);
                    const double rot = val("colorWindowRotate", 0.0);
                    on_geometry_(*selected_id_, cx, cy, size * aspect, size, rot);
                }
                if (on_settings_) {
                    const double soft = std::clamp(val("colorWindowSoft1", 2.0) / 100.0, 0.0, 1.0);
                    on_settings_(*selected_id_, soft, false, color::WindowCombineMode::Add);
                }
            });
        }
    }
    setFieldsEnabled(false);
    connect(delete_button, &QPushButton::clicked, this, [this, list] {
        if (bound_ && on_remove_ && selected_id_) {
            on_remove_(*selected_id_);
            return;
        }
        list->removeSelected();
    });
    connect(reset_all, &QPushButton::clicked, this, [this] { restoreDefaults(); });
}

void WindowPage::setHandlers(
    std::function<void(creative_suite::composition::ShapeMaskKind)> on_add,
    std::function<void(color::PowerWindowId)> on_remove,
    std::function<void(std::optional<color::PowerWindowId>)> on_select,
    std::function<void(color::PowerWindowId, double, bool, color::WindowCombineMode)> on_settings,
    std::function<void(color::PowerWindowId, double, double, double, double, double)> on_geometry) {
    on_add_ = std::move(on_add);
    on_remove_ = std::move(on_remove);
    on_select_ = std::move(on_select);
    on_settings_ = std::move(on_settings);
    on_geometry_ = std::move(on_geometry);
}

void WindowPage::syncWindows(const std::vector<color::PowerWindow>& windows,
                             std::optional<color::PowerWindowId> selected_id,
                             std::int64_t frame) {
    bound_ = true;
    updating_ = true;
    selected_id_ = selected_id;
    if (auto* list = dynamic_cast<WindowList*>(findChild<QWidget*>("colorWindowList"))) {
        list->syncFromWindows(windows, selected_id,
            [this, windows](color::PowerWindowId wid, bool inv) {
                if (updating_ || !on_settings_) return;
                const auto found = std::find_if(windows.begin(), windows.end(),
                    [wid](const auto& w) { return w.id == wid; });
                if (found != windows.end()) {
                    on_settings_(wid, found->parameters.soft_edge, inv, found->combine_mode);
                }
            });
    }
    const color::PowerWindow* active = nullptr;
    if (selected_id_) {
        const auto found = std::find_if(windows.begin(), windows.end(),
            [this](const auto& w) { return w.id == *selected_id_; });
        if (found != windows.end()) active = &(*found);
    }
    if (active) {
        const auto params = creative_suite::composition::evaluateShapeMask(
            active->parameters, active->keyframes, frame);
        const auto set_spin = [this](const char* n, double v) {
            if (auto* b = findChild<QDoubleSpinBox*>(QLatin1String(n))) {
                const QSignalBlocker blocker(b);
                b->setValue(v);
            }
        };
        set_spin("colorWindowPan", params.center_x * 100.0);
        set_spin("colorWindowTilt", params.center_y * 100.0);
        set_spin("colorWindowSize", params.height * 100.0);
        set_spin("colorWindowAspect", params.height > 1e-4 ? (params.width / params.height) * 50.0 : 50.0);
        set_spin("colorWindowRotate", params.rotation);
        set_spin("colorWindowSoft1", params.soft_edge * 100.0);
    }
    updating_ = false;
}

void WindowPage::restoreDefaults() {
    for (auto* box : findChildren<QDoubleSpinBox*>())
        box->setValue(box->property("defaultValue").toDouble());
    // WindowList has no Q_OBJECT, so match it with dynamic_cast.
    if (auto* list = dynamic_cast<WindowList*>(findChild<QWidget*>("colorWindowList")))
        list->resetRows();
}

}  // namespace ui

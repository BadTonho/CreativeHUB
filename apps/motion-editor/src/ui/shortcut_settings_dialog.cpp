#include "shortcut_settings_dialog.h"

#include <QAbstractItemView>
#include <QDialogButtonBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QPushButton>
#include <QScreen>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace motion::ui {

ShortcutSettingsDialog::ShortcutSettingsDialog(
    const std::vector<creative_suite::shortcuts::ShortcutEntry>& entries,
    QWidget* parent)
    : QDialog(parent), entries_(entries)
{
    setObjectName(QStringLiteral("motion-shortcut-settings-dialog"));
    setWindowTitle(QStringLiteral("Keyboard Shortcuts"));
    setSizeGripEnabled(true);
    QScreen* target_screen = parent != nullptr ? parent->screen() : nullptr;
    if (target_screen == nullptr) target_screen = QGuiApplication::primaryScreen();
    const QSize available = target_screen != nullptr
        ? target_screen->availableGeometry().size() : QSize(900, 700);
    const QSize preferred_size(760, 520);
    const QSize screen_limit(
        static_cast<int>(available.width() * 0.9),
        static_cast<int>(available.height() * 0.9));
    const QSize initial_size(std::min(preferred_size.width(), screen_limit.width()),
                             std::min(preferred_size.height(), screen_limit.height()));
    setMinimumSize(std::min(620, initial_size.width()),
                   std::min(420, initial_size.height()));
    resize(initial_size);

    auto* layout = new QVBoxLayout(this);
    auto* table = new QTableWidget(static_cast<int>(entries_.size()), 2, this);
    table->setObjectName(QStringLiteral("motion-shortcut-settings-table"));
    table->setHorizontalHeaderLabels(
        {QStringLiteral("Command"), QStringLiteral("Shortcut")});
    table->verticalHeader()->setVisible(false);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionMode(QAbstractItemView::NoSelection);
    table->setFocusPolicy(Qt::NoFocus);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);

    editors_.reserve(entries_.size());
    for (std::size_t row = 0; row < entries_.size(); ++row) {
        const auto& entry = entries_[row];
        auto* command_item = new QTableWidgetItem(entry.label);
        command_item->setFlags(command_item->flags() & ~Qt::ItemIsEditable);
        table->setItem(static_cast<int>(row), 0, command_item);
        table->setRowHeight(static_cast<int>(row), 40);

        auto* editor_container = new QWidget(table);
        auto* editor_layout = new QHBoxLayout(editor_container);
        editor_layout->setContentsMargins(4, 2, 4, 2);
        editor_layout->setSpacing(6);
        auto* editor = new QKeySequenceEdit(entry.action->shortcut(), editor_container);
        editor->setObjectName(QStringLiteral("motion-shortcut-editor-%1").arg(entry.id));
        editor->setAccessibleName(QStringLiteral("Shortcut for %1").arg(entry.label));
        editors_.push_back(editor);
        editor_layout->addWidget(editor, 1);

        auto* clear_button = new QPushButton(QStringLiteral("Clear"), editor_container);
        clear_button->setObjectName(QStringLiteral("motion-shortcut-clear-%1").arg(entry.id));
        clear_button->setToolTip(QStringLiteral("Remove this shortcut"));
        editor_layout->addWidget(clear_button);
        connect(clear_button, &QPushButton::clicked, editor, [editor] { editor->clear(); });
        connect(editor, &QKeySequenceEdit::keySequenceChanged,
                this, [this] { showValidationMessage({}); });
        table->setCellWidget(static_cast<int>(row), 1, editor_container);
    }
    layout->addWidget(table, 1);

    validation_label_ = new QLabel(this);
    validation_label_->setObjectName(QStringLiteral("motion-shortcut-validation-message"));
    validation_label_->setStyleSheet(QStringLiteral("color: #ff7777;"));
    validation_label_->setWordWrap(true);
    validation_label_->hide();
    layout->addWidget(validation_label_);

    auto* bottom_row = new QHBoxLayout;
    auto* reset_button = new QPushButton(QStringLiteral("Reset All"), this);
    reset_button->setObjectName(QStringLiteral("motion-shortcut-reset-all"));
    bottom_row->addWidget(reset_button);
    bottom_row->addStretch(1);
    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->setObjectName(QStringLiteral("motion-shortcut-dialog-buttons"));
    buttons->button(QDialogButtonBox::Ok)->setObjectName(
        QStringLiteral("motion-shortcut-dialog-ok"));
    buttons->button(QDialogButtonBox::Cancel)->setObjectName(
        QStringLiteral("motion-shortcut-dialog-cancel"));
    bottom_row->addWidget(buttons);
    layout->addLayout(bottom_row);

    connect(reset_button, &QPushButton::clicked,
            this, [this] { resetToDefaults(); });
    connect(buttons, &QDialogButtonBox::accepted,
            this, &ShortcutSettingsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected,
            this, &ShortcutSettingsDialog::reject);
}

std::vector<creative_suite::shortcuts::ShortcutAssignment>
ShortcutSettingsDialog::assignments() const
{
    std::vector<creative_suite::shortcuts::ShortcutAssignment> result;
    result.reserve(entries_.size());
    for (std::size_t index = 0; index < entries_.size(); ++index) {
        result.push_back({entries_[index].id, editors_[index]->keySequence()});
    }
    return result;
}

void ShortcutSettingsDialog::accept()
{
    const auto current = assignments();
    for (std::size_t first = 0; first < current.size(); ++first) {
        if (current[first].sequence.isEmpty()) continue;
        for (std::size_t second = 0; second < first; ++second) {
            if (current[first].sequence != current[second].sequence) continue;
            const auto text = current[first].sequence.toString(QKeySequence::NativeText);
            showValidationMessage(
                QStringLiteral("%1 is assigned to both %2 and %3. Choose a unique shortcut.")
                    .arg(text, entries_[second].label, entries_[first].label));
            editors_[first]->setFocus();
            return;
        }
    }
    QDialog::accept();
}

void ShortcutSettingsDialog::resetToDefaults()
{
    for (std::size_t index = 0; index < entries_.size(); ++index) {
        editors_[index]->setKeySequence(entries_[index].default_sequence);
    }
    showValidationMessage({});
}

void ShortcutSettingsDialog::showValidationMessage(const QString& message)
{
    validation_label_->setText(message);
    validation_label_->setVisible(!message.isEmpty());
}

} // namespace motion::ui

#include "shortcut_settings_dialog.h"

#include <QAbstractItemView>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace image_editor {

ShortcutSettingsDialog::ShortcutSettingsDialog(
    const QList<ShortcutBinding>& bindings, QWidget* parent)
    : QDialog(parent), initial_bindings_(bindings) {
    setObjectName(QStringLiteral("shortcutSettingsDialog"));
    setWindowTitle(QStringLiteral("Keyboard Shortcuts"));
    resize(620, 520);

    auto* layout = new QVBoxLayout(this);
    auto* table = new QTableWidget(static_cast<int>(bindings.size()), 2, this);
    table->setObjectName(QStringLiteral("shortcutSettingsTable"));
    table->setHorizontalHeaderLabels(
        {QStringLiteral("Command"), QStringLiteral("Shortcut")});
    table->verticalHeader()->setVisible(false);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionMode(QAbstractItemView::NoSelection);
    table->setFocusPolicy(Qt::NoFocus);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);

    editors_.reserve(bindings.size());
    for (qsizetype row = 0; row < bindings.size(); ++row) {
        const auto& binding = bindings.at(row);
        auto* command_item = new QTableWidgetItem(binding.label);
        command_item->setFlags(command_item->flags() & ~Qt::ItemIsEditable);
        table->setItem(static_cast<int>(row), 0, command_item);
        table->setRowHeight(static_cast<int>(row), 40);

        auto* editor_container = new QWidget(table);
        auto* editor_layout = new QHBoxLayout(editor_container);
        editor_layout->setContentsMargins(4, 2, 4, 2);
        editor_layout->setSpacing(6);

        auto* editor = new QKeySequenceEdit(binding.sequence, editor_container);
        editor->setObjectName(QStringLiteral("shortcutEditor_%1").arg(binding.id));
        editor->setAccessibleName(QStringLiteral("Shortcut for %1").arg(binding.label));
        editors_.append(editor);
        editor_layout->addWidget(editor, 1);

        auto* clear_button = new QPushButton(QStringLiteral("Clear"), editor_container);
        clear_button->setObjectName(QStringLiteral("clearShortcut_%1").arg(binding.id));
        clear_button->setToolTip(QStringLiteral("Remove this shortcut"));
        editor_layout->addWidget(clear_button);
        QObject::connect(clear_button, &QPushButton::clicked, editor,
                         [editor]() { editor->clear(); });
        QObject::connect(editor, &QKeySequenceEdit::keySequenceChanged,
                         this, [this]() { showValidationMessage({}); });
        table->setCellWidget(static_cast<int>(row), 1, editor_container);
    }
    layout->addWidget(table, 1);

    validation_label_ = new QLabel(this);
    validation_label_->setObjectName(QStringLiteral("shortcutValidationMessage"));
    validation_label_->setStyleSheet(QStringLiteral("color: #ff7777;"));
    validation_label_->setWordWrap(true);
    validation_label_->hide();
    layout->addWidget(validation_label_);

    auto* bottom_row = new QHBoxLayout;
    auto* reset_button = new QPushButton(QStringLiteral("Reset All"), this);
    reset_button->setObjectName(QStringLiteral("resetAllShortcutsButton"));
    bottom_row->addWidget(reset_button);
    bottom_row->addStretch(1);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->setObjectName(QStringLiteral("shortcutSettingsButtons"));
    buttons->button(QDialogButtonBox::Ok)->setObjectName(
        QStringLiteral("shortcutSettingsOkButton"));
    buttons->button(QDialogButtonBox::Cancel)->setObjectName(
        QStringLiteral("shortcutSettingsCancelButton"));
    bottom_row->addWidget(buttons);
    layout->addLayout(bottom_row);

    QObject::connect(reset_button, &QPushButton::clicked,
                     this, [this]() { resetToDefaults(); });
    QObject::connect(buttons, &QDialogButtonBox::accepted,
                     this, &ShortcutSettingsDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected,
                     this, &ShortcutSettingsDialog::reject);
}

QList<ShortcutBinding> ShortcutSettingsDialog::bindings() const {
    QList<ShortcutBinding> result = initial_bindings_;
    for (qsizetype index = 0; index < result.size(); ++index) {
        result[index].sequence = editors_.at(index)->keySequence();
    }
    return result;
}

void ShortcutSettingsDialog::accept() {
    const auto current_bindings = bindings();
    for (qsizetype i = 0; i < current_bindings.size(); ++i) {
        const auto& candidate = current_bindings.at(i);
        if (candidate.sequence.isEmpty()) continue;
        for (qsizetype j = 0; j < i; ++j) {
            const auto& earlier = current_bindings.at(j);
            if (candidate.sequence == earlier.sequence) {
                const auto sequence_text = candidate.sequence.toString(
                    QKeySequence::NativeText);
                showValidationMessage(
                    QStringLiteral("%1 is assigned to both %2 and %3. Choose a unique shortcut.")
                        .arg(sequence_text, earlier.label, candidate.label));
                editors_.at(i)->setFocus();
                return;
            }
        }
    }
    QDialog::accept();
}

void ShortcutSettingsDialog::resetToDefaults() {
    for (qsizetype index = 0; index < initial_bindings_.size(); ++index) {
        editors_.at(index)->setKeySequence(initial_bindings_.at(index).default_sequence);
    }
    showValidationMessage({});
}

void ShortcutSettingsDialog::showValidationMessage(const QString& message) {
    validation_label_->setText(message);
    validation_label_->setVisible(!message.isEmpty());
}

} // namespace image_editor

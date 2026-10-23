#include "ui/translation_helpers.h"
#include "motion_png_sequence_export_dialog.h"

#include "export/motion_video_export.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include <filesystem>
#include <string>
#include <utility>

namespace motion::ui {
namespace {

std::filesystem::path pathFromQString(const QString& value)
{
    const auto bytes = value.toUtf8();
    const auto* begin = reinterpret_cast<const char8_t*>(bytes.constData());
    return std::filesystem::path(std::u8string(begin, begin + bytes.size()));
}

bool validDirectoryName(const QString& name)
{
    if (name.isEmpty() || name == QStringLiteral(".") || name == QStringLiteral("..") ||
        name.endsWith(QLatin1Char('.')) || name.endsWith(QLatin1Char(' '))) return false;
    for (const auto character : name) {
        if (character.unicode() < 32 || QStringLiteral("<>:\"/\\|?*").contains(character))
            return false;
    }
    return true;
}

} // namespace

MotionPngSequenceExportDialog::MotionPngSequenceExportDialog(
    QString initial_parent_directory,
    QString initial_directory_name,
    QWidget* parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("motion-png-sequence-export-dialog"));
    setWindowTitle(MotionUiTranslation::tr("Export PNG Sequence"));
    setModal(true);
    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout();

    auto* parent_row = new QWidget(this);
    auto* parent_layout = new QHBoxLayout(parent_row);
    parent_layout->setContentsMargins(0, 0, 0, 0);
    parent_directory_ = new QLineEdit(parent_row);
    parent_directory_->setObjectName(QStringLiteral("motion-png-sequence-parent"));
    parent_directory_->setText(std::move(initial_parent_directory));
    auto* browse = new QPushButton(MotionUiTranslation::tr("Browse..."), parent_row);
    browse->setObjectName(QStringLiteral("motion-png-sequence-browse"));
    parent_layout->addWidget(parent_directory_, 1);
    parent_layout->addWidget(browse);
    form->addRow(MotionUiTranslation::tr("Parent folder"), parent_row);

    directory_name_ = new QLineEdit(this);
    directory_name_->setObjectName(QStringLiteral("motion-png-sequence-directory-name"));
    directory_name_->setText(std::move(initial_directory_name));
    form->addRow(MotionUiTranslation::tr("New sequence folder"), directory_name_);
    layout->addLayout(form);

    note_ = new QLabel(this);
    note_->setObjectName(QStringLiteral("motion-png-sequence-note"));
    note_->setWordWrap(true);
    layout->addWidget(note_);

    buttons_ = new QDialogButtonBox(
        QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    buttons_->button(QDialogButtonBox::Save)->setText(MotionUiTranslation::tr("Export"));
    buttons_->button(QDialogButtonBox::Save)->setObjectName(
        QStringLiteral("motion-png-sequence-accept"));
    buttons_->button(QDialogButtonBox::Cancel)->setText(MotionUiTranslation::tr("Cancel"));
    buttons_->button(QDialogButtonBox::Cancel)->setObjectName(
        QStringLiteral("motion-png-sequence-cancel"));
    layout->addWidget(buttons_);

    connect(browse, &QPushButton::clicked, this,
            [this] { browseParentDirectory(); });
    connect(parent_directory_, &QLineEdit::textChanged, this,
            [this] { updateAcceptState(); });
    connect(directory_name_, &QLineEdit::textChanged, this,
            [this] { updateAcceptState(); });
    connect(buttons_, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);
    updateAcceptState();
    resize(560, sizeHint().height());
}

std::optional<MotionPngSequenceSettings>
MotionPngSequenceExportDialog::exportSettings() const
{
    const auto parent = parent_directory_->text().trimmed();
    const auto name = directory_name_->text().trimmed();
    if (parent.isEmpty() || !validDirectoryName(name) ||
        !QFileInfo(parent).isDir() || QFileInfo(QDir(parent).filePath(name)).exists())
        return std::nullopt;
    return MotionPngSequenceSettings{
        pathFromQString(parent), name.toUtf8().toStdString()};
}

void MotionPngSequenceExportDialog::browseParentDirectory()
{
    const auto selected = QFileDialog::getExistingDirectory(
        this, MotionUiTranslation::tr("Choose Parent Folder"), parent_directory_->text());
    if (!selected.isEmpty()) parent_directory_->setText(selected);
}

void MotionPngSequenceExportDialog::updateAcceptState()
{
    const auto parent = parent_directory_->text().trimmed();
    const auto name = directory_name_->text().trimmed();
    const bool valid_parent = !parent.isEmpty() && QFileInfo(parent).isDir();
    const bool valid_name = validDirectoryName(name);
    const bool destination_exists = valid_parent && valid_name &&
        QFileInfo(QDir(parent).filePath(name)).exists();
    buttons_->button(QDialogButtonBox::Save)->setEnabled(
        valid_parent && valid_name && !destination_exists);
    if (!valid_parent) {
        note_->setText(MotionUiTranslation::tr("Choose an existing parent folder."));
    } else if (!valid_name) {
        note_->setText(MotionUiTranslation::tr(
            "Enter a valid folder name without path separators or reserved characters."));
    } else if (destination_exists) {
        note_->setText(MotionUiTranslation::tr(
            "That folder already exists. Choose a new folder name; existing files are never replaced."));
    } else {
        note_->setText(MotionUiTranslation::tr(
            "Exports transparent PNG frames at the composition size and frame rate. The new folder is published when every frame is ready."));
    }
}

} // namespace motion::ui

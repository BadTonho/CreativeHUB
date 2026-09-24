#pragma once

#include <QDialog>
#include <QKeySequence>
#include <QList>
#include <QString>

class QKeySequenceEdit;
class QLabel;

namespace image_editor {

struct ShortcutBinding {
    QString id;
    QString label;
    QKeySequence default_sequence;
    QKeySequence sequence;
};

class ShortcutSettingsDialog final : public QDialog {
public:
    explicit ShortcutSettingsDialog(const QList<ShortcutBinding>& bindings,
                                    QWidget* parent = nullptr);

    [[nodiscard]] QList<ShortcutBinding> bindings() const;

protected:
    void accept() override;

private:
    void resetToDefaults();
    void showValidationMessage(const QString& message);

    QList<ShortcutBinding> initial_bindings_;
    QList<QKeySequenceEdit*> editors_;
    QLabel* validation_label_ = nullptr;
};

} // namespace image_editor

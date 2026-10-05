#pragma once

#include "image_document_store.h"

#include <QDialog>
#include <QList>

class QButtonGroup;
class QToolButton;

namespace image_editor {

class ShapePalette final : public QDialog {
    Q_OBJECT

public:
    explicit ShapePalette(QWidget* parent = nullptr);

    void setDocumentAvailable(bool available);
    void setSelectedKind(ImageShapeKind kind);
    void showNear(QWidget* anchor);

signals:
    void shapeKindSelected(int kind);

private:
    QButtonGroup* button_group_ = nullptr;
    QList<QToolButton*> buttons_;
    bool positioned_ = false;
};

} // namespace image_editor

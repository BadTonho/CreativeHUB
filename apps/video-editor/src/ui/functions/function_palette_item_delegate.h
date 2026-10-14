#pragma once

#include <QIcon>
#include <QStyledItemDelegate>

namespace ui {

// Vector icons and painted rows keep the searchable list lightweight and native.
QIcon functionPaletteIcon(const QString& id);

class FunctionPaletteItemDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override;
};

} // namespace ui

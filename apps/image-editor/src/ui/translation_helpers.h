#pragma once

#include <QByteArray>
#include <QCoreApplication>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QHash>

namespace image_editor::ui {

[[maybe_unused]] inline constexpr const char* kDynamicUiMessages[] = {
    QT_TRANSLATE_NOOP("ImageEditorUI", "An Exposure / Offset / Gamma adjustment is invalid."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "A Brightness/Contrast adjustment is invalid."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "A layer outline has invalid properties."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "Group masks and outlines are not supported."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "A document group has invalid properties."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "A document group has too many operations."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "A document layer has invalid properties."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "A document path is required."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "A group contains an invalid child layer."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "A group contains an unsupported operation."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "A layer has an invalid group parent."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "A layer mask contains an unsupported operation or non-grayscale color."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "A layer mask has invalid properties."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "A recovery path is required."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "A selected object is no longer available."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "A shape with this ID already exists."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "An image source path is required."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "An object with this ID already exists."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "Choose a path for the editable document."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "Choose a valid paint color."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "Choose an image with the original dimensions for this reference."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "Choose an image with the original dimensions."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "Choose valid canvas dimensions and a valid background."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "Choose valid canvas dimensions before resizing."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "Group Selected requires contiguous sibling layers."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "Group masks are not supported."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "Group names must contain between 1 and 128 characters."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "Groups can contain editable raster layers only."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "Layer names must contain between 1 and 128 characters."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "Only distinct root raster layers can be grouped."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "Open a document and choose a batch within the layer limit."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "Open or relink an image before creating a shape."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "Open or relink an image before creating text."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "Open or relink an image before cropping."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "Open or relink an image before erasing."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "Open or relink an image before painting."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "Relink the source image before saving the document."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "Select a group to apply a group transform."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "Select a layer mask before filling it."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "Select a layer mask before using the Magic Wand on a mask."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "Select a raster layer with a mask before painting its mask."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "Select an editable layer before applying a gradient."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "Select an editable layer before applying blur."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "Select an editable layer before cropping."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "Select an editable layer before erasing."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "Select an editable layer before filling."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "Select an editable layer before painting."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "Select an editable layer before using the Magic Wand."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "Select an editable layer or layer mask before applying a gradient."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "Select an editable layer or layer mask before applying blur."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "Select at least two contiguous root layers to group."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The Background layer cannot be renamed."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The Background layer is invalid."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The Magic Wand seed exceeds the supported local coordinate range."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The active layer could not be rendered for Magic Wand selection."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The blur brush size or radius is out of range."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The blur coverage buffer could not be allocated."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The blur filter buffers could not be allocated."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The blur selection could not be clipped to the canvas."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The blur selection has invalid or excessive geometry."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The blur stroke contains a non-finite point."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The blur stroke contains a point outside the image."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The blur stroke exceeds the supported local coordinate range."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The blur stroke has an invalid number of points."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The blur stroke has invalid parameters."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The blur target could not be converted to premultiplied RGBA."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The blur target image is empty."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The canvas base is invalid or too large."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The canvas change would move existing content beyond the supported edit range."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The canvas change would move the base image beyond the supported edit range."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The canvas could not be allocated. Try smaller dimensions."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The crop area is empty."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The current canvas dimensions cannot be resized."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The decoded document output is invalid."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The document base dimensions are invalid."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The document base type is not supported."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The document canvas dimensions or base offset are invalid."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The document contains a duplicate mask stroke ID."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The document contains an invalid blur stroke point."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The document contains an invalid blur stroke."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The document contains an invalid bucket fill."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The document contains an invalid crop."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The document contains an invalid edit."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The document contains an invalid erase stroke point."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The document contains an invalid erase stroke."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The document contains an invalid layer."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The document contains an invalid linear gradient."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The document contains an invalid or duplicate group ID."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The document contains an invalid or duplicate layer ID."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The document contains an invalid or duplicate object ID."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The document contains an invalid paint stroke point."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The document contains an invalid paint stroke."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The document contains an invalid rotation."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The document contains an invalid shape."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The document contains an out-of-bounds blur stroke point."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The document contains an out-of-bounds erase stroke point."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The document contains an out-of-bounds paint stroke point."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The document contains an unsupported edit."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The document contains an unsupported layer type."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The document contains an unsupported shape type."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The document contains invalid text data."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The document contains invalid transform bounds."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The document edit list is invalid."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The document exceeds the 512-item layer limit."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The document has reached the maximum of 512 stack items."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The document has too many edit operations."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The document layer stack is invalid."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The document must contain exactly one Background layer."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The document path or image base is invalid."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The document root stack is incomplete."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The document root stack is invalid."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The document source reference is invalid."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The encoded document output is invalid."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The erase selection has invalid or excessive geometry."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The erase stroke contains a point outside the image."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The erase stroke exceeds the supported local coordinate range."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The erase stroke has an invalid number of points."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The eraser diameter must be between 1 and 1024 pixels."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The fill color or tolerance is invalid."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The fill image exceeds the supported canvas size."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The fill image, seed, color, or tolerance is invalid."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The fill seed exceeds the supported local coordinate range."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The fill selection has invalid geometry."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The fill selection has invalid or excessive geometry."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The gradient color is invalid."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The gradient exceeds the supported local coordinate range."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The gradient gesture must stay within the canvas."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The gradient image exceeds the supported canvas size."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The gradient image or geometry is invalid."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The gradient selection could not be clipped to the canvas."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The gradient selection has invalid geometry."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The gradient selection has invalid or excessive geometry."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The imported image batch is invalid."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The imported image geometry is invalid."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The imported image reference or affine geometry is invalid."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The layer or its mask has reached the operation limit."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The mask selection has invalid or excessive geometry."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The mask stroke color, size, or operation count is invalid."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The mask stroke contains a point outside the canvas."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The paint brush diameter must be between 1 and 1024 pixels."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The paint selection has invalid or excessive geometry."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The paint stroke contains a point outside the image."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The paint stroke exceeds the supported local coordinate range."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The paint stroke has an invalid number of points."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The recovered canvas could not be allocated."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The recovery document is invalid."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The recovery source dimensions no longer match."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The selected layer could not be rendered for blur."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The selected layer could not be rendered for filling."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The selected layer could not be rendered for the gradient."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The selected object geometry is invalid."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The selected objects are invalid or duplicated."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The selected shape no longer exists."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The selected text no longer exists."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The shape geometry or style is invalid."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The source image dimensions no longer match the document."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The text content, layout, or style is invalid."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The text layout extends beyond the canvas."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "There is not enough memory to render the fill."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "There is not enough memory to render the gradient."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "This Image Editor document version is not supported."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "This document does not need a source image to be relinked."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "This fill region is too complex to process within the memory limit."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "This is not a supported Image Editor document."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "This layer mask is not supported."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "This recovery snapshot is not supported."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "A different linked document is already open in this tab."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "Another Image Editor instance is saving this linked document. Try again after it finishes."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "Linked editing requires a single open document tab."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "Linked mode requires a .cimg document and a .png published output."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The batch exceeds the layer limit."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The linked document changed outside this Image Editor window. Reopen it before saving to avoid overwriting a newer revision."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The linked document could not be fingerprinted."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The linked document was removed outside this Image Editor window."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The linked image directories could not be created."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The linked image paths are incomplete."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The saved linked document could not be verified."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The source, editable document, and published image must use different files."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "Source image unavailable."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The image could not be decoded."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The recovery directory could not be created."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The selection would exceed the supported geometry limit."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The selected region exceeds the supported selection geometry limit."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The active layer has no sampleable pixels at that position."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The Magic Wand tolerance is outside its supported range."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The Magic Wand could not prepare the active pixels."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The Magic Wand could not allocate its temporary region map."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "JPEG export preferences could not be saved."),
    QT_TRANSLATE_NOOP("ImageEditorUI", "The keyboard shortcut preferences could not be saved."),
};

inline QString translateMessage(const QString& source) {
    const QByteArray source_utf8 = source.toUtf8();
    const QString exact = QCoreApplication::translate("ImageEditorUI", source_utf8.constData());
    if (exact != source) return exact;

    const qsizetype separator = source.indexOf(QStringLiteral(": "));
    if (separator > 0) {
        const QString detail = source.mid(separator + 2);
        const QString translated_detail = translateMessage(detail);
        if (translated_detail != detail) return source.left(separator + 2) + translated_detail;
    }

    static const QRegularExpression placeholder(QStringLiteral("%[1-9][0-9]*"));
    for (const char* entry : kDynamicUiMessages) {
        const QString source_template = QString::fromUtf8(entry);
        auto parts = placeholder.globalMatch(source_template);
        QString expression = QStringLiteral("^");
        QStringList placeholders;
        qsizetype offset = 0;
        while (parts.hasNext()) {
            const auto part = parts.next();
            expression += QRegularExpression::escape(
                source_template.mid(offset, part.capturedStart() - offset));
            expression += QStringLiteral("(.+?)");
            placeholders.append(part.captured());
            offset = part.capturedEnd();
        }
        if (placeholders.isEmpty()) continue;
        expression += QRegularExpression::escape(source_template.mid(offset));
        expression += QLatin1Char('$');

        const QRegularExpression pattern(expression,
            QRegularExpression::DotMatchesEverythingOption);
        const auto match = pattern.match(source);
        if (!match.hasMatch()) continue;

        const QString translated_template = QCoreApplication::translate(
            "ImageEditorUI", entry);
        QHash<QString, QString> substitutions;
        for (qsizetype index = 0; index < placeholders.size(); ++index) {
            if (!substitutions.contains(placeholders.at(index))) {
                substitutions.insert(placeholders.at(index), match.captured(index + 1));
            }
        }
        QString result;
        qsizetype result_offset = 0;
        auto translated_parts = placeholder.globalMatch(translated_template);
        while (translated_parts.hasNext()) {
            const auto part = translated_parts.next();
            result += translated_template.mid(result_offset,
                part.capturedStart() - result_offset);
            result += substitutions.value(part.captured(), part.captured());
            result_offset = part.capturedEnd();
        }
        result += translated_template.mid(result_offset);
        return result;
    }
    return source;
}

} // namespace image_editor::ui

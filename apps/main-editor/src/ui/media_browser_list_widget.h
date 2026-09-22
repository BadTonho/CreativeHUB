#pragma once

#include <QListWidget>
#include <QPoint>

class QMimeData;
class QIcon;
class QMouseEvent;
class QPixmap;

namespace media_browser_ui {

inline constexpr int kMediaIndexRole = Qt::UserRole + 1;
inline constexpr int kMediaInfoRole = Qt::UserRole + 2;
inline constexpr int kMediaItemTypeRole = Qt::UserRole + 3;
inline constexpr int kMediaBinPathRole = Qt::UserRole + 4;
inline constexpr int kMediaFullDisplayNameRole = Qt::UserRole + 5;
inline constexpr int kMediaFrameCountRole = Qt::UserRole + 6;
inline constexpr int kMediaFrameRateRole = Qt::UserRole + 7;
inline constexpr int kMediaDurationSecondsRole = Qt::UserRole + 8;

inline constexpr int kMediaItemTypeMedia = 0;
inline constexpr int kMediaItemTypeBin = 1;

inline constexpr int kDragPreviewImageWidth = 128;
inline constexpr int kDragPreviewImageHeight = 72;

[[nodiscard]] QPixmap createDragPreview(
    const QIcon& icon,
    const QString& label);

[[nodiscard]] QMimeData* createMediaBrowserDragMimeData(
    const QList<QListWidgetItem*>& items);

} // namespace media_browser_ui

class MediaBrowserListWidget final : public QListWidget {
public:
    static constexpr int kMinimumIconScalePercent = 50;
    static constexpr int kMaximumIconScalePercent = 150;
    static constexpr int kDefaultIconScalePercent = 100;

    enum class DisplayMode {
        List,
        Grid,
    };

    explicit MediaBrowserListWidget(QWidget* parent = nullptr);

    [[nodiscard]] DisplayMode displayMode() const noexcept;
    void setDisplayMode(DisplayMode mode);
    [[nodiscard]] int iconScalePercent() const noexcept;
    void setIconScalePercent(int percent);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void startDrag(Qt::DropActions supportedActions) override;

    [[nodiscard]] QMimeData* mimeData(
        const QList<QListWidgetItem*>& items) const override;

private:
    void applyDisplayMode();

    DisplayMode display_mode_ = DisplayMode::List;
    int icon_scale_percent_ = kDefaultIconScalePercent;
    QPoint drag_press_position_;
    QListWidgetItem* drag_press_item_ = nullptr;
};

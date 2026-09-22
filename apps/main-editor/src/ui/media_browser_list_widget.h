#pragma once

#include <QListWidget>

class QMimeData;

namespace media_browser_ui {

inline constexpr int kMediaIndexRole = Qt::UserRole + 1;
inline constexpr int kMediaInfoRole = Qt::UserRole + 2;
inline constexpr int kMediaItemTypeRole = Qt::UserRole + 3;
inline constexpr int kMediaBinPathRole = Qt::UserRole + 4;
inline constexpr int kMediaFullDisplayNameRole = Qt::UserRole + 5;

inline constexpr int kMediaItemTypeMedia = 0;
inline constexpr int kMediaItemTypeBin = 1;

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
    [[nodiscard]] QMimeData* mimeData(
        const QList<QListWidgetItem*>& items) const override;

private:
    void applyDisplayMode();

    DisplayMode display_mode_ = DisplayMode::List;
    int icon_scale_percent_ = kDefaultIconScalePercent;
};

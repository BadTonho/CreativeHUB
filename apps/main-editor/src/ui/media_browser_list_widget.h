#pragma once

#include <QListWidget>

class QMimeData;

namespace media_browser_ui {

inline constexpr int kMediaIndexRole = Qt::UserRole + 1;
inline constexpr int kMediaInfoRole = Qt::UserRole + 2;

} // namespace media_browser_ui

class MediaBrowserListWidget final : public QListWidget {
public:
    enum class DisplayMode {
        List,
        Grid,
    };

    explicit MediaBrowserListWidget(QWidget* parent = nullptr);

    [[nodiscard]] DisplayMode displayMode() const noexcept;
    void setDisplayMode(DisplayMode mode);

protected:
    [[nodiscard]] QMimeData* mimeData(
        const QList<QListWidgetItem*>& items) const override;

private:
    void applyDisplayMode();

    DisplayMode display_mode_ = DisplayMode::List;
};

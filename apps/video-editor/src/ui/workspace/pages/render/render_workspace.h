#pragma once

#include <QObject>

#include <functional>

class QWidget;

namespace ui {

// Provides the empty Render page and its read-only Timeline activation hook.
class RenderWorkspace final : public QObject {
public:
    using TimelineReadOnlyHandler = std::function<void(bool)>;

    explicit RenderWorkspace(
        TimelineReadOnlyHandler timeline_read_only_handler,
        QObject* parent = nullptr);

    void createPanels(QWidget* parent);
    void setActive(bool active);

    [[nodiscard]] QWidget* centralPage() const noexcept { return central_page_; }
    [[nodiscard]] bool isActive() const noexcept { return active_; }

private:
    TimelineReadOnlyHandler timeline_read_only_handler_;
    QWidget* central_page_ = nullptr;
    bool active_ = false;
};

}  // namespace ui

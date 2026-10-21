#pragma once

#include <creative_suite/updater/update_service.h>
#include <QObject>

class QMainWindow;
class QToolButton;

namespace creative_suite::updater {

// Application-owned Qt Widgets adapter around the shared updater service.
class UpdateCenter final : public QObject {
    Q_OBJECT
public:
    UpdateCenter(QMainWindow* window, UpdateServiceConfig config, QObject* parent = nullptr);
    [[nodiscard]] UpdateService* service() const noexcept { return m_service; }
    [[nodiscard]] UpdateRuntime* runtime() const noexcept { return m_runtime; }
private:
    void openUpdateDialog();
    QMainWindow* m_window{nullptr};
    UpdateServiceConfig m_config;
    UpdateRuntime* m_runtime{nullptr};
    UpdateService* m_service{nullptr};
    QToolButton* m_status_button{nullptr};
};

} // namespace creative_suite::updater

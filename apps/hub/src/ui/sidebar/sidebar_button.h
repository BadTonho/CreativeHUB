#pragma once

#include <QPushButton>
#include <QLabel>

namespace creative_suite::hub {

class SidebarButton : public QPushButton {
    Q_OBJECT

public:
    enum class Icon { Applications, Projects, Updates, Storage, Backups, Settings };
    explicit SidebarButton(const QString& title, Icon icon, QWidget* parent = nullptr);

    void setActive(bool active);
    [[nodiscard]] bool isActive() const noexcept { return m_active; }

    void setBadgeCount(int count);

private:
    void updateVisuals();

    bool m_active{false};
    int m_badgeCount{0};
    Icon m_icon;
    QLabel* m_badgeLabel{nullptr};
};

} // namespace creative_suite::hub

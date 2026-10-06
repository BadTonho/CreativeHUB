#pragma once

#include "sidebar_button.h"
#include <QWidget>
#include <vector>

namespace creative_suite::hub {

class SidebarWidget : public QWidget {
    Q_OBJECT

public:
    explicit SidebarWidget(QWidget* parent = nullptr);

    void setCurrentIndex(int index);
    [[nodiscard]] int currentIndex() const noexcept { return m_currentIndex; }

    void setUpdatesCount(int count);

signals:
    void pageSelected(int index);

private:
    void selectButton(int index);

    int m_currentIndex{0};
    std::vector<SidebarButton*> m_buttons;
};

} // namespace creative_suite::hub

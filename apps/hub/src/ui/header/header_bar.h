#pragma once

#include "expandable_search_bar.h"

#include <QWidget>
#include <QLabel>

namespace creative_suite::hub {

class ActivityPopup;

class HeaderBar : public QWidget {
    Q_OBJECT

public:
    explicit HeaderBar(QWidget* parent = nullptr);

signals:
    void searchTextChanged(const QString& query);

private slots:
    void onBellClicked();
    void updateBellIcon();

private:
    QLabel* m_brandTitle{nullptr};
    ExpandableSearchBar* m_searchBar{nullptr};
    QPushButton* m_bellButton{nullptr};
    ActivityPopup* m_activityPopup{nullptr};
};

} // namespace creative_suite::hub

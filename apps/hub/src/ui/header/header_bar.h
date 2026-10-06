#pragma once

#include "expandable_search_bar.h"

#include <QWidget>
#include <QLabel>
#include <QPushButton>

namespace creative_suite::hub {

class HeaderBar : public QWidget {
    Q_OBJECT

public:
    explicit HeaderBar(QWidget* parent = nullptr);

signals:
    void searchTextChanged(const QString& query);
    void refreshRequested();

private:
    QLabel* m_brandTitle{nullptr};
    ExpandableSearchBar* m_searchBar{nullptr};
    QPushButton* m_refreshButton{nullptr};
};

} // namespace creative_suite::hub

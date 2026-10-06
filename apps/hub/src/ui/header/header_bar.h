#pragma once

#include "expandable_search_bar.h"

#include <QWidget>
#include <QLabel>

namespace creative_suite::hub {

class HeaderBar : public QWidget {
    Q_OBJECT

public:
    explicit HeaderBar(QWidget* parent = nullptr);

signals:
    void searchTextChanged(const QString& query);

private:
    QLabel* m_brandTitle{nullptr};
    ExpandableSearchBar* m_searchBar{nullptr};
};

} // namespace creative_suite::hub

#pragma once

#include "../../model/app_catalog.h"
#include "../cards/app_card_widget.h"

#include <QWidget>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QPushButton>
#include <vector>

namespace creative_suite::hub {

class AppsPage : public QWidget {
    Q_OBJECT

public:
    explicit AppsPage(AppCatalog* catalog, QWidget* parent = nullptr);

    void setFilterQuery(const QString& query);
    void refreshCards();

signals:
    void openAppRequested(const QString& appId);
    void downloadAppRequested(const QString& appId);
    void cancelDownloadRequested(const QString& appId);

private slots:
    void onFilterTabClicked(int index);

private:
    void setupUi();
    void applyFilters();

    enum class CategoryFilter {
        All,
        Installed,
        Available
    };

    AppCatalog* m_catalog{nullptr};
    CategoryFilter m_activeFilter{CategoryFilter::All};
    QString m_searchQuery;

    QVBoxLayout* m_cardsLayout{nullptr};
    std::vector<AppCardWidget*> m_cardWidgets;
    std::vector<QPushButton*> m_filterButtons;
};

} // namespace creative_suite::hub

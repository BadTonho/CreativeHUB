#pragma once

#include "../../model/app_catalog.h"
#include "../cards/app_card_widget.h"

#include <QWidget>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <vector>

namespace creative_suite::hub {

class UpdatesPage : public QWidget {
    Q_OBJECT

public:
    explicit UpdatesPage(AppCatalog* catalog, QWidget* parent = nullptr);

    void refreshUpdates();

signals:
    void updateAppRequested(const QString& appId);
    void updateAllRequested();

private:
    void setupUi();

    AppCatalog* m_catalog{nullptr};
    QVBoxLayout* m_contentLayout{nullptr};
    QLabel* m_emptyStateLabel{nullptr};
    QPushButton* m_updateAllButton{nullptr};
    std::vector<AppCardWidget*> m_updateCards;
};

} // namespace creative_suite::hub

#pragma once

#include "../../model/activity_manager.h"

#include <QWidget>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QLabel>
#include <QPushButton>

namespace creative_suite::hub {

class ActivityPopup : public QWidget {
    Q_OBJECT

public:
    explicit ActivityPopup(QWidget* parent = nullptr);

    void refreshActivities();
    void showAt(const QPoint& globalPos);

protected:
    void hideEvent(QHideEvent* event) override;

private slots:
    void onMarkAllRead();
    void onClearAll();

private:
    void setupUi();
    QWidget* createActivityCard(const ActivityItem& item);

    QLabel* m_countBadge{nullptr};
    QScrollArea* m_scrollArea{nullptr};
    QVBoxLayout* m_listLayout{nullptr};
    QWidget* m_emptyWidget{nullptr};
};

} // namespace creative_suite::hub

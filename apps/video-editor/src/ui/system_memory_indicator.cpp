#include "system_memory_indicator.h"

#include "system/system_memory_usage.h"
#include "ui/system_memory_details_dialog.h"

#include <QMouseEvent>
#include <QSizePolicy>
#include <Qt>

SystemMemoryIndicator::SystemMemoryIndicator(QWidget* parent)
    : QLabel(parent) {
    setStyleSheet("color: #9aa4b2;");
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    setCursor(Qt::PointingHandCursor);
    setToolTip("Open detailed Main Editor memory usage.");
    setAccessibleName("Main Editor memory usage");

    connect(
        &refresh_timer_,
        &QTimer::timeout,
        this,
        &SystemMemoryIndicator::refresh);
    refresh();
    refresh_timer_.start(1000);
}

void SystemMemoryIndicator::refresh() {
    setText(QString::fromStdString(
        system_monitor::formatProcessMemoryUsage(
            system_monitor::queryMemorySnapshot())));
}

void SystemMemoryIndicator::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        showDetails();
        event->accept();
        return;
    }

    QLabel::mousePressEvent(event);
}

void SystemMemoryIndicator::showDetails() {
    if (details_dialog_ == nullptr) {
        details_dialog_ = new SystemMemoryDetailsDialog(this);
        details_dialog_->setAttribute(Qt::WA_DeleteOnClose);
    }

    details_dialog_->show();
    details_dialog_->raise();
    details_dialog_->activateWindow();
}

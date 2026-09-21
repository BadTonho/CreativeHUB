#include "system_memory_indicator.h"

#include "system/system_memory_usage.h"

#include <QSizePolicy>

SystemMemoryIndicator::SystemMemoryIndicator(QWidget* parent)
    : QLabel(parent) {
    setStyleSheet("color: #9aa4b2;");
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    setToolTip("Total system memory currently in use.");
    setAccessibleName("System memory usage");

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
        system_monitor::formatMemoryUsage(system_monitor::querySystemMemory())));
}

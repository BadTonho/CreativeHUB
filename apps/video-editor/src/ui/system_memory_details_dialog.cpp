#include "system_memory_details_dialog.h"

#include "system/system_memory_usage.h"

#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QSizePolicy>
#include <QVBoxLayout>

namespace {

QLabel* createValueLabel(QWidget* parent) {
    auto* label = new QLabel("N/A", parent);
    label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    return label;
}

}  // namespace

SystemMemoryDetailsDialog::SystemMemoryDetailsDialog(QWidget* parent)
    : QDialog(parent) {
    setWindowTitle("Memory Usage");
    setModal(false);
    setWindowModality(Qt::NonModal);
    resize(420, 250);

    auto* layout = new QVBoxLayout(this);

    auto* system_group = new QGroupBox("System Memory", this);
    auto* system_layout = new QFormLayout(system_group);
    system_total_value_ = createValueLabel(system_group);
    system_used_value_ = createValueLabel(system_group);
    system_available_value_ = createValueLabel(system_group);
    system_layout->addRow("Total", system_total_value_);
    system_layout->addRow("Used", system_used_value_);
    system_layout->addRow("Available", system_available_value_);

    auto* process_group = new QGroupBox("Video Editor", this);
    auto* process_layout = new QFormLayout(process_group);
    process_working_set_value_ = createValueLabel(process_group);
    process_private_usage_value_ = createValueLabel(process_group);
    process_layout->addRow("Working Set", process_working_set_value_);
    process_layout->addRow("Private Usage", process_private_usage_value_);

    layout->addWidget(system_group);
    layout->addWidget(process_group);
    layout->addStretch(1);

    connect(
        &refresh_timer_,
        &QTimer::timeout,
        this,
        &SystemMemoryDetailsDialog::refresh);
    refresh();
    refresh_timer_.start(1000);
}

void SystemMemoryDetailsDialog::refresh() {
    const auto snapshot = system_monitor::queryMemorySnapshot();
    system_total_value_->setText(QString::fromStdString(
        system_monitor::formatGigabytesWithMegabytes(
            snapshot.system_total_bytes)));
    system_used_value_->setText(QString::fromStdString(
        system_monitor::formatGigabytesWithMegabytes(
            system_monitor::usedSystemBytes(snapshot))));
    system_available_value_->setText(QString::fromStdString(
        system_monitor::formatGigabytesWithMegabytes(
            snapshot.system_available_bytes)));
    process_working_set_value_->setText(QString::fromStdString(
        system_monitor::formatMegabytes(snapshot.process_working_set_bytes)));
    process_private_usage_value_->setText(QString::fromStdString(
        system_monitor::formatMegabytes(
            snapshot.process_private_usage_bytes)));
}

#pragma once

#include <QDialog>
#include <QTimer>

class QLabel;
class QWidget;

class SystemMemoryDetailsDialog final : public QDialog {
    Q_OBJECT

public:
    explicit SystemMemoryDetailsDialog(QWidget* parent = nullptr);

private:
    void refresh();

    QLabel* system_total_value_ = nullptr;
    QLabel* system_used_value_ = nullptr;
    QLabel* system_available_value_ = nullptr;
    QLabel* process_working_set_value_ = nullptr;
    QLabel* process_private_usage_value_ = nullptr;
    QTimer refresh_timer_;
};

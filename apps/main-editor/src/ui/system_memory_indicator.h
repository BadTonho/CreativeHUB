#pragma once

#include <QLabel>
#include <QTimer>

class SystemMemoryIndicator final : public QLabel {
public:
    explicit SystemMemoryIndicator(QWidget* parent = nullptr);

private:
    void refresh();

    QTimer refresh_timer_;
};

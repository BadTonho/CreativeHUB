#pragma once

#include <QLabel>
#include <QPointer>
#include <QTimer>

class QMouseEvent;
class SystemMemoryDetailsDialog;

class SystemMemoryIndicator final : public QLabel {
public:
    explicit SystemMemoryIndicator(QWidget* parent = nullptr);

protected:
    void mousePressEvent(QMouseEvent* event) override;

private:
    void refresh();
    void showDetails();

    QPointer<SystemMemoryDetailsDialog> details_dialog_;
    QTimer refresh_timer_;
};

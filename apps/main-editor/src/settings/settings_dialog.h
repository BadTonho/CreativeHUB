#pragma once

#include <QDialog>

class QWidget;

class SettingsDialog final : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget* parent = nullptr);

private:
    [[nodiscard]] QWidget* createGeneralPage();
    [[nodiscard]] QWidget* createTimelinePage();
};

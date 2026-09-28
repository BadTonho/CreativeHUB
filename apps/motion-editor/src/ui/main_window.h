#pragma once

#include <QMainWindow>

namespace motion::ui {

class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(QWidget* parent = nullptr);
};

} // namespace motion::ui

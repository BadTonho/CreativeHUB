#include "main_window.h"

#include <QLabel>

namespace motion::ui {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("Motion Studio"));

    auto* empty_state = new QLabel(QStringLiteral("No composition open"), this);
    empty_state->setObjectName(QStringLiteral("motion-empty-state"));
    empty_state->setAlignment(Qt::AlignCenter);
    setCentralWidget(empty_state);
}

} // namespace motion::ui

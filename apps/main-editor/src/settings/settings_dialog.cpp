#include "settings/settings_dialog.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QTabWidget>
#include <QVBoxLayout>

SettingsDialog::SettingsDialog(QWidget* parent)
    : QDialog(parent) {
    setWindowTitle("Settings");
    setModal(true);
    resize(420, 260);

    auto* layout = new QVBoxLayout(this);
    auto* tabs = new QTabWidget(this);
    tabs->addTab(createGeneralPage(), "General");
    tabs->addTab(createTimelinePage(), "Timeline");

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Close, Qt::Horizontal, this);
    connect(buttons, &QDialogButtonBox::rejected,
            this, &QDialog::reject);

    layout->addWidget(tabs, 1);
    layout->addWidget(buttons);
}

QWidget* SettingsDialog::createGeneralPage() {
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    auto* message = new QLabel(
        "General user preferences will be added here.", page);
    message->setAlignment(Qt::AlignCenter);
    message->setWordWrap(true);
    layout->addWidget(message);
    return page;
}

QWidget* SettingsDialog::createTimelinePage() {
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    auto* message = new QLabel(
        "Timeline preferences will be added here.", page);
    message->setAlignment(Qt::AlignCenter);
    message->setWordWrap(true);
    layout->addWidget(message);
    return page;
}

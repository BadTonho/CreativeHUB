#pragma once

#include <QWidget>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace creative_suite::hub {

class BackupsPage : public QWidget {
    Q_OBJECT

public:
    explicit BackupsPage(QWidget* parent = nullptr);

    void refreshBackupsList();

private slots:
    void onBrowseBackupFolder();
    void onOpenBackupFolder();

private:
    void setupUi();

    QLineEdit* m_pathEdit{nullptr};
    QLabel* m_statsLabel{nullptr};
    QVBoxLayout* m_listLayout{nullptr};
    QWidget* m_emptyWidget{nullptr};
};

} // namespace creative_suite::hub

#pragma once

#include "../../model/recent_projects_manager.h"
#include "../../model/app_catalog.h"
#include "../theme/hub_style.h"

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QScrollArea>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <vector>

namespace creative_suite::hub {

class ProjectsPage : public QWidget {
    Q_OBJECT

public:
    explicit ProjectsPage(RecentProjectsManager* manager, AppCatalog* catalog, QWidget* parent = nullptr);

    void refreshList();

signals:
    void openProjectRequested(const QString& filePath, const QString& appId);
    void newProjectRequested(const QString& appId);

protected:
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void onBrowseAndOpenProject();
    void onNewProjectMenu();
    void onOpenBackupFolder();
    void onFilterTabClicked(int index);
    void onSearchTextChanged(const QString& text);

private:
    void setupUi();
    void onBackupProject(const QString& filePath, QPushButton* triggerBtn);
    QWidget* createProjectCard(const RecentProject& project);

    enum class AppCategoryFilter {
        All,
        Video,
        Image,
        Motion
    };

    RecentProjectsManager* m_manager{nullptr};
    AppCatalog* m_catalog{nullptr};

    AppCategoryFilter m_activeFilter{AppCategoryFilter::All};
    QString m_searchQuery;

    QScrollArea* m_scrollArea{nullptr};
    QVBoxLayout* m_projectsListLayout{nullptr};
    QLabel* m_emptyStateLabel{nullptr};
    QWidget* m_emptyStateWidget{nullptr};
    QLineEdit* m_searchEdit{nullptr};
    std::vector<QPushButton*> m_filterButtons;
};

} // namespace creative_suite::hub

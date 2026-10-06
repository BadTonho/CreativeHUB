#include "projects_page.h"
#include "../theme/hub_palette.h"
#include "../theme/hub_style.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QMenu>
#include <QPainter>
#include <QDateTime>

namespace creative_suite::hub {

namespace {

QPixmap createMiniAppBadge(const QString& appId) {
    constexpr int size = 28;
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);

    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);

    p.setBrush(QColor(0x22, 0x22, 0x22));
    p.setPen(QColor(0x38, 0x38, 0x38));
    p.drawRoundedRect(QRectF(0, 0, size, size), 6, 6);

    QString letter = QStringLiteral("P");
    if (appId == QStringLiteral("video-editor")) {
        letter = QStringLiteral("V");
    } else if (appId == QStringLiteral("image-editor")) {
        letter = QStringLiteral("I");
    } else if (appId == QStringLiteral("motion-editor")) {
        letter = QStringLiteral("M");
    }

    p.setPen(Qt::white);
    QFont font = p.font();
    font.setPointSize(12);
    font.setBold(true);
    p.setFont(font);
    p.drawText(QRect(0, 0, size, size), Qt::AlignCenter, letter);

    return pixmap;
}

QString appDisplayName(const QString& appId) {
    if (appId == QStringLiteral("video-editor")) return QStringLiteral("Video Editor");
    if (appId == QStringLiteral("image-editor")) return QStringLiteral("Image Editor");
    if (appId == QStringLiteral("motion-editor")) return QStringLiteral("Motion Studio");
    return QStringLiteral("Projeto da Suíte");
}

} // namespace

ProjectsPage::ProjectsPage(RecentProjectsManager* manager, AppCatalog* catalog, QWidget* parent)
    : QWidget(parent)
    , m_manager(manager)
    , m_catalog(catalog)
{
    setupUi();

    if (m_manager) {
        connect(m_manager, &RecentProjectsManager::projectsChanged, this, &ProjectsPage::refreshList);
        m_manager->load();
    }

    refreshList();
}

void ProjectsPage::setupUi() {
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setStyleSheet(QStringLiteral("background: transparent;"));

    auto* scrollContainer = new QWidget(m_scrollArea);
    scrollContainer->setStyleSheet(QStringLiteral("background: transparent;"));

    auto* mainLayout = new QVBoxLayout(scrollContainer);
    mainLayout->setContentsMargins(28, 24, 28, 24);
    mainLayout->setSpacing(16);

    // 1. Header Row: Title & Subtitle + Action Buttons
    auto* headerRow = new QHBoxLayout();
    headerRow->setSpacing(12);

    auto* titleCol = new QVBoxLayout();
    titleCol->setSpacing(4);

    auto* titleLabel = new QLabel(QStringLiteral("Projetos Recentes"), scrollContainer);
    titleLabel->setStyleSheet(QStringLiteral("font-size: 22px; font-weight: 800; color: #ffffff; background: transparent;"));
    titleCol->addWidget(titleLabel);

    auto* subLabel = new QLabel(QStringLiteral("Acesse rapidamente seus trabalhos do Video Editor, Image Editor e Motion Studio."), scrollContainer);
    subLabel->setWordWrap(true);
    subLabel->setStyleSheet(QStringLiteral("font-size: 13px; color: #888888; background: transparent;"));
    titleCol->addWidget(subLabel);

    headerRow->addLayout(titleCol, 1);

    auto* openProjectBtn = new QPushButton(QStringLiteral("Abrir Projeto..."), scrollContainer);
    openProjectBtn->setCursor(Qt::PointingHandCursor);
    openProjectBtn->setFixedHeight(36);
    openProjectBtn->setStyleSheet(HubStyle::secondaryButtonStyle());
    connect(openProjectBtn, &QPushButton::clicked, this, &ProjectsPage::onBrowseAndOpenProject);
    headerRow->addWidget(openProjectBtn);

    auto* newProjectBtn = new QPushButton(QStringLiteral("+ Novo Projeto"), scrollContainer);
    newProjectBtn->setCursor(Qt::PointingHandCursor);
    newProjectBtn->setFixedHeight(36);
    newProjectBtn->setStyleSheet(HubStyle::primaryButtonStyle());
    connect(newProjectBtn, &QPushButton::clicked, this, &ProjectsPage::onNewProjectMenu);
    headerRow->addWidget(newProjectBtn);

    mainLayout->addLayout(headerRow);

    // 2. Filter & Search Controls Row
    auto* controlsRow = new QHBoxLayout();
    controlsRow->setSpacing(10);

    const QStringList filterNames = {
        QStringLiteral("Todos"),
        QStringLiteral("Vídeo"),
        QStringLiteral("Imagem"),
        QStringLiteral("Motion")
    };

    for (int i = 0; i < filterNames.size(); ++i) {
        auto* btn = new QPushButton(filterNames[i], scrollContainer);
        btn->setCursor(Qt::PointingHandCursor);
        m_filterButtons.push_back(btn);
        controlsRow->addWidget(btn);

        connect(btn, &QPushButton::clicked, this, [this, i]() {
            onFilterTabClicked(i);
        });
    }

    controlsRow->addStretch();

    m_searchEdit = new QLineEdit(scrollContainer);
    m_searchEdit->setPlaceholderText(QStringLiteral("Filtrar projetos..."));
    m_searchEdit->setFixedWidth(220);
    m_searchEdit->setFixedHeight(32);
    m_searchEdit->setStyleSheet(HubStyle::searchInputStyle());
    connect(m_searchEdit, &QLineEdit::textChanged, this, &ProjectsPage::onSearchTextChanged);
    controlsRow->addWidget(m_searchEdit);

    mainLayout->addLayout(controlsRow);

    // 3. Project Cards Container
    m_projectsListLayout = new QVBoxLayout();
    m_projectsListLayout->setSpacing(10);
    mainLayout->addLayout(m_projectsListLayout);

    // 4. Empty State Widget
    m_emptyStateWidget = new QWidget(scrollContainer);
    auto* emptyLayout = new QVBoxLayout(m_emptyStateWidget);
    emptyLayout->setContentsMargins(24, 48, 24, 48);
    emptyLayout->setSpacing(14);
    emptyLayout->setAlignment(Qt::AlignCenter);

    m_emptyStateLabel = new QLabel(
        QStringLiteral("✦ Nenhum projeto recente encontrado.\nComece criando um novo projeto ou abrindo um arquivo existente da suíte."),
        m_emptyStateWidget
    );
    m_emptyStateLabel->setAlignment(Qt::AlignCenter);
    m_emptyStateLabel->setStyleSheet(QStringLiteral(
        "color: #999999; font-size: 14px; font-weight: 500; line-height: 1.6; background: transparent;"
    ));
    emptyLayout->addWidget(m_emptyStateLabel);

    auto* emptyActionsRow = new QHBoxLayout();
    emptyActionsRow->setSpacing(12);
    emptyActionsRow->addStretch();

    auto* emptyOpenBtn = new QPushButton(QStringLiteral("Abrir Projeto Existente..."), m_emptyStateWidget);
    emptyOpenBtn->setCursor(Qt::PointingHandCursor);
    emptyOpenBtn->setFixedHeight(36);
    emptyOpenBtn->setStyleSheet(HubStyle::secondaryButtonStyle());
    connect(emptyOpenBtn, &QPushButton::clicked, this, &ProjectsPage::onBrowseAndOpenProject);
    emptyActionsRow->addWidget(emptyOpenBtn);

    auto* emptyNewBtn = new QPushButton(QStringLiteral("+ Criar Novo Projeto"), m_emptyStateWidget);
    emptyNewBtn->setCursor(Qt::PointingHandCursor);
    emptyNewBtn->setFixedHeight(36);
    emptyNewBtn->setStyleSheet(HubStyle::primaryButtonStyle());
    connect(emptyNewBtn, &QPushButton::clicked, this, &ProjectsPage::onNewProjectMenu);
    emptyActionsRow->addWidget(emptyNewBtn);

    emptyActionsRow->addStretch();
    emptyLayout->addLayout(emptyActionsRow);

    m_emptyStateWidget->setStyleSheet(QStringLiteral(
        "background-color: #141414; border: 1px dashed #2a2a2a; border-radius: 14px;"
    ));
    mainLayout->addWidget(m_emptyStateWidget);

    mainLayout->addStretch();

    m_scrollArea->setWidget(scrollContainer);
    rootLayout->addWidget(m_scrollArea);

    onFilterTabClicked(0);
}

QWidget* ProjectsPage::createProjectCard(const RecentProject& project) {
    auto* card = new QFrame(this);
    card->setObjectName(QStringLiteral("ProjectCard"));
    card->setStyleSheet(QStringLiteral(
        "#ProjectCard {"
        "   background-color: #161616;"
        "   border: 1px solid #262626;"
        "   border-radius: 10px;"
        "   padding: 10px 14px;"
        "}"
        "#ProjectCard:hover {"
        "   background-color: #1c1c1c;"
        "   border-color: #383838;"
        "}"
    ));

    auto* layout = new QHBoxLayout(card);
    layout->setContentsMargins(12, 10, 12, 10);
    layout->setSpacing(14);

    // App mini badge icon
    auto* iconLabel = new QLabel(card);
    iconLabel->setPixmap(createMiniAppBadge(project.appId));
    iconLabel->setFixedSize(28, 28);
    layout->addWidget(iconLabel);

    // Text column (Name, Path, App)
    auto* textCol = new QVBoxLayout();
    textCol->setSpacing(3);

    auto* nameRow = new QHBoxLayout();
    nameRow->setSpacing(8);

    auto* nameLabel = new QLabel(project.name, card);
    nameLabel->setStyleSheet(QStringLiteral(
        "color: #ffffff; font-size: 13px; font-weight: 700; background: transparent;"
    ));
    nameRow->addWidget(nameLabel);

    auto* appBadge = new QLabel(appDisplayName(project.appId), card);
    appBadge->setStyleSheet(QStringLiteral(
        "background-color: #222222; color: #888888; font-size: 10px; font-weight: 600; "
        "padding: 2px 6px; border-radius: 4px; border: 1px solid #2e2e2e;"
    ));
    nameRow->addWidget(appBadge);
    nameRow->addStretch();

    textCol->addLayout(nameRow);

    auto* pathLabel = new QLabel(project.filePath, card);
    pathLabel->setStyleSheet(QStringLiteral(
        "color: #777777; font-size: 11px; background: transparent;"
    ));
    textCol->addWidget(pathLabel);

    layout->addLayout(textCol, 1);

    // Date
    if (project.lastOpened.isValid()) {
        auto* dateLabel = new QLabel(project.lastOpened.toString(QStringLiteral("dd/MM/yyyy HH:mm")), card);
        dateLabel->setStyleSheet(QStringLiteral("color: #666666; font-size: 11px; background: transparent;"));
        layout->addWidget(dateLabel);
    }

    // "Abrir" button
    auto* openBtn = new QPushButton(QStringLiteral("Abrir"), card);
    openBtn->setCursor(Qt::PointingHandCursor);
    openBtn->setFixedHeight(30);
    openBtn->setStyleSheet(HubStyle::primaryButtonStyle());
    connect(openBtn, &QPushButton::clicked, this, [this, project]() {
        emit openProjectRequested(project.filePath, project.appId);
    });
    layout->addWidget(openBtn);

    // Remove "✕" button
    auto* removeBtn = new QPushButton(QStringLiteral("✕"), card);
    removeBtn->setCursor(Qt::PointingHandCursor);
    removeBtn->setFixedSize(28, 28);
    removeBtn->setToolTip(QStringLiteral("Remover dos recentes"));
    removeBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background: transparent; color: #666666; border: none; font-size: 12px; border-radius: 6px; }"
        "QPushButton:hover { background-color: #262626; color: #ffffff; }"
    ));
    connect(removeBtn, &QPushButton::clicked, this, [this, project]() {
        if (m_manager) {
            m_manager->removeProject(project.filePath);
        }
    });
    layout->addWidget(removeBtn);

    return card;
}

void ProjectsPage::refreshList() {
    // Clear existing cards
    while (auto* item = m_projectsListLayout->takeAt(0)) {
        if (auto* w = item->widget()) {
            delete w;
        }
        delete item;
    }

    if (!m_manager) {
        m_emptyStateWidget->setVisible(true);
        return;
    }

    const auto& allProjects = m_manager->projects();
    int visibleCount = 0;

    for (const auto& p : allProjects) {
        // Filter by search query
        if (!m_searchQuery.isEmpty()) {
            const bool matches = p.name.contains(m_searchQuery, Qt::CaseInsensitive) ||
                                 p.filePath.contains(m_searchQuery, Qt::CaseInsensitive);
            if (!matches) {
                continue;
            }
        }

        // Filter by category
        if (m_activeFilter == AppCategoryFilter::Video && p.appId != QStringLiteral("video-editor")) {
            continue;
        }
        if (m_activeFilter == AppCategoryFilter::Image && p.appId != QStringLiteral("image-editor")) {
            continue;
        }
        if (m_activeFilter == AppCategoryFilter::Motion && p.appId != QStringLiteral("motion-editor")) {
            continue;
        }

        auto* card = createProjectCard(p);
        m_projectsListLayout->addWidget(card);
        ++visibleCount;
    }

    m_emptyStateWidget->setVisible(visibleCount == 0);
}

void ProjectsPage::onFilterTabClicked(int index) {
    m_activeFilter = static_cast<AppCategoryFilter>(index);

    for (size_t i = 0; i < m_filterButtons.size(); ++i) {
        if (static_cast<int>(i) == index) {
            m_filterButtons[i]->setStyleSheet(QStringLiteral(
                "QPushButton {"
                "   background-color: #ffffff;"
                "   color: #000000;"
                "   border-radius: 14px;"
                "   padding: 5px 14px;"
                "   font-size: 12px;"
                "   font-weight: 700;"
                "   border: none;"
                "}"
            ));
        } else {
            m_filterButtons[i]->setStyleSheet(QStringLiteral(
                "QPushButton {"
                "   background-color: #181818;"
                "   color: #888888;"
                "   border-radius: 14px;"
                "   padding: 5px 14px;"
                "   font-size: 12px;"
                "   font-weight: 600;"
                "   border: 1px solid #282828;"
                "}"
                "QPushButton:hover {"
                "   background-color: #222222;"
                "   color: #ffffff;"
                "   border-color: #383838;"
                "}"
            ));
        }
    }

    refreshList();
}

void ProjectsPage::onSearchTextChanged(const QString& text) {
    m_searchQuery = text.trimmed();
    refreshList();
}

void ProjectsPage::onBrowseAndOpenProject() {
    const QString filter = QStringLiteral(
        "Todos os Projetos da Suíte (*.csp *.csve *.cimg *.csie *.motion *.csme);;"
        "Projetos de Vídeo (*.csp *.csve);;"
        "Projetos de Imagem (*.cimg *.csie);;"
        "Projetos de Motion (*.motion *.csme);;"
        "Todos os Arquivos (*.*)"
    );

    const QString selectedFile = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("Abrir Projeto da Creative Suite"),
        QString(),
        filter
    );

    if (selectedFile.isEmpty()) {
        return;
    }

    const QString detectedApp = RecentProjectsManager::detectAppForFile(selectedFile);

    RecentProject proj;
    proj.filePath = selectedFile;
    proj.name = QFileInfo(selectedFile).fileName();
    proj.appId = detectedApp;
    proj.lastOpened = QDateTime::currentDateTime();
    proj.fileSizeBytes = QFileInfo(selectedFile).size();

    if (m_manager) {
        m_manager->addOrUpdateProject(proj);
    }

    emit openProjectRequested(selectedFile, detectedApp);
}

void ProjectsPage::onNewProjectMenu() {
    auto* menu = new QMenu(this);
    menu->setStyleSheet(QStringLiteral(
        "QMenu {"
        "   background-color: #1a1a1a;"
        "   color: #ffffff;"
        "   border: 1px solid #333333;"
        "   border-radius: 8px;"
        "   padding: 6px;"
        "}"
        "QMenu::item {"
        "   padding: 8px 18px;"
        "   border-radius: 4px;"
        "   font-size: 12px;"
        "   font-weight: 600;"
        "}"
        "QMenu::item:selected {"
        "   background-color: #282828;"
        "}"
    ));

    auto* videoAction = menu->addAction(QStringLiteral("🎬 Novo no Video Editor"));
    connect(videoAction, &QAction::triggered, this, [this]() {
        emit newProjectRequested(QStringLiteral("video-editor"));
    });

    auto* imageAction = menu->addAction(QStringLiteral("🎨 Novo no Image Editor"));
    connect(imageAction, &QAction::triggered, this, [this]() {
        emit newProjectRequested(QStringLiteral("image-editor"));
    });

    auto* motionAction = menu->addAction(QStringLiteral("✨ Novo no Motion Studio"));
    connect(motionAction, &QAction::triggered, this, [this]() {
        emit newProjectRequested(QStringLiteral("motion-editor"));
    });

    menu->exec(QCursor::pos());
}

void ProjectsPage::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
}

} // namespace creative_suite::hub

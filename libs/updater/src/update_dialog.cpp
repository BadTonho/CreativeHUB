#include <creative_suite/updater/update_dialog.h>

#include <creative_suite/updater/update_service.h>

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

namespace creative_suite::updater {

UpdateDialog::UpdateDialog(UpdateService* service,
                           UpdateRuntime* runtime,
                           QWidget* parent)
    : QDialog(parent)
    , m_service(service)
    , m_runtime(runtime) {
    setWindowTitle(QStringLiteral("Application Update"));
    setObjectName(QStringLiteral("applicationUpdateDialog"));
    setModal(true);
    setMinimumWidth(520);
    setStyleSheet(QStringLiteral(
        "QDialog { background: #17171c; color: #f3f3f7; }"
        "QLabel#updateEyebrow { color: #9b8cff; font-size: 11px; font-weight: 700; letter-spacing: 1px; }"
        "QLabel#updateTitle { color: #ffffff; font-size: 22px; font-weight: 800; }"
        "QLabel#updateBody { color: #b7b7c2; font-size: 13px; }"
        "QFrame#updateCard { background: #222229; border: 1px solid #34343e; border-radius: 12px; }"
        "QProgressBar { color: #ffffff; background: #303039; border: 0; border-radius: 5px; min-height: 10px; max-height: 10px; text-align: center; }"
        "QProgressBar::chunk { background: #8b78ff; border-radius: 5px; }"
        "QPushButton { border: 1px solid #41414d; border-radius: 8px; padding: 9px 16px; color: #f3f3f7; background: #292932; }"
        "QPushButton#updatePrimaryButton { border: 0; background: #765cff; font-weight: 700; }"
        "QPushButton#updatePrimaryButton:hover { background: #8a73ff; }"
    ));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(24, 22, 24, 20);
    root->setSpacing(14);

    auto* eyebrow = new QLabel(QStringLiteral("CREATIVE SUITE UPDATE"), this);
    eyebrow->setObjectName(QStringLiteral("updateEyebrow"));
    root->addWidget(eyebrow);

    auto* title = new QLabel(QStringLiteral("A better version is ready"), this);
    title->setObjectName(QStringLiteral("updateTitle"));
    root->addWidget(title);

    m_version_label = new QLabel(this);
    m_version_label->setObjectName(QStringLiteral("updateBody"));
    root->addWidget(m_version_label);

    auto* card = new QFrame(this);
    card->setObjectName(QStringLiteral("updateCard"));
    auto* card_layout = new QVBoxLayout(card);
    card_layout->setContentsMargins(16, 14, 16, 14);
    card_layout->setSpacing(10);
    m_notes_label = new QLabel(this);
    m_notes_label->setObjectName(QStringLiteral("updateBody"));
    m_notes_label->setWordWrap(true);
    m_notes_label->setTextFormat(Qt::PlainText);
    m_notes_label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    card_layout->addWidget(m_notes_label);
    m_progress_bar = new QProgressBar(card);
    m_progress_bar->setRange(0, 1000);
    m_progress_bar->setValue(0);
    m_progress_bar->setTextVisible(false);
    m_progress_bar->setVisible(false);
    card_layout->addWidget(m_progress_bar);
    root->addWidget(card);

    m_status_label = new QLabel(this);
    m_status_label->setObjectName(QStringLiteral("updateBody"));
    m_status_label->setWordWrap(true);
    root->addWidget(m_status_label);

    auto* buttons = new QHBoxLayout();
    buttons->addStretch();
    m_cancel_button = new QPushButton(QStringLiteral("Cancel download"), this);
    m_cancel_button->setVisible(false);
    buttons->addWidget(m_cancel_button);
    m_primary_button = new QPushButton(QStringLiteral("Download update"), this);
    m_primary_button->setObjectName(QStringLiteral("updatePrimaryButton"));
    buttons->addWidget(m_primary_button);
    root->addLayout(buttons);

    connect(m_primary_button, &QPushButton::clicked,
            this, &UpdateDialog::beginOrRetry);
    connect(m_cancel_button, &QPushButton::clicked,
            this, &UpdateDialog::cancelDownload);
    if (m_service) {
        connect(m_service, &UpdateService::stateChanged, this, [this] { refresh(); });
        connect(m_service, &UpdateService::updateAvailable, this,
                [this] { refresh(); });
        connect(m_service, &UpdateService::downloadProgress, this,
                [this](qint64 received, qint64 total) {
                    if (total <= 0) return;
                    m_progress_bar->setValue(static_cast<int>(
                        qBound<qint64>(qint64{0}, received * 1000 / total, qint64{1000})));
                });
        connect(m_service, &UpdateService::operationFailed, this,
                [this](const QString& message) {
                    m_status_label->setText(message);
                    refresh();
                });
    }
    if (m_runtime) {
        connect(m_runtime, &UpdateRuntime::installationStateChanged, this,
                [this](const QString& app_id, UpdateState state) {
                    if (m_service && app_id != m_service->config().app_id) return;
                    if (state == UpdateState::WaitingForApplicationToClose) {
                        m_runtime_status = QStringLiteral(
                            "The installer is ready. It will run automatically when the application closes.");
                        m_installation_in_progress = true;
                    } else if (state == UpdateState::InstallerStarted) {
                        m_runtime_status = QStringLiteral(
                            "The installer has started. Follow its progress to finish the update.");
                        m_installation_in_progress = true;
                    }
                    refresh();
                });
        connect(m_runtime, &UpdateRuntime::installationFailed, this,
                [this](const QString& app_id, const QString& message) {
                    if (m_service && app_id != m_service->config().app_id) return;
                    m_runtime_status = message;
                    m_installation_in_progress = false;
                    refresh();
                });
    }
    refresh();
    if (m_service && m_service->state() == UpdateState::Idle) {
        m_service->checkForUpdates();
    }
}

void UpdateDialog::refresh() {
    if (!m_service) {
        m_status_label->setText(QStringLiteral("Update service is unavailable."));
        m_primary_button->setEnabled(false);
        return;
    }
    const auto state = m_service->state();
    const auto& config = m_service->config();
    const auto& release = m_service->release();
    m_version_label->setText(QStringLiteral("%1  ·  Current version %2")
        .arg(config.app_name,
             config.current_version.isEmpty() ? QStringLiteral("Not installed") : config.current_version));

    if (release) {
        m_version_label->setText(QStringLiteral("%1  ·  %2 → %3")
            .arg(config.app_name,
                 config.installed ? config.current_version : QStringLiteral("Not installed"),
                 release->version));
        m_notes_label->setText(release->release_notes.isEmpty()
            ? QStringLiteral("This release includes application updates and fixes.")
            : release->release_notes);
    } else {
        m_notes_label->setText(QStringLiteral("Checking the latest release from GitHub."));
    }

    m_progress_bar->setVisible(state == UpdateState::Downloading);
    m_cancel_button->setVisible(state == UpdateState::Downloading);
    m_primary_button->setEnabled(state != UpdateState::Checking &&
                                 state != UpdateState::Downloading &&
                                 !m_installation_in_progress);
    m_primary_button->setVisible(state != UpdateState::UpToDate &&
                                 state != UpdateState::WaitingForApplicationToClose &&
                                 state != UpdateState::InstallerStarted);

    switch (state) {
    case UpdateState::Idle:
    case UpdateState::Checking:
        m_status_label->setText(QStringLiteral("Checking for a compatible version…"));
        m_primary_button->setText(QStringLiteral("Checking…"));
        break;
    case UpdateState::UpdateAvailable:
        m_status_label->setText(QStringLiteral("Download only starts when you choose it."));
        m_primary_button->setText(config.installed
            ? QStringLiteral("Download update")
            : QStringLiteral("Download and install"));
        break;
    case UpdateState::UpToDate:
        m_status_label->setText(QStringLiteral("This application is up to date."));
        m_primary_button->setVisible(true);
        m_primary_button->setEnabled(true);
        m_primary_button->setText(QStringLiteral("Check again"));
        break;
    case UpdateState::Downloading:
        m_status_label->setText(QStringLiteral("Downloading the full application installer…"));
        m_primary_button->setText(QStringLiteral("Downloading…"));
        break;
    case UpdateState::ReadyToInstall:
        m_status_label->setText(m_runtime_status.isEmpty()
            ? QStringLiteral("The installer is ready. Installation starts automatically after this application closes.")
            : m_runtime_status);
        m_primary_button->setText(m_installation_in_progress
            ? QStringLiteral("Installing…") : QStringLiteral("Retry installation"));
        break;
    case UpdateState::WaitingForApplicationToClose:
        m_status_label->setText(QStringLiteral("Waiting for the application to close before installing."));
        m_primary_button->setText(QStringLiteral("Waiting…"));
        break;
    case UpdateState::InstallerStarted:
        m_status_label->setText(QStringLiteral("The installer has started."));
        m_primary_button->setText(QStringLiteral("Installing…"));
        break;
    case UpdateState::Failed:
        m_primary_button->setVisible(true);
        m_primary_button->setEnabled(true);
        m_primary_button->setText(release ? QStringLiteral("Try again") : QStringLiteral("Check again"));
        if (m_status_label->text().isEmpty()) {
            m_status_label->setText(QStringLiteral("The update could not be completed. The installed application is unchanged."));
        }
        break;
    }
}

void UpdateDialog::beginOrRetry() {
    if (!m_service) return;
    if (m_service->state() == UpdateState::ReadyToInstall) {
        m_runtime_status.clear();
        m_installation_in_progress = false;
        m_service->downloadUpdate();
        return;
    }
    if (m_service->state() == UpdateState::UpToDate ||
        (m_service->state() == UpdateState::Failed && !m_service->release())) {
        m_service->checkForUpdates();
        return;
    }
    m_status_label->clear();
    m_runtime_status.clear();
    m_service->downloadUpdate();
}

void UpdateDialog::cancelDownload() {
    if (m_service) m_service->cancelDownload();
}

} // namespace creative_suite::updater

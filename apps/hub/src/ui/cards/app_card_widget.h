#pragma once

#include "../../model/app_info.h"
#include "app_status_badge.h"
#include "download_progress_bar.h"

#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QPointer>
#include <QPropertyAnimation>
#include <QGraphicsOpacityEffect>

namespace creative_suite::hub {

class AppCardWidget : public QFrame {
    Q_OBJECT
    Q_PROPERTY(int cardHeight READ cardHeight WRITE setCardHeight)

public:
    static constexpr int kCardWidth = 236;
    static constexpr int kCollapsedHeight = 264;
    static constexpr int kExpandedHeight = 475;

    explicit AppCardWidget(QWidget* parent = nullptr);

    void setAppInfo(const AppInfo& app);
    [[nodiscard]] const AppInfo& appInfo() const noexcept { return m_appInfo; }

    void setDownloadingProgress(double percentage, const QString& statusText);

    [[nodiscard]] bool isExpanded() const noexcept { return m_isExpanded; }
    void expand(bool animated = true);
    void collapse(bool animated = true);
    void toggleExpanded(bool animated = true);

    [[nodiscard]] int cardHeight() const noexcept { return height(); }
    void setCardHeight(int h);

signals:
    void expansionToggled(const QString& appId, bool isExpanded);
    void detailsRequested(const QString& appId);
    void openRequested(const QString& appId);
    void downloadRequested(const QString& appId);
    void cancelDownloadRequested(const QString& appId);

protected:
    void mouseReleaseEvent(QMouseEvent* event) override;

private slots:
    void onActionButtonClicked();

private:
    void setupUi();
    void updateVisuals();
    void applyCardStyle(bool expanded);

    AppInfo m_appInfo;
    bool m_isExpanded{false};

    QPushButton* m_closeButton{nullptr};
    AppStatusBadge* m_statusBadge{nullptr};
    QLabel* m_iconLabel{nullptr};
    QLabel* m_titleLabel{nullptr};
    QLabel* m_tagLineLabel{nullptr};

    QWidget* m_detailsWidget{nullptr};
    QLabel* m_descriptionLabel{nullptr};
    QLabel* m_featuresLabel{nullptr};
    QLabel* m_formatBadgeLabel{nullptr};
    QPushButton* m_collapseLinkButton{nullptr};

    DownloadProgressBar* m_progressBar{nullptr};
    QPushButton* m_actionButton{nullptr};

    QGraphicsOpacityEffect* m_detailsOpacityEffect{nullptr};
    QPointer<QPropertyAnimation> m_heightAnimation;
    QPointer<QPropertyAnimation> m_opacityAnimation;
};

} // namespace creative_suite::hub

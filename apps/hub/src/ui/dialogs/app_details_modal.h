#pragma once

#include "../../model/app_info.h"
#include "../../model/changelog_reader.h"
#include <QWidget>
#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QTextBrowser>
#include <QPointer>
#include <QPropertyAnimation>
#include <QVBoxLayout>

namespace creative_suite::hub {

class AppDetailsModal : public QWidget {
    Q_OBJECT
    Q_PROPERTY(QRect cardGeometry READ cardGeometry WRITE setCardGeometry)
    Q_PROPERTY(double backdropOpacity READ backdropOpacity WRITE setBackdropOpacity)

public:
    explicit AppDetailsModal(QWidget* parent = nullptr);

    void showApp(const AppInfo& app, const QRect& originRect = QRect());
    void closeWithAnimation();

    [[nodiscard]] QRect cardGeometry() const noexcept;
    void setCardGeometry(const QRect& rect);

    [[nodiscard]] double backdropOpacity() const noexcept { return m_backdropOpacity; }
    void setBackdropOpacity(double opacity);

    void setChangelogBasePath(const QString& path);
    void loadChangelogForVersion(const QString& version);

    [[nodiscard]] QTextBrowser* changelogBrowser() const noexcept { return m_changelogBrowser; }
    [[nodiscard]] QComboBox* versionCombo() const noexcept { return m_versionCombo; }
    [[nodiscard]] QLabel* emptyChangelogLabel() const noexcept { return m_emptyChangelogLabel; }

signals:
    void actionRequested(const QString& appId);
    void closed();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void setupUi();
    void updateVisuals();
    void clearFeatures();
    QRect targetCardRect() const;
    static QPixmap getAppIcon(const AppInfo& app);

    AppInfo m_app;
    QRect m_originRect;
    double m_backdropOpacity{0.0};
    bool m_isClosing{false};
    ChangelogReader m_changelogReader;

    QFrame* m_cardFrame{nullptr};
    QLabel* m_iconLabel{nullptr};
    QLabel* m_nameLabel{nullptr};
    QLabel* m_taglineLabel{nullptr};
    QLabel* m_descText{nullptr};
    QVBoxLayout* m_featuresLayout{nullptr};
    QLabel* m_formatLabel{nullptr};
    QLabel* m_exeLabel{nullptr};
    QPushButton* m_actionButton{nullptr};
    QPushButton* m_closeIconButton{nullptr};
    QPushButton* m_closeBottomButton{nullptr};

    // Changelog section widgets
    QLabel* m_versionBadge{nullptr};
    QComboBox* m_versionCombo{nullptr};
    QFrame* m_changelogCard{nullptr};
    QTextBrowser* m_changelogBrowser{nullptr};
    QLabel* m_emptyChangelogLabel{nullptr};

    QPointer<QPropertyAnimation> m_geometryAnim;
    QPointer<QPropertyAnimation> m_opacityAnim;
};

} // namespace creative_suite::hub

#pragma once

#include <QWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QPropertyAnimation>

namespace creative_suite::hub {

class ExpandableSearchBar : public QWidget {
    Q_OBJECT
    Q_PROPERTY(int animatedWidth READ animatedWidth WRITE setAnimatedWidth)

public:
    explicit ExpandableSearchBar(QWidget* parent = nullptr);

    [[nodiscard]] int animatedWidth() const noexcept { return width(); }
    void setAnimatedWidth(int w);

    [[nodiscard]] QString text() const;

signals:
    void searchTextChanged(const QString& query);

public slots:
    void expand();
    void collapse();

protected:
    void focusOutEvent(QFocusEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void setupUi();
    static QIcon createSearchIcon(const QColor& color);
    static QIcon createCloseIcon(const QColor& color);

    QPushButton* m_searchButton{nullptr};
    QLineEdit* m_lineEdit{nullptr};
    QPushButton* m_closeButton{nullptr};
    QPropertyAnimation* m_animation{nullptr};

    bool m_expanded{false};
    static constexpr int kCollapsedWidth = 36;
    static constexpr int kExpandedWidth = 260;
};

} // namespace creative_suite::hub

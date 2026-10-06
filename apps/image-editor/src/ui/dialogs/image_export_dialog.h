#pragma once

#include "import_export/image_exporter.h"

#include <QDialog>

class QLabel;
class QCloseEvent;
class QProgressBar;
class QPushButton;
class QSlider;
class QSpinBox;

namespace image_editor {

class JpegExportOptionsDialog final : public QDialog {
    Q_OBJECT

public:
    explicit JpegExportOptionsDialog(const ImageExportOptions& options,
                                     QWidget* parent = nullptr);

    [[nodiscard]] ImageExportOptions options() const;
    void setBackgroundColor(const QColor& color);

private:
    void chooseBackgroundColor();
    void updateBackgroundButton();

    QSlider* quality_slider_ = nullptr;
    QSpinBox* quality_spin_ = nullptr;
    QPushButton* background_button_ = nullptr;
    QColor background_color_ = Qt::white;
};

class ImageExportProgressDialog final : public QDialog {
    Q_OBJECT

public:
    explicit ImageExportProgressDialog(QWidget* parent = nullptr);

    void setPhaseText(const QString& text);
    void setCancellationText(const QString& text) { cancellation_text_ = text; }
    void setCancellationPending();
    void finish();

signals:
    void cancelRequested();

protected:
    void reject() override;
    void closeEvent(QCloseEvent* event) override;

private:
    void requestCancellation();

    QLabel* label_ = nullptr;
    QProgressBar* progress_bar_ = nullptr;
    QPushButton* cancel_button_ = nullptr;
    QString phase_text_;
    QString cancellation_text_;
    bool cancellation_pending_ = false;
    bool finished_ = false;
};

} // namespace image_editor

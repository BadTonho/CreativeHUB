#pragma once

#include "model/composition_document.h"

#include <creative_suite/animation/animation.h>

#include <QDialog>
#include <QString>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;

namespace motion::ui {

struct AudioKeyframeDialogSettings final {
    QString audio_path;
    creative_suite::animation::TransformProperty property =
        creative_suite::animation::TransformProperty::Scale;
    double minimum_value = 1.0;
    double maximum_value = 2.0;
};

class AudioKeyframeDialog final : public QDialog {
public:
    AudioKeyframeDialog(const QString& layer_name, QWidget* parent = nullptr);

    [[nodiscard]] AudioKeyframeDialogSettings settings() const;

protected:
    void accept() override;

private:
    void browseForAudio();
    void setPropertyDefaults(int property_index);
    void validateInputs();

    QLineEdit* audio_path_ = nullptr;
    QComboBox* property_ = nullptr;
    QDoubleSpinBox* minimum_value_ = nullptr;
    QDoubleSpinBox* maximum_value_ = nullptr;
    QLabel* validation_message_ = nullptr;
};

} // namespace motion::ui

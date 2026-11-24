#pragma once

#include <QCheckBox>
#include <QCoreApplication>
#include <QDialog>
#include <QFormLayout>
#include <QLabel>
#include <QSlider>
#include <QVBoxLayout>

#include <functional>

namespace audio_effects::monitor {

class MonitorDialog final : public QDialog {
public:
    explicit MonitorDialog(bool dim_active, double ref_level, QWidget* parent = nullptr)
        : QDialog(parent) {
        setObjectName("audioMonitorDialog");
        setWindowTitle(QCoreApplication::translate("AudioWorkspace", "Control Room Monitor"));
        setMinimumWidth(280);
        setStyleSheet(QStringLiteral(
            "QDialog { background: #16181c; color: #cfd6e0; }"
            "QLabel, QCheckBox { color: #cfd6e0; font-size: 11px; }"));
        auto* layout = new QVBoxLayout(this);
        auto* form = new QFormLayout;

        dim_check_ = new QCheckBox(QCoreApplication::translate("AudioWorkspace", "DIM (-20 dB)"), this);
        dim_check_->setChecked(dim_active);
        form->addRow(dim_check_);

        mono_check_ = new QCheckBox(QCoreApplication::translate("AudioWorkspace", "Mono Sum"), this);
        form->addRow(mono_check_);

        ref_slider_ = new QSlider(Qt::Horizontal, this);
        ref_slider_->setRange(0, 100);
        ref_slider_->setValue(static_cast<int>(ref_level * 100.0));
        auto* ref_label = new QLabel(QString::number(ref_slider_->value()) + QStringLiteral("%"), this);
        form->addRow(QCoreApplication::translate("AudioWorkspace", "Reference Level"), ref_slider_);
        form->addRow(QString(), ref_label);
        layout->addLayout(form);

        connect(dim_check_, &QCheckBox::toggled, this, [this](bool v) {
            if (on_dim_changed) on_dim_changed(v);
        });
        connect(ref_slider_, &QSlider::valueChanged, this, [this, ref_label](int v) {
            ref_label->setText(QString::number(v) + QStringLiteral("%"));
            if (on_ref_changed) on_ref_changed(v / 100.0);
        });
    }

    void setDimChecked(bool dim) {
        if (dim_check_) dim_check_->setChecked(dim);
    }

    std::function<void(bool)> on_dim_changed;
    std::function<void(double)> on_ref_changed;

private:
    QCheckBox* dim_check_ = nullptr;
    QCheckBox* mono_check_ = nullptr;
    QSlider* ref_slider_ = nullptr;
};

}  // namespace audio_effects::monitor

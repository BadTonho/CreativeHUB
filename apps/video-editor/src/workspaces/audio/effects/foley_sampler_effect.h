#pragma once

#include <QAudioOutput>
#include <QCoreApplication>
#include <QDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QLabel>
#include <QMediaPlayer>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

#include <array>

namespace audio_effects::foley_sampler {

class FoleySamplerDialog final : public QDialog {
public:
    explicit FoleySamplerDialog(QWidget* parent = nullptr) : QDialog(parent) {
        setObjectName("audioFoleySamplerDialog");
        setWindowTitle(QCoreApplication::translate("AudioWorkspace", "Foley Sampler"));
        setMinimumSize(380, 220);
        setStyleSheet(QStringLiteral(
            "QDialog { background: #16181c; color: #cfd6e0; }"
            "QPushButton { background: #232730; color: #cfd6e0; border: 1px solid #323743; border-radius: 4px; padding: 8px; font-weight: 600; }"
            "QPushButton:hover { background: #2d3340; border-color: #4f88c2; }"
            "QLabel { color: #8f98a8; font-size: 10px; }"));

        player_ = new QMediaPlayer(this);
        audio_output_ = new QAudioOutput(this);
        audio_output_->setVolume(0.85F);
        player_->setAudioOutput(audio_output_);

        auto* outer = new QVBoxLayout(this);
        auto* hint = new QLabel(
            QCoreApplication::translate("AudioWorkspace", "Click a pad to load or audition a local sound file (.wav, .mp3, .ogg)."),
            this);
        outer->addWidget(hint);

        auto* grid = new QGridLayout;
        static constexpr const char* kDefaultNames[8] = {
            "Pad 1 (Load...)", "Pad 2 (Load...)", "Pad 3 (Load...)", "Pad 4 (Load...)",
            "Pad 5 (Load...)", "Pad 6 (Load...)", "Pad 7 (Load...)", "Pad 8 (Load...)"
        };
        for (int i = 0; i < 8; ++i) {
            auto* pad = new QPushButton(QString::fromLatin1(kDefaultNames[i]), this);
            pad->setMinimumHeight(44);
            grid->addWidget(pad, i / 4, i % 4);
            connect(pad, &QPushButton::clicked, this, [this, i, pad] {
                auto& path = pad_paths_[static_cast<std::size_t>(i)];
                if (path.isEmpty()) {
                    const auto chosen = QFileDialog::getOpenFileName(
                        this,
                        QCoreApplication::translate("AudioWorkspace", "Select Foley Sound"),
                        QString(),
                        QCoreApplication::translate("AudioWorkspace", "Audio files (*.wav *.mp3 *.flac *.ogg *.m4a);;All files (*)"));
                    if (chosen.isEmpty()) return;
                    path = chosen;
                    pad->setText(QFileInfo(chosen).baseName());
                }
                player_->setSource(QUrl::fromLocalFile(path));
                player_->play();
            });
        }
        outer->addLayout(grid, 1);
    }

private:
    QMediaPlayer* player_ = nullptr;
    QAudioOutput* audio_output_ = nullptr;
    std::array<QString, 8> pad_paths_;
};

}  // namespace audio_effects::foley_sampler

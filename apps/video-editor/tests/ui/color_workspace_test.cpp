#include "workspaces/color/ui/color_workspace.h"
#include "ui/preview/preview_widget.h"

#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QLabel>
#include <QListWidget>
#include <QMetaObject>
#include <QPushButton>
#include <QThread>

#include <creative_suite/effects/effects.h>

#include <cstdio>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    try {
        ui::ColorWorkspace workspace;
        std::vector<creative_suite::effects::EffectInstance> effects;
        int edits = 0;
        workspace.setEffectsChangedHandler([&](timeline::ClipId id,
            std::vector<creative_suite::effects::EffectInstance> updated) {
            require(id == 7, "Color must edit the selected clip identity.");
            effects = std::move(updated);
            ++edits;
            workspace.setClips({{7, QStringLiteral("Shot A")}}, 7, effects);
        });
        workspace.setClips({{7, QStringLiteral("Shot A")}}, 7, effects);
        auto* add = workspace.findChild<QPushButton*>("colorAddNodeButton");
        auto* up = workspace.findChild<QPushButton*>("colorMoveNodeUpButton");
        auto* toggle = workspace.findChild<QPushButton*>("colorToggleNodeButton");
        auto* remove = workspace.findChild<QPushButton*>("colorRemoveNodeButton");
        auto* nodes = workspace.findChild<QListWidget*>("colorNodesList");
        require(add && up && toggle && remove && nodes, "Color node controls must exist.");
        add->click();
        require(effects.size() == 1 && effects[0].id == "video.primary_grade" &&
                nodes->count() == 3 && edits == 1,
                "Adding a node must use the clip effect stack.");
        nodes->setCurrentRow(1);
        auto* red = workspace.findChild<QDoubleSpinBox*>("colorParameter_red_gain");
        require(red != nullptr, "Primary Grade must expose red gain.");
        red->setValue(145);
        require(QMetaObject::invokeMethod(red, "editingFinished"),
                "Red gain edit must be committed.");
        require(creative_suite::effects::parameterValue(effects[0], "red_gain") == 145,
                "Primary Grade parameters must share the clip effect stack.");
        require(red->isHidden(),
                "Rebuilt controls must hide the old spin box before deferred deletion.");
        add->click();
        require(effects.size() == 2, "A second serial node must be added.");
        nodes->setCurrentRow(2);
        up->click();
        require(effects.size() == 2 &&
                creative_suite::effects::parameterValue(effects[0], "red_gain") == 100 &&
                creative_suite::effects::parameterValue(effects[1], "red_gain") == 145,
                "Moving nodes must change processing order.");
        nodes->setCurrentRow(1);
        toggle->click();
        require(!effects[0].enabled, "Node enable state must be editable.");
        nodes->setCurrentRow(1);
        remove->click();
        require(effects.size() == 1 && effects[0].enabled,
                "Removing a node must retain the other effect.");
        auto* new_type = workspace.findChild<QComboBox*>("colorNewNodeType");
        require(new_type != nullptr, "Color must offer the available serial color effects.");
        new_type->setCurrentIndex(new_type->findData(QStringLiteral("video.saturation")));
        add->click();
        require(effects.size() == 2 && effects.back().id == "video.saturation",
                "Color must add the chosen effect to the same ordered clip stack.");
        QImage thumbnail(4, 3, QImage::Format_RGBA8888);
        thumbnail.fill(Qt::red);
        workspace.setClips({{7, QStringLiteral("Shot A"), thumbnail}}, 7, effects);
        auto* clip_strip = workspace.findChild<QListWidget*>("colorClipStrip");
        require(clip_strip && !clip_strip->item(0)->icon().isNull(),
                "Color clip strip must display available media thumbnails.");
        auto* viewer = new PreviewWidget(&workspace);
        auto frame = std::make_shared<media::VideoFrame>();
        frame->width = 2; frame->height = 2; frame->stride = 8;
        frame->rgba_pixels = {255, 0, 0, 255, 0, 255, 0, 255,
                              0, 0, 255, 255, 255, 255, 255, 255};
        viewer->setFrame(frame);
        workspace.setPreviewWidget(viewer);
        workspace.setActive(true);
        auto* waveform = workspace.findChild<QLabel*>("colorWaveform");
        require(waveform != nullptr, "Color waveform display must exist.");
        QElapsedTimer timeout;
        timeout.start();
        while (waveform->pixmap().isNull() && timeout.elapsed() < 2000) {
            application.processEvents();
            QThread::msleep(10);
        }
        require(!waveform->pixmap().isNull() && waveform->pixmap().size() == QSize(320, 256),
                "Waveform must process the displayed frame asynchronously at bounded resolution.");
        workspace.setActive(false);
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}

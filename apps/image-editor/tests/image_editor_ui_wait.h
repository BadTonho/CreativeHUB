#pragma once
#include "../src/ui/rendering/image_render_controller.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QTest>

// UI assertions must observe the latest requested frame, not a previous frame
// left on screen while the worker runs. Bounded waiting also exposes deadlocks.
inline void processImageEditorEvents() {
    QCoreApplication::processEvents();
    QElapsedTimer timer; timer.start();
    for (;;) {
        bool busy=false;
        for(auto* window:QApplication::topLevelWidgets())
            for(auto* renderer:window->findChildren<image_editor::ImageRenderController*>())
                busy |= renderer->busy() || renderer->hasPendingRequest();
        if(!busy || timer.elapsed()>10000) break;
        QTest::qWait(1);
    }
    QCoreApplication::processEvents();
}

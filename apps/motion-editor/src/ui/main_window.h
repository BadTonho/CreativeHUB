#pragma once

#include "model/composition_document.h"

#include <QMainWindow>

#include <array>
#include <filesystem>
#include <memory>
#include <optional>

class QAction;
class QLabel;
class QPushButton;
class QSplitter;
class QDoubleSpinBox;
class QTabWidget;

namespace motion::ui {

class CompositionViewer;
class MediaDetailsWidget;
class MediaPoolWidget;
class PreviewRenderer;
class TimelineNavigator;

class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    [[nodiscard]] const model::CompositionDocument* compositionDocument() const noexcept;
    [[nodiscard]] MediaPoolWidget* mediaPoolWidget() const noexcept;

private:
    void createNewComposition();
    void createWorkspace();
    void openMedia();
    void updateMediaDetails();
    void refreshTimeline();
    void selectLayer(model::LayerId id);
    void syncTransformInspector();
    void editSelectedLayerTransform();
    void handleMediaDrop(const std::filesystem::path& path,
                         std::int64_t start_frame,
                         model::LayerId before_layer_id);
    void requestPreview(bool playback_tick = false);

    std::optional<model::CompositionDocument> document_;
    QLabel* empty_state_ = nullptr;
    QPushButton* empty_state_new_composition_button_ = nullptr;
    QAction* import_media_action_ = nullptr;
    QSplitter* composition_splitter_ = nullptr;
    QSplitter* workspace_ = nullptr;
    MediaPoolWidget* media_pool_ = nullptr;
    CompositionViewer* viewer_ = nullptr;
    MediaDetailsWidget* media_details_ = nullptr;
    QTabWidget* inspector_tabs_ = nullptr;
    QWidget* transform_inspector_ = nullptr;
    std::array<QDoubleSpinBox*, 5> transform_fields_{};
    TimelineNavigator* timeline_ = nullptr;
    std::unique_ptr<PreviewRenderer> preview_renderer_;
    model::LayerId selected_layer_id_ = 0;
};

} // namespace motion::ui

#pragma once

#include "model/composition_document.h"

#include <QMainWindow>

#include <optional>

class QAction;
class QLabel;
class QPushButton;
class QSplitter;

namespace motion::ui {

class CompositionViewer;
class MediaDetailsWidget;
class MediaPoolWidget;
class TimelineNavigator;

class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(QWidget* parent = nullptr);

    [[nodiscard]] const model::CompositionDocument* compositionDocument() const noexcept;
    [[nodiscard]] MediaPoolWidget* mediaPoolWidget() const noexcept;

private:
    void createNewComposition();
    void createWorkspace();
    void openMedia();
    void updateMediaDetails();

    std::optional<model::CompositionDocument> document_;
    QLabel* empty_state_ = nullptr;
    QPushButton* empty_state_new_composition_button_ = nullptr;
    QAction* import_media_action_ = nullptr;
    QSplitter* composition_splitter_ = nullptr;
    QSplitter* workspace_ = nullptr;
    MediaPoolWidget* media_pool_ = nullptr;
    CompositionViewer* viewer_ = nullptr;
    MediaDetailsWidget* media_details_ = nullptr;
    TimelineNavigator* timeline_ = nullptr;
};

} // namespace motion::ui

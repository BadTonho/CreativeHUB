#pragma once

#include "editor_session.h"

#include <project/project_document.h>

namespace application {

struct TimelinePresentationState {
    double zoom = 1.0;
    double row_height = timeline::kDefaultTrackRowHeight;
};

class ProjectDocumentMapper final {
public:
    [[nodiscard]] static project::ProjectDocument toDocument(
        const EditorSession& session,
        TimelinePresentationState presentation = {});
};

} // namespace application

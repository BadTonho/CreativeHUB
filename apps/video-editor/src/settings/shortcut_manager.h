#pragma once

#include "ui/workspace/workspace_page_id.h"

#include <QKeySequence>
#include <QString>

#include <memory>
#include <vector>

class QAction;

namespace settings {

enum class ShortcutScope {
    Application,
    Shared,
    Edit,
    Fusion,
    Render
};

struct ShortcutEntry {
    QString id;
    QString label;
    QAction* action = nullptr;
    QKeySequence default_sequence;
    ShortcutScope scope = ShortcutScope::Application;
    QString availability;
    QKeySequence configured_sequence;
};

// Video Editor shortcut routing. Configured sequences remain independent of
// the sequence temporarily installed on each QAction for the active workspace.
class ShortcutManager final {
public:
    ShortcutManager();
    ~ShortcutManager();

    ShortcutManager(const ShortcutManager&) = delete;
    ShortcutManager& operator=(const ShortcutManager&) = delete;

    void registerAction(
        const QString& id,
        const QString& label,
        QAction* action,
        ShortcutScope scope,
        const QString& availability);
    bool load(QString* error_message = nullptr);
    void setWorkspace(ui::WorkspacePageId page);
    [[nodiscard]] bool isScopeActive(ShortcutScope scope) const noexcept;
    [[nodiscard]] bool setShortcut(
        const QString& id,
        const QKeySequence& sequence,
        QString* conflict_message = nullptr);
    [[nodiscard]] bool resetShortcut(
        const QString& id,
        QString* conflict_message = nullptr);
    void resetAll();

    [[nodiscard]] const std::vector<ShortcutEntry>& entries() const noexcept;
    [[nodiscard]] QKeySequence shortcut(const QString& id) const;
    [[nodiscard]] static QString scopeLabel(ShortcutScope scope);

private:
    struct ScopeBackend;

    [[nodiscard]] ShortcutEntry* findEntry(const QString& id) noexcept;
    [[nodiscard]] const ShortcutEntry* findEntry(const QString& id) const noexcept;
    [[nodiscard]] ScopeBackend* backendFor(ShortcutScope scope) noexcept;
    [[nodiscard]] bool scopesOverlap(
        ShortcutScope first, ShortcutScope second) const noexcept;
    [[nodiscard]] bool isActive(ShortcutScope scope) const noexcept;
    [[nodiscard]] const ShortcutEntry* conflictingEntry(
        const QString& id,
        ShortcutScope scope,
        const QKeySequence& sequence) const noexcept;
    void synchronizeBackendActions(ShortcutScope scope);
    void applyActiveBindings();

    std::vector<ShortcutEntry> entries_;
    std::vector<std::unique_ptr<ScopeBackend>> backends_;
    ui::WorkspacePageId active_page_ = ui::WorkspacePageId::Edit;
};

} // namespace settings

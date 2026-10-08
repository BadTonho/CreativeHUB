#include "settings/shortcut_manager.h"

#include <creative_suite/shortcuts/shortcut_manager.h>

#include <QAction>

#include <algorithm>
#include <memory>
#include <utility>

namespace settings {

struct ShortcutManager::ScopeBackend {
    explicit ScopeBackend(ShortcutScope value)
        : scope(value), manager(std::make_unique<
              creative_suite::shortcuts::ShortcutManager>(
                  QStringLiteral("shortcuts"))) {}

    ShortcutScope scope;
    std::unique_ptr<creative_suite::shortcuts::ShortcutManager> manager;
};

ShortcutManager::ShortcutManager() = default;
ShortcutManager::~ShortcutManager() = default;

void ShortcutManager::registerAction(
    const QString& id,
    const QString& label,
    QAction* action,
    ShortcutScope scope,
    const QString& availability) {
    if (action == nullptr || findEntry(id) != nullptr) return;

    auto* backend = backendFor(scope);
    if (backend == nullptr) {
        auto created = std::make_unique<ScopeBackend>(scope);
        backend = created.get();
        backends_.push_back(std::move(created));
    }
    backend->manager->registerAction(id, label, action);
    entries_.push_back({
        id, label, action, action->shortcut(), scope, availability,
        action->shortcut()});
}

bool ShortcutManager::load(QString* error_message) {
    bool succeeded = true;
    QString first_error;
    for (const auto& backend : backends_) {
        QString backend_error;
        if (!backend->manager->load(&backend_error)) {
            succeeded = false;
            if (first_error.isEmpty()) first_error = backend_error;
        }
    }

    for (auto& entry : entries_) {
        entry.configured_sequence = entry.action != nullptr
            ? entry.action->shortcut() : QKeySequence{};
    }

    // Resolve an invalid stored overlap deterministically. Existing global
    // shortcut preferences were unique before scoped bindings were introduced;
    // this also protects users if a future version writes conflicting values.
    std::vector<std::pair<ShortcutScope, QKeySequence>> accepted;
    accepted.reserve(entries_.size());
    for (auto& entry : entries_) {
        auto conflicts_with_accepted = [this, &entry, &accepted](
                                          const QKeySequence& sequence) {
            if (sequence.isEmpty()) return false;
            return std::any_of(
                accepted.cbegin(), accepted.cend(),
                [this, &entry, &sequence](const auto& previous) {
                    return previous.second == sequence &&
                        scopesOverlap(entry.scope, previous.first);
                });
        };

        if (conflicts_with_accepted(entry.configured_sequence)) {
            auto repaired = entry.default_sequence;
            if (conflicts_with_accepted(repaired)) repaired = {};
            synchronizeBackendActions(entry.scope);
            auto* backend = backendFor(entry.scope);
            if (backend != nullptr) {
                QString ignored_error;
                if (backend->manager->setShortcut(
                        entry.id, repaired, &ignored_error)) {
                    entry.configured_sequence = repaired;
                } else {
                    entry.configured_sequence = {};
                    static_cast<void>(backend->manager->setShortcut(
                        entry.id, {}, &ignored_error));
                }
            } else {
                entry.configured_sequence = repaired;
            }
        }
        accepted.emplace_back(entry.scope, entry.configured_sequence);
    }

    applyActiveBindings();
    if (error_message != nullptr) *error_message = first_error;
    return succeeded;
}

void ShortcutManager::setWorkspace(ui::WorkspacePageId page) {
    active_page_ = page;
    applyActiveBindings();
}

bool ShortcutManager::setShortcut(
    const QString& id,
    const QKeySequence& sequence,
    QString* conflict_message) {
    auto* entry = findEntry(id);
    if (entry == nullptr || entry->action == nullptr) return false;

    if (const auto* conflict = conflictingEntry(id, entry->scope, sequence)) {
        if (conflict_message != nullptr) {
            *conflict_message = QStringLiteral(
                "The shortcut is already assigned to \"%1\" (%2).")
                .arg(conflict->label, conflict->availability);
        }
        return false;
    }

    synchronizeBackendActions(entry->scope);
    auto* backend = backendFor(entry->scope);
    if (backend == nullptr || !backend->manager->setShortcut(
            id, sequence, conflict_message)) {
        return false;
    }
    entry->configured_sequence = sequence;
    applyActiveBindings();
    if (conflict_message != nullptr) conflict_message->clear();
    return true;
}

bool ShortcutManager::resetShortcut(
    const QString& id,
    QString* conflict_message) {
    auto* entry = findEntry(id);
    if (entry == nullptr || entry->action == nullptr) return false;

    if (const auto* conflict = conflictingEntry(
            id, entry->scope, entry->default_sequence)) {
        if (conflict_message != nullptr) {
            *conflict_message = QStringLiteral(
                "The default shortcut conflicts with \"%1\" (%2).")
                .arg(conflict->label, conflict->availability);
        }
        return false;
    }

    synchronizeBackendActions(entry->scope);
    auto* backend = backendFor(entry->scope);
    if (backend == nullptr || !backend->manager->resetShortcut(
            id, conflict_message)) {
        return false;
    }
    entry->configured_sequence = entry->default_sequence;
    applyActiveBindings();
    if (conflict_message != nullptr) conflict_message->clear();
    return true;
}

void ShortcutManager::resetAll() {
    for (const auto& backend : backends_) {
        synchronizeBackendActions(backend->scope);
        backend->manager->resetAll();
    }
    for (auto& entry : entries_) {
        entry.configured_sequence = entry.default_sequence;
    }
    applyActiveBindings();
}

const std::vector<ShortcutEntry>& ShortcutManager::entries() const noexcept {
    return entries_;
}

QKeySequence ShortcutManager::shortcut(const QString& id) const {
    const auto* entry = findEntry(id);
    return entry == nullptr ? QKeySequence{} : entry->configured_sequence;
}

QString ShortcutManager::scopeLabel(ShortcutScope scope) {
    switch (scope) {
    case ShortcutScope::Application: return QStringLiteral("Application");
    case ShortcutScope::Shared: return QStringLiteral("Shared");
    case ShortcutScope::Edit: return QStringLiteral("Edit");
    case ShortcutScope::Fusion: return QStringLiteral("Fusion");
    case ShortcutScope::Render: return QStringLiteral("Render");
    }
    return QStringLiteral("Other");
}

ShortcutEntry* ShortcutManager::findEntry(const QString& id) noexcept {
    const auto iterator = std::find_if(
        entries_.begin(), entries_.end(), [&id](const ShortcutEntry& entry) {
            return entry.id == id;
        });
    return iterator == entries_.end() ? nullptr : &*iterator;
}

const ShortcutEntry* ShortcutManager::findEntry(const QString& id) const noexcept {
    const auto iterator = std::find_if(
        entries_.cbegin(), entries_.cend(), [&id](const ShortcutEntry& entry) {
            return entry.id == id;
        });
    return iterator == entries_.cend() ? nullptr : &*iterator;
}

ShortcutManager::ScopeBackend* ShortcutManager::backendFor(
    ShortcutScope scope) noexcept {
    const auto iterator = std::find_if(
        backends_.begin(), backends_.end(), [scope](const auto& backend) {
            return backend->scope == scope;
        });
    return iterator == backends_.end() ? nullptr : iterator->get();
}

bool ShortcutManager::scopesOverlap(
    ShortcutScope first,
    ShortcutScope second) const noexcept {
    if (first == ShortcutScope::Application ||
        second == ShortcutScope::Application) {
        return true;
    }
    if (first == second) return true;
    if (first == ShortcutScope::Shared) {
        return second == ShortcutScope::Edit ||
            second == ShortcutScope::Fusion;
    }
    if (second == ShortcutScope::Shared) {
        return first == ShortcutScope::Edit ||
            first == ShortcutScope::Fusion;
    }
    return false;
}

bool ShortcutManager::isActive(ShortcutScope scope) const noexcept {
    switch (scope) {
    case ShortcutScope::Application:
        return true;
    case ShortcutScope::Shared:
        return active_page_ == ui::WorkspacePageId::Edit ||
            active_page_ == ui::WorkspacePageId::Fusion;
    case ShortcutScope::Edit:
        return active_page_ == ui::WorkspacePageId::Edit;
    case ShortcutScope::Fusion:
        return active_page_ == ui::WorkspacePageId::Fusion;
    case ShortcutScope::Render:
        return active_page_ == ui::WorkspacePageId::Render;
    }
    return false;
}

const ShortcutEntry* ShortcutManager::conflictingEntry(
    const QString& id,
    ShortcutScope scope,
    const QKeySequence& sequence) const noexcept {
    if (sequence.isEmpty()) return nullptr;
    const auto iterator = std::find_if(
        entries_.cbegin(), entries_.cend(),
        [this, &id, scope, &sequence](const ShortcutEntry& entry) {
            return entry.id != id && entry.configured_sequence == sequence &&
                scopesOverlap(scope, entry.scope);
        });
    return iterator == entries_.cend() ? nullptr : &*iterator;
}

void ShortcutManager::synchronizeBackendActions(ShortcutScope scope) {
    for (auto& entry : entries_) {
        if (entry.scope == scope && entry.action != nullptr) {
            entry.action->setShortcut(entry.configured_sequence);
        }
    }
}

void ShortcutManager::applyActiveBindings() {
    for (auto& entry : entries_) {
        if (entry.action == nullptr) continue;
        entry.action->setShortcut(
            isActive(entry.scope) ? entry.configured_sequence : QKeySequence{});
    }
}

} // namespace settings

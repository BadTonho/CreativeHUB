#include "settings/shortcut_manager.h"

#include <QAction>
#include <QSettings>

#include <algorithm>

namespace settings {

namespace {

constexpr auto kDisabledShortcutMarker = "<disabled>";

QString serializeShortcut(const QKeySequence& sequence) {
    return sequence.isEmpty()
        ? QString::fromLatin1(kDisabledShortcutMarker)
        : sequence.toString(QKeySequence::PortableText);
}

QKeySequence deserializeShortcut(const QString& value) {
    if (value == QLatin1String(kDisabledShortcutMarker)) {
        return {};
    }
    return QKeySequence(value, QKeySequence::PortableText);
}

} // namespace

void ShortcutManager::registerAction(
    const QString& id,
    const QString& label,
    QAction* action) {
    if (action == nullptr || findEntry(id) != nullptr) return;
    entries_.push_back({id, label, action, action->shortcut()});
}

void ShortcutManager::load() {
    QSettings settings;
    settings.sync();
    settings.beginGroup("shortcuts");
    for (const auto& entry : entries_) {
        if (!settings.contains(entry.id)) {
            entry.action->setShortcut(entry.default_sequence);
            continue;
        }

        const auto sequence = deserializeShortcut(
            settings.value(entry.id).toString());
        if (conflictingEntry(entry.id, sequence) != nullptr) {
            entry.action->setShortcut(entry.default_sequence);
            settings.setValue(
                entry.id,
                serializeShortcut(entry.default_sequence));
            continue;
        }
        entry.action->setShortcut(sequence);
    }
    settings.endGroup();
}

bool ShortcutManager::setShortcut(
    const QString& id,
    const QKeySequence& sequence,
    QString* conflict_message) {
    auto* entry = findEntry(id);
    if (entry == nullptr) return false;

    const auto* conflict = conflictingEntry(id, sequence);
    if (conflict != nullptr) {
        if (conflict_message != nullptr) {
            *conflict_message = QString(
                "The shortcut is already assigned to \"%1\".")
                .arg(conflict->label);
        }
        return false;
    }

    entry->action->setShortcut(sequence);
    saveShortcut(*entry);
    return true;
}

bool ShortcutManager::resetShortcut(
    const QString& id,
    QString* conflict_message) {
    auto* entry = findEntry(id);
    if (entry == nullptr) return false;
    return setShortcut(id, entry->default_sequence, conflict_message);
}

void ShortcutManager::resetAll() {
    QSettings settings;
    settings.beginGroup("shortcuts");
    settings.clear();
    for (const auto& entry : entries_) {
        entry.action->setShortcut(entry.default_sequence);
        settings.setValue(
            entry.id,
            serializeShortcut(entry.default_sequence));
    }
    settings.endGroup();
    settings.sync();
}

const std::vector<ShortcutEntry>& ShortcutManager::entries() const noexcept {
    return entries_;
}

QKeySequence ShortcutManager::shortcut(const QString& id) const {
    const auto* entry = findEntry(id);
    return entry == nullptr ? QKeySequence() : entry->action->shortcut();
}

ShortcutEntry* ShortcutManager::findEntry(const QString& id) noexcept {
    const auto iterator = std::find_if(
        entries_.begin(), entries_.end(),
        [&id](const ShortcutEntry& entry) { return entry.id == id; });
    return iterator == entries_.end() ? nullptr : &*iterator;
}

const ShortcutEntry* ShortcutManager::findEntry(
    const QString& id) const noexcept {
    const auto iterator = std::find_if(
        entries_.cbegin(), entries_.cend(),
        [&id](const ShortcutEntry& entry) { return entry.id == id; });
    return iterator == entries_.cend() ? nullptr : &*iterator;
}

const ShortcutEntry* ShortcutManager::conflictingEntry(
    const QString& id,
    const QKeySequence& sequence) const noexcept {
    if (sequence.isEmpty()) return nullptr;
    const auto iterator = std::find_if(
        entries_.cbegin(), entries_.cend(),
        [&id, &sequence](const ShortcutEntry& entry) {
            return entry.id != id && entry.action != nullptr &&
                entry.action->shortcut() == sequence;
        });
    return iterator == entries_.cend() ? nullptr : &*iterator;
}

void ShortcutManager::saveShortcut(const ShortcutEntry& entry) {
    QSettings settings;
    settings.beginGroup("shortcuts");
    settings.setValue(
        entry.id,
        serializeShortcut(entry.action->shortcut()));
    settings.endGroup();
    settings.sync();
}

} // namespace settings

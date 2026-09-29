#include <creative_suite/shortcuts/shortcut_manager.h>

#include <QAction>
#include <QSettings>
#include <QVariant>

#include <algorithm>
#include <iterator>
#include <utility>

namespace creative_suite::shortcuts {
namespace {

constexpr auto kDisabledShortcutMarker = "<disabled>";

QString serializeShortcut(const QKeySequence& sequence)
{
    return sequence.isEmpty()
        ? QString::fromLatin1(kDisabledShortcutMarker)
        : sequence.toString(QKeySequence::PortableText);
}

QKeySequence deserializeShortcut(const QString& value)
{
    if (value == QLatin1String(kDisabledShortcutMarker)) return {};
    return QKeySequence(value, QKeySequence::PortableText);
}

void setError(QString* destination, const QString& message)
{
    if (destination != nullptr) *destination = message;
}

} // namespace

ShortcutManager::ShortcutManager(QString settings_group)
    : settings_group_(settings_group.isEmpty()
          ? QStringLiteral("shortcuts") : std::move(settings_group))
{
}

void ShortcutManager::registerAction(
    const QString& id,
    const QString& label,
    QAction* action)
{
    if (action == nullptr || findEntry(id) != nullptr) return;
    entries_.push_back({id, label, action, action->shortcut()});
}

bool ShortcutManager::load(QString* error_message)
{
    QSettings settings;
    settings.sync();
    bool succeeded = settings.status() == QSettings::NoError;
    settings.beginGroup(settings_group_);
    for (const auto& entry : entries_) {
        if (entry.action == nullptr) continue;
        if (!settings.contains(entry.id)) {
            entry.action->setShortcut(entry.default_sequence);
            continue;
        }

        const auto sequence = deserializeShortcut(settings.value(entry.id).toString());
        if (conflictingEntry(entry.id, sequence) != nullptr) {
            entry.action->setShortcut(entry.default_sequence);
            settings.setValue(entry.id, serializeShortcut(entry.default_sequence));
            continue;
        }
        entry.action->setShortcut(sequence);
    }
    settings.endGroup();
    settings.sync();
    succeeded = succeeded && settings.status() == QSettings::NoError;
    if (!succeeded) {
        setError(error_message,
                 QStringLiteral("Shortcut preferences could not be loaded from %1.")
                     .arg(settings.fileName()));
    } else {
        setError(error_message, {});
    }
    return succeeded;
}

bool ShortcutManager::setShortcut(
    const QString& id,
    const QKeySequence& sequence,
    QString* conflict_message)
{
    auto* entry = findEntry(id);
    if (entry == nullptr || entry->action == nullptr) return false;

    const auto* conflict = conflictingEntry(id, sequence);
    if (conflict != nullptr) {
        setError(conflict_message,
                 QStringLiteral("The shortcut is already assigned to \"%1\".")
                     .arg(conflict->label));
        return false;
    }

    entry->action->setShortcut(sequence);
    saveShortcut(*entry);
    setError(conflict_message, {});
    return true;
}

bool ShortcutManager::applyShortcuts(
    const std::vector<ShortcutAssignment>& assignments,
    QString* error_message)
{
    std::vector<QKeySequence> resulting_sequences;
    resulting_sequences.reserve(entries_.size());
    for (const auto& entry : entries_) {
        resulting_sequences.push_back(
            entry.action != nullptr ? entry.action->shortcut() : QKeySequence{});
    }

    std::vector<std::size_t> entry_indices;
    entry_indices.reserve(assignments.size());
    for (std::size_t assignment_index = 0;
         assignment_index < assignments.size(); ++assignment_index) {
        const auto& assignment = assignments[assignment_index];
        const auto* entry = findEntry(assignment.id);
        if (entry == nullptr || entry->action == nullptr) {
            setError(error_message,
                     QStringLiteral("Unknown shortcut command: %1").arg(assignment.id));
            return false;
        }
        for (std::size_t previous = 0; previous < assignment_index; ++previous) {
            if (assignments[previous].id == assignment.id) {
                setError(error_message,
                         QStringLiteral("Shortcut command %1 appears more than once.")
                             .arg(assignment.id));
                return false;
            }
        }
        const auto iterator = std::find_if(
            entries_.cbegin(), entries_.cend(), [&assignment](const ShortcutEntry& candidate) {
                return candidate.id == assignment.id;
            });
        const auto entry_index = static_cast<std::size_t>(
            std::distance(entries_.cbegin(), iterator));
        entry_indices.push_back(entry_index);
        resulting_sequences[entry_index] = assignment.sequence;
    }

    for (std::size_t first = 0; first < entries_.size(); ++first) {
        if (resulting_sequences[first].isEmpty()) continue;
        for (std::size_t second = first + 1; second < entries_.size(); ++second) {
            if (resulting_sequences[first] == resulting_sequences[second]) {
                setError(error_message,
                         QStringLiteral("The shortcut is assigned to both \"%1\" and \"%2\".")
                             .arg(entries_[first].label, entries_[second].label));
                return false;
            }
        }
    }

    if (assignments.empty()) {
        setError(error_message, {});
        return true;
    }

    struct StoredValue {
        QString id;
        bool existed = false;
        QVariant value;
    };
    std::vector<StoredValue> previous_values;
    previous_values.reserve(assignments.size());

    QSettings settings;
    settings.beginGroup(settings_group_);
    for (const auto& assignment : assignments) {
        const bool existed = settings.contains(assignment.id);
        previous_values.push_back({
            assignment.id, existed, existed ? settings.value(assignment.id) : QVariant{}});
        settings.setValue(assignment.id, serializeShortcut(assignment.sequence));
    }
    settings.endGroup();
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        settings.beginGroup(settings_group_);
        for (const auto& previous : previous_values) {
            if (previous.existed) settings.setValue(previous.id, previous.value);
            else settings.remove(previous.id);
        }
        settings.endGroup();
        settings.sync();
        setError(error_message,
                 QStringLiteral("Shortcut preferences could not be saved to %1.")
                     .arg(settings.fileName()));
        return false;
    }

    for (std::size_t index = 0; index < assignments.size(); ++index) {
        entries_[entry_indices[index]].action->setShortcut(assignments[index].sequence);
    }
    setError(error_message, {});
    return true;
}

bool ShortcutManager::resetShortcut(
    const QString& id,
    QString* conflict_message)
{
    const auto* entry = findEntry(id);
    if (entry == nullptr) return false;
    return setShortcut(id, entry->default_sequence, conflict_message);
}

void ShortcutManager::resetAll()
{
    QSettings settings;
    settings.beginGroup(settings_group_);
    for (const auto& entry : entries_) {
        if (entry.action == nullptr) continue;
        entry.action->setShortcut(entry.default_sequence);
        settings.setValue(entry.id, serializeShortcut(entry.default_sequence));
    }
    settings.endGroup();
    settings.sync();
}

const std::vector<ShortcutEntry>& ShortcutManager::entries() const noexcept
{
    return entries_;
}

QKeySequence ShortcutManager::shortcut(const QString& id) const
{
    const auto* entry = findEntry(id);
    return entry == nullptr || entry->action == nullptr
        ? QKeySequence{} : entry->action->shortcut();
}

const QString& ShortcutManager::settingsGroup() const noexcept
{
    return settings_group_;
}

ShortcutEntry* ShortcutManager::findEntry(const QString& id) noexcept
{
    const auto iterator = std::find_if(
        entries_.begin(), entries_.end(), [&id](const ShortcutEntry& entry) {
            return entry.id == id;
        });
    return iterator == entries_.end() ? nullptr : &*iterator;
}

const ShortcutEntry* ShortcutManager::findEntry(const QString& id) const noexcept
{
    const auto iterator = std::find_if(
        entries_.cbegin(), entries_.cend(), [&id](const ShortcutEntry& entry) {
            return entry.id == id;
        });
    return iterator == entries_.cend() ? nullptr : &*iterator;
}

const ShortcutEntry* ShortcutManager::conflictingEntry(
    const QString& id,
    const QKeySequence& sequence) const noexcept
{
    if (sequence.isEmpty()) return nullptr;
    const auto iterator = std::find_if(
        entries_.cbegin(), entries_.cend(), [&id, &sequence](const ShortcutEntry& entry) {
            return entry.id != id && entry.action != nullptr &&
                entry.action->shortcut() == sequence;
        });
    return iterator == entries_.cend() ? nullptr : &*iterator;
}

void ShortcutManager::saveShortcut(const ShortcutEntry& entry)
{
    QSettings settings;
    settings.beginGroup(settings_group_);
    settings.setValue(entry.id, serializeShortcut(entry.action->shortcut()));
    settings.endGroup();
    settings.sync();
}

} // namespace creative_suite::shortcuts

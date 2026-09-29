#pragma once

#include <QKeySequence>
#include <QString>

#include <vector>

class QAction;

namespace creative_suite::shortcuts {

struct ShortcutEntry {
    QString id;
    QString label;
    QAction* action = nullptr;
    QKeySequence default_sequence;
};

struct ShortcutAssignment {
    QString id;
    QKeySequence sequence;
};

// Registers application-owned actions and persists their shortcuts in the
// current application's QSettings scope under a configurable group.
class ShortcutManager final {
public:
    explicit ShortcutManager(QString settings_group = QStringLiteral("shortcuts"));

    void registerAction(
        const QString& id,
        const QString& label,
        QAction* action);
    bool load(QString* error_message = nullptr);
    [[nodiscard]] bool setShortcut(
        const QString& id,
        const QKeySequence& sequence,
        QString* conflict_message = nullptr);
    [[nodiscard]] bool applyShortcuts(
        const std::vector<ShortcutAssignment>& assignments,
        QString* error_message = nullptr);
    [[nodiscard]] bool resetShortcut(
        const QString& id,
        QString* conflict_message = nullptr);
    void resetAll();

    [[nodiscard]] const std::vector<ShortcutEntry>& entries() const noexcept;
    [[nodiscard]] QKeySequence shortcut(const QString& id) const;
    [[nodiscard]] const QString& settingsGroup() const noexcept;

private:
    [[nodiscard]] ShortcutEntry* findEntry(const QString& id) noexcept;
    [[nodiscard]] const ShortcutEntry* findEntry(const QString& id) const noexcept;
    [[nodiscard]] const ShortcutEntry* conflictingEntry(
        const QString& id,
        const QKeySequence& sequence) const noexcept;
    void saveShortcut(const ShortcutEntry& entry);

    QString settings_group_;
    std::vector<ShortcutEntry> entries_;
};

} // namespace creative_suite::shortcuts

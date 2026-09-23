#pragma once

#include <QKeySequence>
#include <QString>

#include <vector>

class QAction;

namespace settings {

struct ShortcutEntry {
    QString id;
    QString label;
    QAction* action = nullptr;
    QKeySequence default_sequence;
};

class ShortcutManager final {
public:
    void registerAction(
        const QString& id,
        const QString& label,
        QAction* action);
    void load();
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

private:
    [[nodiscard]] ShortcutEntry* findEntry(const QString& id) noexcept;
    [[nodiscard]] const ShortcutEntry* findEntry(
        const QString& id) const noexcept;
    [[nodiscard]] const ShortcutEntry* conflictingEntry(
        const QString& id,
        const QKeySequence& sequence) const noexcept;
    void saveShortcut(const ShortcutEntry& entry);

    std::vector<ShortcutEntry> entries_;
};

} // namespace settings

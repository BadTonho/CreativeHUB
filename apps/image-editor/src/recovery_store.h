#pragma once

#include <QString>
#include <QStringList>

namespace image_editor {

class ImageDocumentSession;

class RecoveryStore final {
public:
    explicit RecoveryStore(QString application_data_directory = {});

    [[nodiscard]] QString recoveryDirectory() const;
    [[nodiscard]] QString pathFor(const ImageDocumentSession& session) const;
    [[nodiscard]] bool save(const ImageDocumentSession& session, QString* error = nullptr) const;
    [[nodiscard]] QStringList snapshots() const;
    [[nodiscard]] bool remove(const QString& recovery_path) const;

private:
    QString application_data_directory_;
};

} // namespace image_editor

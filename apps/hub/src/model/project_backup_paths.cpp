#include "project_backup_paths.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QStringList>

#include <filesystem>
#include <initializer_list>
#include <system_error>

namespace creative_suite::hub {
namespace {

class PathRebaser {
public:
    PathRebaser(const QString& directory, bool restrictRelativePaths, int formatVersion)
        : base(directory), restrictRelative(restrictRelativePaths), version(formatVersion) {}

    bool paths(QJsonObject& object, std::initializer_list<const char*> keys) {
        for (const auto* key : keys) {
            const auto name = QLatin1String(key);
            if (!object.contains(name)) continue;
            const auto value = object.value(name);
            if (!value.isString() || value.toString().contains(QChar::Null))
                return fail(QStringLiteral("O campo de caminho '%1' é inválido.").arg(name));
            const auto path = value.toString();
            if (path.isEmpty() || QDir::isAbsolutePath(path)) continue;
            const auto relative = QDir::cleanPath(path);
            const auto absolute = QDir::cleanPath(base.filePath(path));
            if (restrictRelative) {
                // Motion's reader rejects both lexical and symlink escapes.
                std::error_code ec;
                const auto resolved = std::filesystem::weakly_canonical(
                    QFileInfo(absolute).filesystemFilePath(), ec);
                const auto inside = resolved.lexically_relative(
                    QFileInfo(base.absolutePath()).filesystemFilePath());
                if (relative == ".." || relative.startsWith("../") || ec ||
                    inside.empty() || inside.is_absolute() || *inside.begin() == "..")
                    return fail(QStringLiteral("Uma referência relativa sai da pasta do documento: %1").arg(path));
            }
            object.insert(name, absolute);
            changed = true;
        }
        return true;
    }

    template<typename Function>
    bool child(QJsonObject& owner, const char* key, Function function) {
        const auto name = QLatin1String(key);
        if (!owner.contains(name)) return true;
        if (!owner.value(name).isObject())
            return fail(QStringLiteral("A seção de referências '%1' é inválida.").arg(name));
        auto object = owner.value(name).toObject();
        if (!function(object)) return false;
        owner.insert(name, object);
        return true;
    }

    template<typename Function>
    bool array(QJsonObject& owner, const char* key, Function function) {
        const auto name = QLatin1String(key);
        if (!owner.contains(name)) return true;
        if (!owner.value(name).isArray())
            return fail(QStringLiteral("A lista de referências '%1' é inválida.").arg(name));
        auto values = owner.value(name).toArray();
        for (qsizetype i = 0; i < values.size(); ++i) {
            if (!values[i].isObject())
                return fail(QStringLiteral("A lista '%1' contém uma referência inválida.").arg(name));
            auto object = values[i].toObject();
            if (!function(object)) return false;
            values[i] = object;
        }
        owner.insert(name, values);
        return true;
    }

    bool imageOperations(QJsonObject& owner) {
        if (version < 11) return true;
        return array(owner, "operations", [this](QJsonObject& operation) {
            return operation.value("kind") != "raster_image" || paths(operation, {"path"});
        });
    }

    bool imageLayer(QJsonObject& layer) {
        return imageOperations(layer) &&
            (version < 8 || layer.value("kind") != "group" ||
             array(layer, "children", [this](QJsonObject& childLayer) { return imageLayer(childLayer); }));
    }

    bool image(QJsonObject& root) {
        const bool baseOk = version == 1
            ? child(root, "source", [this](QJsonObject& source) { return paths(source, {"path"}); })
            : child(root, "base", [this](QJsonObject& source) {
                return source.value("kind") != "source_image" || paths(source, {"path"});
            });
        return baseOk && imageOperations(root) &&
            (version < 4 || array(root, "layers", [this](QJsonObject& layer) { return imageLayer(layer); }));
    }

    bool videoLinks(QJsonObject& owner, bool clip = false) {
        return (version < 10 || child(owner, clip ? "image_editor_variant" : "image_editor_link",
            [this](QJsonObject& link) { return paths(link, {"document", "output"}); })) &&
            (version < 24 || child(owner, "motion_link", [this](QJsonObject& link) { return paths(link, {"document", "output", "source"}); }));
    }

    bool videoClips(QJsonObject& owner) {
        return array(owner, "clips", [this](QJsonObject& clip) {
            return (clip.value("kind") == "text" || clip.value("kind") == "adjustment" || clip.value("kind") == "fusion_composition" || paths(clip, {"source"})) && videoLinks(clip, true) &&
                (version < 20 || child(clip, "node_graph", [this](QJsonObject& graph) {
                    return array(graph, "nodes", [this](QJsonObject& node) {
                        return node.value("type") != "input" || paths(node, {"source"});
                    });
                }));
        });
    }

    bool video(QJsonObject& root) {
        return array(root, "media", [this](QJsonObject& media) { return paths(media, {"path"}) && videoLinks(media); }) &&
            child(root, "timeline", [this](QJsonObject& timeline) {
                return version == 1 ? videoClips(timeline) :
                    array(timeline, "tracks", [this](QJsonObject& track) { return videoClips(track); });
            });
    }

    bool motion(QJsonObject& root) {
        return child(root, "media_pool", [this](QJsonObject& pool) {
            return array(pool, "items", [this](QJsonObject& item) { return paths(item, {"path"}); });
        }) && array(root, "layers", [this](QJsonObject& layer) {
            return paths(layer, {"source_path"}) && (version < 6 || child(layer, "linked_image", [this](QJsonObject& link) {
                return paths(link, {"document_path", "published_output_path", "source_snapshot_path"});
            }));
        });
    }

    bool changed = false;
    QString error;

private:
    bool fail(const QString& cause) { error = cause; return false; }
    QDir base;
    bool restrictRelative;
    int version;
};

} // namespace

bool prepareProjectBackup(const QByteArray& original, const QString& originalPath,
                          QByteArray* prepared, QString* error) {
    const auto fail = [error](const QString& cause) { if (error) *error = cause; return false; };
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(original, &parseError);
    auto root = document.object();
    const auto format = root.value("format").toString();
    const bool image = format == "creative-suite-image-document";
    const bool video = format == "creative-suite.main-editor";
    const bool motion = format == "creative-suite.motion-studio";
    const QStringList editorSuffixes{"cimg", "csie", "csp", "csve", "motion", "csme"};
    if (!image && !video && !motion) {
        if (editorSuffixes.contains(QFileInfo(originalPath).suffix().toLower()))
            return fail(QStringLiteral("O projeto não tem um formato JSON reconhecido (código %1): %2")
                .arg(static_cast<int>(parseError.error)).arg(parseError.errorString()));
        *prepared = original;
        return true;
    }
    const int version = root.value("version").toInt(-1);
    const int maximum = image ? 24 : video ? 31 : 17;
    if (version < 1 || version > maximum)
        return fail(QStringLiteral("A versão %1 do formato '%2' não é suportada para backup.").arg(version).arg(format));
    const QFileInfo source(originalPath);
    std::error_code canonicalError;
    const auto canonical = std::filesystem::weakly_canonical(source.filesystemFilePath(), canonicalError);
    const QString base = image || canonicalError ? source.absolutePath()
        : QString::fromStdU16String(canonical.parent_path().u16string());
    PathRebaser rebaser(base, motion, version);
    if (!(image ? rebaser.image(root) : video ? rebaser.video(root) : rebaser.motion(root)))
        return fail(rebaser.error);
    *prepared = rebaser.changed ? QJsonDocument(root).toJson(QJsonDocument::Indented) : original;
    return true;
}

} // namespace creative_suite::hub

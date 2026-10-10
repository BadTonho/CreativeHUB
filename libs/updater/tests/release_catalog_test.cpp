#include <creative_suite/updater/release_catalog.h>

#include <QCoreApplication>
#include "../../../cmake/test_support/test_check.h"
#include <iostream>

using creative_suite::updater::ReleaseCatalog;
using creative_suite::updater::compareVersions;

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    CS_TEST_CHECK(compareVersions(QStringLiteral("0.1.10"), QStringLiteral("0.1.9")) == 1);
    CS_TEST_CHECK(compareVersions(QStringLiteral("1.2.0"), QStringLiteral("1.2.0")) == 0);
    CS_TEST_CHECK(compareVersions(QStringLiteral("1.2.3"), QStringLiteral("1.2.4")) == -1);
    CS_TEST_CHECK(!compareVersions(QStringLiteral("Beta 1.2.3"), QStringLiteral("1.2.3")));

    const QByteArray valid = R"({
        "schema_version": 1,
        "suite_version": "suite-2026.10.06",
        "applications": {
            "video-editor": {
                "version": "0.1.7",
                "installer_asset": "video-editor-setup.exe",
                "size_bytes": 42,
                "sha256": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
                "release_notes": "Timeline improvements"
            }
        }
    })";
    QString error;
    const auto catalog = ReleaseCatalog::fromJson(valid, &error);
    CS_TEST_CHECK(catalog.has_value());
    CS_TEST_CHECK(catalog->find(QStringLiteral("video-editor")) != nullptr);
    CS_TEST_CHECK(catalog->find(QStringLiteral("hub")) == nullptr);

    const QByteArray path_traversal = R"({
        "schema_version": 1,
        "applications": {
            "video-editor": {
                "version": "0.1.7",
                "installer_asset": "../setup.exe",
                "size_bytes": 42,
                "sha256": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
            }
        }
    })";
    CS_TEST_CHECK(!ReleaseCatalog::fromJson(path_traversal, &error));
    CS_TEST_CHECK(!ReleaseCatalog::fromJson(QByteArrayLiteral("{}"), &error));

    auto invalid_size = valid;
    invalid_size.replace(QByteArrayLiteral("\"size_bytes\": 42"),
                         QByteArrayLiteral("\"size_bytes\": 42.5"));
    CS_TEST_CHECK(!ReleaseCatalog::fromJson(invalid_size, &error));

    const QByteArray invalid_app_id = R"({
        "schema_version": 1,
        "applications": {
            "../video-editor": {
                "version": "0.1.7",
                "installer_asset": "video-editor-setup.exe",
                "size_bytes": 42,
                "sha256": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
            }
        }
    })";
    CS_TEST_CHECK(!ReleaseCatalog::fromJson(invalid_app_id, &error));

    const QByteArray invalid_digest = R"({
        "schema_version": 1,
        "applications": {
            "video-editor": {
                "version": "0.1.7",
                "installer_asset": "video-editor-setup.exe",
                "size_bytes": 42,
                "sha256": "not-a-sha256-digest"
            }
        }
    })";
    CS_TEST_CHECK(!ReleaseCatalog::fromJson(invalid_digest, &error));

    const auto url = creative_suite::updater::resolveInstallerUrl(
        QUrl(QStringLiteral("https://github.com/example/project/releases/latest/download")),
        QStringLiteral("video-editor-setup.exe"));
    CS_TEST_CHECK(url.toString().endsWith(QStringLiteral("/video-editor-setup.exe")));
    CS_TEST_CHECK(!creative_suite::updater::resolveInstallerUrl(
        QUrl(QStringLiteral("https://example.test/download")), QStringLiteral("../bad.exe")).isValid());

    std::cout << "Updater catalog tests passed.\n";
    return 0;
}

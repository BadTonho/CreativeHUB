#include "../src/model/app_catalog.h"
#include "../../../cmake/test_support/test_check.h"
#include <iostream>

using namespace creative_suite::hub;

int main() {
    AppCatalog catalog;

    const auto& apps = catalog.apps();
    CS_TEST_CHECK(apps.size() == 3);
    CS_TEST_CHECK(apps[0].id() == QStringLiteral("video-editor"));
    CS_TEST_CHECK(apps[1].id() == QStringLiteral("image-editor"));
    CS_TEST_CHECK(apps[2].id() == QStringLiteral("motion-editor"));

    auto found = catalog.findApp(QStringLiteral("video-editor"));
    CS_TEST_CHECK(found.has_value());
    CS_TEST_CHECK(found->name() == QStringLiteral("Video Editor"));
    CS_TEST_CHECK(!found->features().isEmpty());
    CS_TEST_CHECK(!found->projectFormat().isEmpty());

    catalog.updateAppStatus(QStringLiteral("video-editor"), AppStatus::Installed);
    catalog.updateLatestVersion(QStringLiteral("video-editor"), QStringLiteral("0.2.0"));
    auto updated = catalog.findApp(QStringLiteral("video-editor"));
    CS_TEST_CHECK(updated.has_value());
    CS_TEST_CHECK(updated->status() == AppStatus::Installed);
    CS_TEST_CHECK(updated->isInstalled() == true);
    CS_TEST_CHECK(updated->latestVersion() == QStringLiteral("0.2.0"));

    auto notFound = catalog.findApp(QStringLiteral("non-existent-app"));
    CS_TEST_CHECK(!notFound.has_value());

    std::cout << "All AppCatalog tests passed successfully.\n";
    return 0;
}

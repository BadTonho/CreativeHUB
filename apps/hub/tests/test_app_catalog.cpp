#include "../src/model/app_catalog.h"
#include <cassert>
#include <iostream>

using namespace creative_suite::hub;

int main() {
    AppCatalog catalog;

    const auto& apps = catalog.apps();
    assert(apps.size() == 3);
    assert(apps[0].id() == QStringLiteral("video-editor"));
    assert(apps[1].id() == QStringLiteral("image-editor"));
    assert(apps[2].id() == QStringLiteral("motion-editor"));

    auto found = catalog.findApp(QStringLiteral("video-editor"));
    assert(found.has_value());
    assert(found->name() == QStringLiteral("Video Editor"));
    assert(!found->features().isEmpty());
    assert(!found->projectFormat().isEmpty());

    catalog.updateAppStatus(QStringLiteral("video-editor"), AppStatus::Installed);
    auto updated = catalog.findApp(QStringLiteral("video-editor"));
    assert(updated.has_value());
    assert(updated->status() == AppStatus::Installed);
    assert(updated->isInstalled() == true);

    auto notFound = catalog.findApp(QStringLiteral("non-existent-app"));
    assert(!notFound.has_value());

    std::cout << "All AppCatalog tests passed successfully.\n";
    return 0;
}

#pragma once

#include <QString>

namespace creative_suite::hub {

class HubStyle {
public:
    static QString globalStyleSheet();
    static QString primaryButtonStyle();
    static QString secondaryButtonStyle();
    static QString searchInputStyle();
};

} // namespace creative_suite::hub

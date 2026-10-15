#pragma once

#include <QCoreApplication>
#include <QString>

namespace motion::ui {

// Keep Motion Studio UI strings in one stable context, including strings
// assembled in helper functions outside QObject subclasses.
class MotionUiTranslation final
{
    Q_DECLARE_TR_FUNCTIONS(MotionUiTranslation)
};

} // namespace motion::ui

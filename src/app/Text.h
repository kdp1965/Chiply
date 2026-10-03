#pragma once
// "1 error", "3 errors": English plurals without a translation file (Qt's
// %n leaves "error(s)" as typed).
#include <QString>

inline QString countOf(int n, const char* singular, const char* plural)
{
    return QString::number(n) + QLatin1Char(' ') + QString::fromUtf8(n == 1 ? singular : plural);
}

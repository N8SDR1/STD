// Qt 5 leftovers the MSHV FT8 sources still call. Force-included
// ahead of those files so the algorithm sources stay untouched.
#pragma once

#include <QChar>
#include <QString>
#include <QtGlobal>

#include <algorithm>

inline bool operator<=(QChar c, int n) { return int(c.unicode()) <= n; }
inline bool operator>=(QChar c, int n) { return int(c.unicode()) >= n; }
inline bool operator<(QChar c, int n) { return int(c.unicode()) < n; }
inline bool operator>(QChar c, int n) { return int(c.unicode()) > n; }
inline bool operator==(QChar c, int n) { return int(c.unicode()) == n; }
inline bool operator!=(QChar c, int n) { return int(c.unicode()) != n; }
inline bool operator<=(int n, QChar c) { return n <= int(c.unicode()); }
inline bool operator>=(int n, QChar c) { return n >= int(c.unicode()); }
inline bool operator<(int n, QChar c) { return n < int(c.unicode()); }
inline bool operator>(int n, QChar c) { return n > int(c.unicode()); }

// QString::midRef() was removed. mid() returns a QString, and the
// call sites use toInt() / toDouble() on that.
#define midRef mid

// QDateTime::toTime_t() was removed. toSecsSinceEpoch() is the same clock.
#define toTime_t toSecsSinceEpoch

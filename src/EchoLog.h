// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QColor>
#include <QScrollBar>
#include <QString>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextEdit>

// Receive color used for RTTY, PSK, Olivia, and the other keyboard decodes.
// TX echo stays red. Hell uses the same pair in the shared receive log.
inline QColor rxEchoColor() {
  return QColor(0xd5, 0xde, 0xea);
}

inline QColor txEchoColor() {
  return QColor(0xff, 0x5c, 0x5c);
}

// True when the log is empty or the thumb is already on the latest line.
inline bool echoViewAtBottom(const QTextEdit* echo) {
  if (!echo)
    return true;
  const QScrollBar* bar = echo->verticalScrollBar();
  if (!bar)
    return true;
  return bar->maximum() <= 0 || bar->value() >= bar->maximum() - 2;
}

inline void followEcho(QTextEdit* echo, const QTextCursor& cursor, bool follow) {
  if (!echo || !follow)
    return;
  echo->setTextCursor(cursor);
  echo->ensureCursorVisible();
  if (QScrollBar* bar = echo->verticalScrollBar())
    bar->setValue(bar->maximum());
}

// Insert at the end. Earlier text stays. The view moves only when it was
// already on the latest line, so scrolling back is not yanked to the bottom.
inline void appendKeyedEcho(QTextEdit* echo, const QString& text, const QColor& color) {
  if (!echo || text.isEmpty())
    return;
  const bool follow = echoViewAtBottom(echo);
  QTextCursor cursor(echo->document());
  cursor.movePosition(QTextCursor::End);
  QTextCharFormat format;
  format.setForeground(color);
  cursor.setCharFormat(format);
  cursor.insertText(text);
  followEcho(echo, cursor, follow);
}

// Newline before the next Hell TX echo when the log is mid-line.
inline void breakEchoLine(QTextEdit* echo) {
  if (!echo)
    return;
  QTextCursor cursor(echo->document());
  cursor.movePosition(QTextCursor::End);
  if (cursor.atBlockStart())
    return;
  const bool follow = echoViewAtBottom(echo);
  QTextCharFormat format;
  format.setForeground(txEchoColor());
  cursor.setCharFormat(format);
  cursor.insertText(QStringLiteral("\n"));
  followEcho(echo, cursor, follow);
}

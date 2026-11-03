// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "MacroFile.h"

#include <algorithm>
#include <optional>

#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QTimeZone>

namespace {

QString unescapeLine(QString line) {
  if (line.endsWith(QStringLiteral("\\n"))) {
    line.chop(2);
    line.append(QLatin1Char('\n'));
  }
  return line;
}

// "<QSY:FFF.F[:NNNN]>" -> "QSY", "<@TXRSID:off>" -> "TXRSID".
QString bareTagName(QString tag) {
  if (tag.startsWith(QLatin1Char('<')))
    tag.remove(0, 1);
  if (tag.startsWith(QLatin1Char('@')))
    tag.remove(0, 1);
  int end = 0;
  while (end < tag.size() && (tag.at(end).isLetterOrNumber() || tag.at(end) == QLatin1Char('_') ||
                              tag.at(end) == QLatin1Char('/') || tag.at(end) == QLatin1Char('#') ||
                              tag.at(end) == QLatin1Char('+')))
    ++end;
  return tag.left(end).toUpper();
}

const QSet<QString>& fldigiTagNames() {
  static const QSet<QString> names = [] {
    QSet<QString> out;
    for (const MacroTagRow& row : macroTagList()) {
      const QString name = bareTagName(row.tag);
      if (!name.isEmpty())
        out.insert(name);
    }
    out.insert(QStringLiteral("MODEM"));
    out.insert(QStringLiteral("COMMENT"));
    return out;
  }();
  return names;
}

// The other station's fields. An empty one is reported, not sent blank.
const QHash<QString, QString>& otherStationLabels() {
  static const QHash<QString, QString> labels = {
      {QStringLiteral("CALL"), QStringLiteral("Call")}, {QStringLiteral("NAME"), QStringLiteral("Name")},
      {QStringLiteral("QTH"), QStringLiteral("QTH")},   {QStringLiteral("ST"), QStringLiteral("St")},
      {QStringLiteral("PR"), QStringLiteral("Pr")},     {QStringLiteral("LOC"), QStringLiteral("Loc")},
      {QStringLiteral("RST"), QStringLiteral("RST out")},
  };
  return labels;
}

// Fldigi time formats are strftime. These are the codes people use.
QString strftimeLike(const QString& format, const QDateTime& when) {
  QString out;
  for (int i = 0; i < format.size(); ++i) {
    const QChar c = format.at(i);
    if (c != QLatin1Char('%') || i + 1 >= format.size()) {
      out += c;
      continue;
    }
    const QChar code = format.at(++i);
    switch (code.unicode()) {
      case 'Y': out += when.toString(QStringLiteral("yyyy")); break;
      case 'y': out += when.toString(QStringLiteral("yy")); break;
      case 'm': out += when.toString(QStringLiteral("MM")); break;
      case 'd': out += when.toString(QStringLiteral("dd")); break;
      case 'H': out += when.toString(QStringLiteral("HH")); break;
      case 'M': out += when.toString(QStringLiteral("mm")); break;
      case 'S': out += when.toString(QStringLiteral("ss")); break;
      case 'b': out += when.toString(QStringLiteral("MMM")); break;
      case 'B': out += when.toString(QStringLiteral("MMMM")); break;
      case 'a': out += when.toString(QStringLiteral("ddd")); break;
      case 'A': out += when.toString(QStringLiteral("dddd")); break;
      case 'x': out += when.toString(QStringLiteral("yyyy-MM-dd")); break;
      case 'Z': out += when.timeSpec() == Qt::UTC ? QStringLiteral("UTC") : when.timeZoneAbbreviation(); break;
      case 'z': out += when.toString(QStringLiteral("t")); break;
      case '%': out += QLatin1Char('%'); break;
      default: out += QLatin1Char('%') + code; break;
    }
  }
  return out;
}

// The value of a fill-in tag, or nullopt when the name is not a fill-in.
std::optional<QString> fillValue(const QString& name, const QString& arg, const MacroContext& c,
                                 const QHash<QString, QString>& upperTags) {
  const QDateTime utc = c.nowUtc.isValid() ? c.nowUtc.toUTC() : QDateTime::currentDateTimeUtc();
  const QDateTime local = utc.toLocalTime();
  auto timeTag = [&](const QDateTime& when, const QString& fallback) {
    return strftimeLike(arg.isEmpty() ? fallback : arg, when);
  };
  if (name == QLatin1String("MYCALL"))
    return c.myCall.toUpper();
  if (name == QLatin1String("MYLOC") || name == QLatin1String("MYGRID"))
    return c.myLoc;
  if (upperTags.contains(name))
    return upperTags.value(name);
  if (otherStationLabels().contains(name))
    return QString();
  if (name == QLatin1String("MODE") || name == QLatin1String("DIGI"))
    return c.mode;
  if (name == QLatin1String("BAND"))
    return c.band;
  if (name == QLatin1String("FREQ"))
    return c.freqKhz;
  if (name == QLatin1String("VER"))
    return c.version;
  if (name == QLatin1String("CNTR") || name == QLatin1String("SERNO") || name == QLatin1String("#"))
    return QStringLiteral("%1").arg(std::max(0, c.serial), 3, 10, QLatin1Char('0'));
  if (name == QLatin1String("LASTNO"))
    return QStringLiteral("%1").arg(std::max(0, c.serial - 1), 3, 10, QLatin1Char('0'));
  if (name == QLatin1String("ZDT") || name == QLatin1String("IZDT"))
    return timeTag(utc, QStringLiteral("%Y-%m-%d %H:%MZ"));
  if (name == QLatin1String("LDT") || name == QLatin1String("ILDT"))
    return timeTag(local, QStringLiteral("%Y-%m-%d %H:%M"));
  if (name == QLatin1String("ZT"))
    return timeTag(utc, QStringLiteral("%H%MZ"));
  if (name == QLatin1String("LT"))
    return timeTag(local, QStringLiteral("%H%M"));
  if (name == QLatin1String("ZD"))
    return timeTag(utc, QStringLiteral("%Y-%m-%d"));
  if (name == QLatin1String("LD"))
    return timeTag(local, QStringLiteral("%Y-%m-%d"));
  if (name == QLatin1String("QSOTIME"))
    return utc.toString(QStringLiteral("HHmm"));
  return std::nullopt;
}

// {CALL} and {NAME|OM} become <CALL> and <NAME|OM>. {#} is the serial.
// Braces around anything else stay as typed.
QString bracesToTags(const QString& body, const MacroContext& context) {
  static const QRegularExpression braces(QStringLiteral("\\{([A-Za-z_#][A-Za-z0-9_/]*)(\\|[^}]*)?\\}"));
  static const QSet<QString> actions = {QStringLiteral("TX"),     QStringLiteral("RX"),     QStringLiteral("TX/RX"),
                                        QStringLiteral("CLRTX"),  QStringLiteral("CLRRX"),  QStringLiteral("LOG"),
                                        QStringLiteral("CLRQSO"), QStringLiteral("INCR"),   QStringLiteral("DECR")};
  QHash<QString, QString> none;
  QString out;
  int last = 0;
  auto it = braces.globalMatch(body);
  while (it.hasNext()) {
    const QRegularExpressionMatch m = it.next();
    const QString name = m.captured(1).toUpper();
    out += body.mid(last, m.capturedStart() - last);
    QHash<QString, QString> upper;
    for (auto t = context.tags.cbegin(); t != context.tags.cend(); ++t)
      upper.insert(bareTagName(t.key()), t.value());
    const bool known = actions.contains(name) || fillValue(name, QString(), context, upper).has_value() ||
                       fldigiTagNames().contains(name);
    out += known ? QStringLiteral("<%1%2>").arg(name, m.captured(2)) : m.captured(0);
    last = m.capturedEnd();
  }
  out += body.mid(last);
  return out;
}

}  // namespace

MacroPlan planMacro(const QString& body, const MacroContext& context) {
  MacroPlan plan;
  QHash<QString, QString> upperTags;
  for (auto it = context.tags.cbegin(); it != context.tags.cend(); ++it)
    upperTags.insert(bareTagName(it.key()), it.value());

  const QString source = bracesToTags(body, context);
  static const QRegularExpression tag(
      QStringLiteral("<(@?)([A-Za-z_#][A-Za-z0-9_/+]*)(?::([^>|]*))?(?:\\|([^>]*))?>"));
  QString text;
  int last = 0;
  auto it = tag.globalMatch(source);
  while (it.hasNext()) {
    const QRegularExpressionMatch m = it.next();
    text += source.mid(last, m.capturedStart() - last);
    last = m.capturedEnd();
    const QString name = m.captured(2).toUpper();
    const QString arg = m.captured(3).trimmed();
    const bool hasFallback = m.capturedStart(4) >= 0;
    const QString fallback = m.captured(4);
    if (name == QLatin1String("TX")) {
      plan.keys = true;
    } else if (name == QLatin1String("RX")) {
      plan.backToRx = true;
    } else if (name == QLatin1String("TX/RX")) {
      plan.toggle = true;
    } else if (name == QLatin1String("CLRTX")) {
      plan.clearTx = true;
    } else if (name == QLatin1String("CLRRX")) {
      plan.clearRx = true;
    } else if (name == QLatin1String("LOG") || name == QLatin1String("LNW")) {
      plan.log = true;
      if (!arg.isEmpty())
        plan.logNote = arg;
    } else if (name == QLatin1String("CLRQSO") || name == QLatin1String("CLRLOG")) {
      plan.clearQso = true;
    } else if (name == QLatin1String("INCR")) {
      ++plan.serialStep;
    } else if (name == QLatin1String("DECR")) {
      --plan.serialStep;
    } else if (name == QLatin1String("TIMER")) {
      bool ok = false;
      const int seconds = arg.toInt(&ok);
      if (ok && seconds > 0)
        plan.repeatSeconds = std::clamp(seconds, 5, 600);
    } else if (name == QLatin1String("COMMENT") || (name.startsWith(QLatin1Char('#')) && name.size() > 1)) {
      // A note for the reader, never sent.
    } else if (const auto value = fillValue(name, arg, context, upperTags)) {
      if (!value->isEmpty())
        text += *value;
      else if (hasFallback)
        text += fallback;
      else if (otherStationLabels().contains(name) && !plan.missing.contains(otherStationLabels().value(name)))
        plan.missing << otherStationLabels().value(name);
      else if (name.startsWith(QLatin1String("WX")) && !plan.missing.contains(QStringLiteral("Weather")))
        plan.missing << QStringLiteral("Weather");
    } else if (fldigiTagNames().contains(name)) {
      // A Fldigi tag STD does not run: drop it so it never goes out as text.
      if (!plan.skipped.contains(m.captured(0)))
        plan.skipped << m.captured(0);
    } else {
      text += m.captured(0);  // not a tag, e.g. "<< N8SDR >>" stays
    }
  }
  text += source.mid(last);

  // Spaces left where tags were come off the line ends. A leading or
  // trailing line break stays so a macro can start or end a new line.
  QStringList lines = text.split(QLatin1Char('\n'));
  for (QString& line : lines) {
    while (line.endsWith(QLatin1Char(' ')) || line.endsWith(QLatin1Char('\t')))
      line.chop(1);
  }
  text = lines.join(QLatin1Char('\n'));
  const bool leadBreak = [&] {
    for (const QChar c : text) {
      if (c == QLatin1Char('\n'))
        return true;
      if (!c.isSpace())
        return false;
    }
    return false;
  }();
  const bool tailBreak = text.trimmed().size() > 0 && text.endsWith(QLatin1Char('\n'));
  text = text.trimmed();
  text.replace(QRegularExpression(QStringLiteral("[ \\t]{2,}")), QStringLiteral(" "));
  if (!text.isEmpty()) {
    if (leadBreak)
      text.prepend(QLatin1Char('\n'));
    if (tailBreak)
      text.append(QLatin1Char('\n'));
  }
  plan.text = text;
  return plan;
}

QString macroButtonLabel(const QString& name, const MacroContext& context, bool keys) {
  QString label = name;
  // Fldigi draws @>| @|| @>> @> as arrow icons. STD marks a keying macro with ▶.
  label.remove(QRegularExpression(QStringLiteral("@[<>|]+")));
  if (label.contains(QLatin1Char('<')) || label.contains(QLatin1Char('{'))) {
    MacroContext shown = context;
    const MacroPlan plan = planMacro(label, shown);
    label = plan.text;
    if (!plan.missing.isEmpty())
      label += QStringLiteral(" ?");
  }
  label = label.simplified();
  if (label.isEmpty())
    label = name.simplified();
  return keys ? QStringLiteral("▶ ") + label : label;
}

QVector<MacroSlot> blankMacroSet() {
  QVector<MacroSlot> rows(kMacroCount);
  for (int i = 0; i < kMacroCount; ++i)
    rows[i].name = QStringLiteral("F-%1").arg(i + 1);
  return rows;
}

bool loadMacroDefinition(const QString& body, QVector<MacroSlot>* rows, QString* error) {
  if (!rows)
    return false;
  const QStringList lines = body.split(QLatin1Char('\n'));
  if (lines.isEmpty() || !lines.first().startsWith(QStringLiteral("//fldigi macro definition file"))) {
    if (error)
      *error = QStringLiteral("Not a Fldigi macro definition file.");
    return false;
  }
  const bool extended = lines.first().contains(QStringLiteral("extended"));
  *rows = blankMacroSet();
  int number = 0;
  for (int i = 1; i < lines.size(); ++i) {
    const QString line = lines.at(i);
    if (line.isEmpty() || line.startsWith(QStringLiteral("//")))
      continue;
    if (line.startsWith(QStringLiteral("/$"))) {
      static const QRegularExpression header(QStringLiteral("^/\\$\\s+(\\d+)\\s*(.*)$"));
      const QRegularExpressionMatch match = header.match(line);
      if (!match.hasMatch())
        continue;
      bool ok = false;
      int parsed = match.captured(1).toInt(&ok);
      if (!ok || parsed < 0 || parsed >= kMacroCount)
        break;
      if (!extended && parsed > 9)
        parsed += 2;
      if (parsed < 0 || parsed >= kMacroCount)
        break;
      number = parsed;
      (*rows)[number].name = match.captured(2);
      (*rows)[number].text.clear();
      continue;
    }
    (*rows)[number].text += unescapeLine(line);
  }
  return true;
}

QString saveMacroDefinition(const QVector<MacroSlot>& rows) {
  QString out;
  out += QStringLiteral(
      "//fldigi macro definition file extended\n"
      "// This file defines the macro structure(s) for the digital modem program, fldigi\n"
      "// It also serves as a basis for any macros that are written by the user\n"
      "//\n"
      "// The top line of this file should always be the first line in every macro \n"
      "// definition file (.mdf) for the fldigi program to recognize it as such.\n"
      "//\n");
  const int count = qMin(rows.size(), kMacroCount);
  for (int i = 0; i < count; ++i) {
    out += QStringLiteral("\n//\n// Macro # %1\n/$ %2 %3\n")
               .arg(i + 1)
               .arg(i)
               .arg(rows.at(i).name);
    QString work = rows.at(i).text;
    int pos = 0;
    while ((pos = work.indexOf(QLatin1Char('\n'), pos)) >= 0) {
      work.insert(pos, QStringLiteral("\\n"));
      pos += 3;
    }
    out += work;
    out += QLatin1Char('\n');
  }
  return out;
}

QString macroInsertText(const QString& body, const QString& call, const QString& locator,
                        const QHash<QString, QString>& userTags) {
  QString text = body;
  const QStringList drop = {QStringLiteral("TX"), QStringLiteral("RX"), QStringLiteral("CLRTX"),
                            QStringLiteral("CLRRX"), QStringLiteral("LOG"),   QStringLiteral("LNW"),
                            QStringLiteral("CLRQSO"), QStringLiteral("CLRLOG")};
  for (const QString& tag : drop) {
    text.replace(QStringLiteral("<%1>").arg(tag), QString(), Qt::CaseInsensitive);
    text.replace(QRegularExpression(QStringLiteral("<@?%1:[^>]*>").arg(tag),
                                    QRegularExpression::CaseInsensitiveOption),
                 QString());
  }
  text.replace(QStringLiteral("<MYCALL>"), call, Qt::CaseInsensitive);
  text.replace(QStringLiteral("<MYLOC>"), locator, Qt::CaseInsensitive);
  text.replace(QStringLiteral("<MYGRID>"), locator, Qt::CaseInsensitive);
  QStringList tags = userTags.keys();
  std::sort(tags.begin(), tags.end(), [](const QString& a, const QString& b) { return a.size() > b.size(); });
  for (const QString& tag : tags) {
    if (!tag.startsWith(QLatin1Char('<')))
      continue;
    text.replace(tag, userTags.value(tag), Qt::CaseInsensitive);
  }
  return text.trimmed();
}

QVector<MacroUserField> macroUserFields() {
  // Fldigi operator and log fields that the tag list names, except the
  // callsign and the locator. Those stay Options → User as they are.
  static const struct {
    const char* tag;
    const char* key;
    const char* objectName;
    const char* label;
    const char* note;
  } kFields[] = {
      {"<MYNAME>", "user/name", "userName", "Name", "Operator name"},
      {"<MYQTH>", "user/qth", "userQth", "QTH", "Station city"},
      {"<ANTENNA>", "user/antenna", "userAntenna", "Antenna", "Short antenna description"},
      {"<TX_PWR>", "user/power", "userPower", "Power", "Transmit power"},
      {"<MYRST>", "user/rst", "userRst", "RST", "My RST"},
      {"<MYCLASS>", "user/fdClass", "userFdClass", "FD class", "Field Day class"},
      {"<MYSECTION>", "user/fdSection", "userFdSection", "FD section", "Field Day section"},
      {"<MYSTATE>", "user/state", "userState", "State", "State or province"},
      {"<MYST>", "user/st", "userSt", "ST", "State abbreviation"},
      {"<MYCOUNTY>", "user/county", "userCounty", "County", "County"},
      {"<MYCNTY>", "user/cnty", "userCnty", "CNTY", "County abbreviation"},
  };
  QVector<MacroUserField> rows;
  rows.reserve(int(sizeof kFields / sizeof kFields[0]));
  for (const auto& field : kFields) {
    rows.push_back(MacroUserField{QString::fromLatin1(field.tag), QString::fromLatin1(field.key),
                                  QString::fromLatin1(field.objectName), QString::fromLatin1(field.label),
                                  QString::fromLatin1(field.note)});
  }
  return rows;
}

QVector<MacroTagRow> macroTagList() {
  // Fldigi 4.2.06 loadBrowser(), without the per-modem <MODEM:...> rows.
  static const struct {
    const char* tag;
    const char* note;
  } kTags[] = {
      {"<FREQ>", "my frequency"},
      {"<MODE>", "mode"},
      {"<MYCALL>", "my call"},
      {"<MYLOC>", "my locator"},
      {"<MYNAME>", "my name"},
      {"<MYQTH>", "my QTH"},
      {"<MYRST>", "my RST"},
      {"<MYCLASS>", "my FD class"},
      {"<MYSECTION>", "my FD section"},
      {"<MYSTATE>", "my state"},
      {"<MYST>", "my ST"},
      {"<MYCOUNTY>", "my county"},
      {"<MYCNTY>", "my CNTY"},
      {"<ANTENNA>", "my antenna"},
      {"<BAND>", "operating band"},
      {"<TX_PWR>", "transmit power"},
      {"<VER>", "Fldigi version"},
      {"<DIGI>", "digital mode (adif)"},
      {"", ""},
      {"<CALL>", "other call"},
      {"<NAME>", "other name"},
      {"<QTH>", "other QTH"},
      {"<ST>", "other State"},
      {"<PR>", "other Province"},
      {"<LOC>", "other locator"},
      {"<RST>", "other RST"},
      {"", ""},
      {"<INFO1>", "S/N etc."},
      {"<INFO2>", "IMD etc."},
      {"", ""},
      {"<QSONBR>", "# QSO recs"},
      {"<NXTNBR>", "next QSO rec #"},
      {"", ""},
      {"<MAPIT>", "map on google"},
      {"<MAPIT:adr/lat/loc>", "map by value"},
      {"", ""},
      {"<CLRRX>", "clear RX pane"},
      {"<CLRTX>", "clear TX pane"},
      {"<CLRQSO>", "clear QSO fields"},
      {"", ""},
      {"<GET>", "text to NAME/QTH"},
      {"", ""},
      {"<CLRLOG>", "clear log fields"},
      {"<LOG>", "save QSO data"},
      {"<LOG:msg>", "save QSO data, append msg to notes"},
      {"<LNW>", "log at xmt time"},
      {"<LNW:msg>", "save QSO data, append msg to notes"},
      {"<EQSL>", "log eQSL"},
      {"<EQSL:[msg]>", "log eQSL optional msg"},
      {"", ""},
      {"<QSOTIME>", "QSO time (HHMM)"},
      {"<WELCOME>", "nice to work you again, when in your log"},
      {"<LASTQSO>", "date of your last QSO with them"},
      {"<ILDT[:fmt]>", "LDT default '%Y-%m-%d %H:%M%z'"},
      {"<LDT[[fmt]>", "Local datetime, default '%x %H:%M %Z'"},
      {"<IZDT[:fmt]>", "ZDT default '%Y-%m-%d %H:%MZ'"},
      {"<ZDT[:fmt]>", "UTC datetime, default '%x %H:%MZ'"},
      {"<LT[:fmt]>", "local time, default %H%M"},
      {"<ZT[:fmt]>", "zulu time default %H%MZ"},
      {"<LD[:fmt]>", "local date, default '%Y-%M-%D'"},
      {"<ZD[:fmt]>", "zulu date, default '%Y-%M-%D Z'"},
      {"<WX>", "local weather, Options → Weather"},
      {"<WX:xxxx>", "get weather data for station"},
      {"<WX_TEMP>", "temperature, Options → Weather"},
      {"<WX_COND>", "sky, such as partly cloudy"},
      {"<WX_WIND>", "wind, such as SW 8 mph"},
      {"<WX_HUM>", "humidity, such as 72%"},
      {"", ""},
      {"<CNTR>", "contest counter"},
      {"<DECR>", "decrement counter"},
      {"<INCR>", "increment counter"},
      {"<XIN>", "exchange in"},
      {"<XOUT>", "exchange out"},
      {"<XBEG>", "exchange begin"},
      {"<XEND>", "exchange end"},
      {"<SAVEXCHG>", "save contest out"},
      {"<SERNO>", "current contest serno"},
      {"<LASTNO>", "last serno sent"},
      {"<FDCLASS>", "FD class"},
      {"<FDSECT>", "FD section"},
      {"<CLASS>", "contest class"},
      {"<SECTION>", "ARRL section"},
      {"", ""},
      {"<RX>", "receive"},
      {"<TX>", "transmit"},
      {"<TX/RX>", "toggle T/R"},
      {"<SRCHUP>", "search UP for signal"},
      {"<SRCHDN>", "search DOWN for signal"},
      {"<GOHOME>", "return to sweet spot"},
      {"<GOFREQ:NNNN>", "move to freq NNNN Hz"},
      {"<QSYTO>", "left-clk QSY button"},
      {"<QSYFM>", "right-clk QSY button"},
      {"<QSY:FFF.F[:NNNN]>", "qsy to kHz, Hz"},
      {"<QSY+:+/-n.nnn>", "incr/decr xcvr freq"},
      {"<RIGMODE:mode>", "valid xcvr mode"},
      {"<FILWID:width>", "valid xcvr filter width"},
      {"<RIGLO:lowcut>", "valid xcvr low cutoff filter"},
      {"<RIGHI:hicut>", "valid xcvr hi cutoff filter"},
      {"<FOCUS>", "rig freq has kbd focus"},
      {"", ""},
      {"<QRG:text>", "insert QRG into Rx text"},
      {"", ""},
      {"<FILE:>", "insert text file"},
      {"<IMAGE:>", "insert image file"},
      {"<AVATAR>", "send avatar"},
      {"", ""},
      {"<PAUSE>", "pause transmit"},
      {"<IDLE:NN.nn>", "idle signal for NN.nn sec"},
      {"<TIMER:NN>", "repeat every NN sec"},
      {"<AFTER:NN>", "repeat after waiting NN sec"},
      {"<TUNE:NN>", "tune signal for NN sec"},
      {"<WAIT:NN.n>", "delay xmt for NN.n sec"},
      {"<REPEAT>", "repeat macro continuously"},
      {"<SKED:hhmm[ss][:YYYYMMDD]>", "schedule execution for"},
      {"<UNTIL:hhmm[ss][:YYYYMMDD]>", "end execution at"},
      {"<LOCAL>", "use local date/time"},
      {"", ""},
      {"<TXATTEN:nn.n>", "set xmt attenuator"},
      {"", ""},
      {"<CWID>", "CW identifier"},
      {"<ID>", "send mode ID; TX start only"},
      {"<TEXT>", "text at start of TX"},
      {"<VIDEO:>", "video text in TX stream"},
      {"<TXRSID:on|off|t>", "Tx RSID on,off,toggle"},
      {"<RXRSID:on|off|t>", "Rx RSID on,off,toggle"},
      {"<NRSID:NN>", "Transmit |NN| successive RsID bursts"},
      {"<DTMF:[Wn:][Ln:]chrs>", "[Wait][Len](ms)"},
      {"", ""},
      {"<AUDIO:>", "Xmt audio wav file"},
      {"", ""},
      {"<ALERT:[bark][checkout][doesnot][phone][beeboo][diesel][steam_train][dinner_bell][standard_tone]>",
       "alert sound"},
      {"<ALERT:>", "alert using external wav file"},
      {"", ""},
      {"<POST:+/-nn.n>", "CW QSK post-timing"},
      {"<PRE:nn.n>", "CW QSK pre-timing"},
      {"<RISE:nn.n>", "CW rise time"},
      {"<WPM:NN.nn:FF.nn>", "Char WPM:Text WPM (15.0:5.0)"},
      {"", ""},
      {"<RIGCAT:[\"text\"][hex ...]:ret>", "send CAT cmd"},
      {"<FLRIG:[\"text\"][hex ...]>", "send CAT cmd"},
      {"", ""},
      {"<AFC:on|off|t>", "AFC on,off,toggle"},
      {"<LOCK:on|off|t>", "LOCK on,off,toggle"},
      {"<REV:on|off|t>", "Rev on,off,toggle"},
      {"", ""},
      {"<MACROS:>", "change macro defs file"},
      {"<SAVE>", "save current macro file"},
      {"<BUFFERED>", "run macro from buffered text"},
      {"", ""},
      {"<COMMENT:comment text>", "ignore comment text"},
      {"<#comments>", "ignore comments"},
      {"", ""},
      {"<CPS_TEST:nn>", "modem char/sec test on nn chars"},
      {"<CPS_N:n>", "modem timing test, n random 5 char groups"},
      {"<CPS_FILE:>", "modem timing test, spec file"},
      {"<CPS_STRING:s>", "modem timing test, string s"},
      {"", ""},
      {"<WAV_TEST>", "WAV file; internal string"},
      {"<WAV_N:n>", "WAV file; n random 5 char groups"},
      {"<WAV_FILE:>", "WAV file; spec file"},
      {"<WAV_STRING:s>", "WAV file; string s"},
      {"", ""},
      {"<CSV:on|off|t>", "Analysis CSV on,off,toggle"},
      {"", ""},
      {"<PUSH>", "push current mode to stack"},
      {"<PUSH:m|f>", "push current mode / audio freq to stack"},
      {"<POP>", "pop current mode/freq from stack"},
  };
  QVector<MacroTagRow> rows;
  rows.reserve(int(sizeof kTags / sizeof kTags[0]));
  for (const auto& tag : kTags)
    rows.push_back(MacroTagRow{QString::fromLatin1(tag.tag), QString::fromLatin1(tag.note)});
  return rows;
}

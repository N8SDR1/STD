// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "MacroFile.h"

#include <algorithm>

#include <QRegularExpression>
#include <QStringList>

namespace {

QString unescapeLine(QString line) {
  if (line.endsWith(QStringLiteral("\\n"))) {
    line.chop(2);
    line.append(QLatin1Char('\n'));
  }
  return line;
}

}  // namespace

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
                            QStringLiteral("CLRRX"), QStringLiteral("LOG")};
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
      {"<ILDT[:fmt]>", "LDT default '%Y-%m-%d %H:%M%z'"},
      {"<LDT[[fmt]>", "Local datetime, default '%x %H:%M %Z'"},
      {"<IZDT[:fmt]>", "ZDT default '%Y-%m-%d %H:%MZ'"},
      {"<ZDT[:fmt]>", "UTC datetime, default '%x %H:%MZ'"},
      {"<LT[:fmt]>", "local time, default %H%M"},
      {"<ZT[:fmt]>", "zulu time default %H%MZ"},
      {"<LD[:fmt]>", "local date, default '%Y-%M-%D'"},
      {"<ZD[:fmt]>", "zulu date, default '%Y-%M-%D Z'"},
      {"<WX>", "get weather data"},
      {"<WX:xxxx>", "get weather data for station"},
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

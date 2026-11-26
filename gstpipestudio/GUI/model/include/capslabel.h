// capslabel.h – how caps are written on a link: "mediatype=video/x-raw · width=640 · …". The link draws it, the
// layout measures it to leave room for it.
#pragma once

#include <QFont>
#include <QString>
#include <QStringList>

namespace capslabel {

QStringList fields(const QString& caps);  // "mediatype=video/x-raw", "width=640" ...; the first structure, types dropped
QString singleLine(const QString& caps);  // the fields joined, at most 60 characters
QString multiLine(const QString& caps);   // at most five fields, one per line
QFont font(QFont base = QFont());         // the label's font

}  // namespace capslabel

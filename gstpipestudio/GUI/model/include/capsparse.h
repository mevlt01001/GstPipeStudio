// capsparse.h – the inverse of CapsChoice::toText, for the caps texts the caps page writes.
#pragma once

#include <QString>

#include "capstext.h"

gps::CapsChoice parseCapsChoice(const QString& text);

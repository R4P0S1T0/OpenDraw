#pragma once

#include <QString>

namespace opendraw::language {

// Interface language: "" follows the system, otherwise a locale name such as "pt_BR" or "en".
QString stored();
void store(const QString &name);

// Loads OpenDraw's translation and Qt's own (standard buttons and dialogs) for the stored
// language. Call once, after the QApplication exists and before any window is created.
void install();

// The languages OpenDraw is translated into, as locale names (English is the source language).
QStringList available();

} // namespace opendraw::language

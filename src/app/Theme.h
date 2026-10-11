#pragma once

class QApplication;

namespace opendraw {

// Colour scheme of the whole application. The same look on every platform: Qt's Fusion
// style with OpenDraw's palettes.
enum class ColorScheme { Dark, Light };

namespace theme {

ColorScheme stored();
void apply(ColorScheme scheme);
// Applies and remembers the choice.
void set(ColorScheme scheme);

} // namespace theme
} // namespace opendraw

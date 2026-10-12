#pragma once

#include "core/Document.h"

namespace opendraw {

// Options of the drawing tools, edited on the property bar while the tool is active.
struct ToolSettings {
    int polygonSides = 5;
    int starPoints = 5;
    double starSharpness = 53.0; // 0–100
    double spiralRevolutions = 4.0;
    int graphColumns = 4;
    int graphRows = 3;
    double cornerRadius = 0.0; // new rectangles, document units
    int blendSteps = 20;       // new blends
    QColor smartFillColor = QColor(0x4a, 0x90, 0xd9); // areas made by the smart fill tool
    bool smartFillOutline = true;                       // ... with the default outline
    bool operator==(const ToolSettings &) const = default;
};

// What the drawing snaps to while objects are moved, drawn or resized.
struct SnapSettings {
    bool grid = false;
    bool guides = true;
    bool objects = false;
    bool page = false;
    bool operator==(const SnapSettings &) const = default;
};

// Grid shown over the drawing (View › Grid).
struct GridSettings {
    bool visible = false;
    double spacing = 5.0 * kUnitsPerMm;
};

} // namespace opendraw

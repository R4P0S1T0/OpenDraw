#pragma once

#include <QIcon>

namespace opendraw {

// OpenDraw's own icons, drawn in code on a 24 × 24 grid so they stay sharp at any size and
// follow the colour scheme (no image assets, no third-party artwork).
enum class Icon {
    New, Open, Save, Cut, Copy, Paste, Undo, Redo, Import, Export, ExportPdf, Rulers,
    Pick, Shape, Freehand, Bezier, Zoom, Pan, Rectangle, Ellipse, Text,
    FillBucket, OutlinePen, Portrait, Landscape,
    FirstPage, PreviousPage, NextPage, LastPage, AddPage,
    LockRatio, MirrorHorizontal, MirrorVertical,
    AlignLeft, AlignCenter, AlignRight, AlignJustify, Blend, Contour, Envelope, Extrude, SmartFill, InteractiveFill, Mesh,
    ScrollUp, ScrollDown, ScrollLeft, ScrollRight,
    Line, Polygon, Star, Spiral, GraphPaper, Eyedropper, Grid, Guidelines, Snap, CornerRadius,
    Rotate, RotateCounterclockwise,
    Eye, EyeOff, Lock, Unlock, Layer, Page, Group, Bitmap,
    FillNone, FillUniform, FillLinear, FillRadial, Transparency,
    CapButt, CapRound, CapSquare, JoinMiter, JoinRound, JoinBevel, Shadow,
};

QIcon icon(Icon id);

} // namespace opendraw

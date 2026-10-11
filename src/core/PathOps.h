#pragma once

#include "core/Shape.h"

#include <QPainterPath>
#include <QString>
#include <QTransform>

#include <optional>
#include <vector>

// Geometry operations on PathData. Pure functions, no GUI dependencies.
namespace opendraw::pathops {

QPainterPath toPainterPath(const PathData &path);
// Converts any QPainterPath (for example a rectangle or ellipse) into editable nodes.
PathData fromPainterPath(const QPainterPath &path);

void transform(PathData &path, const QTransform &t);

// SVG path data ("M 0 0 C ..."). Parsing understands every SVG path command, including
// relative forms, quadratic curves and elliptical arcs (converted to cubic curves).
QString toSvg(const PathData &path);
std::optional<PathData> fromSvg(QStringView d);

// Segment `segment` of a subpath runs from node `segment` to the next node (wrapping when closed).
struct SegmentHit {
    int subpath = -1;
    int segment = -1;
    double t = 0.0;      // position along the segment, 0..1
    QPointF point;       // closest point on the segment
    double distance = 0.0;
};
std::optional<SegmentHit> nearestSegment(const PathData &path, const QPointF &point, double maxDistance);

// Splits a segment at `t` without changing the shape of the path. Returns the new node's index.
int insertNode(PathData &path, int subpath, int segment, double t);

// Removes a node. Subpaths left with fewer than two nodes are removed too.
void removeNode(PathData &path, int subpath, int node);

// Smooth or corner, judged from whether the handles line up.
NodeType inferNodeType(const PathNode &node);

// Node commands. Each returns false and leaves the path untouched when it does not apply.

// Smooth lines the handles up (creating them from the neighbouring nodes if missing);
// corner just frees them.
bool setNodeType(PathData &path, int subpath, int node, NodeType type);

// The segment that starts at `node`, or ends at it when `node` is the last node of an open subpath.
std::optional<int> segmentAtNode(const PathData &path, int subpath, int node);
bool segmentToLine(PathData &path, int subpath, int segment);
// Gives a straight segment handles at a third of its length, so it can be bent.
bool segmentToCurve(PathData &path, int subpath, int segment);

// Opens a closed subpath at the node, or splits an open one in two there.
bool breakAtNode(PathData &path, int subpath, int node);

// Joins two end nodes of open subpaths: closes the subpath when both belong to it, otherwise
// connects the two subpaths into one. Nodes at the same position are merged into one node.
bool joinNodes(PathData &path, int subpathA, int nodeA, int subpathB, int nodeB);

// Turns a freehand stroke into a few smooth Bézier segments.
// `tolerance` is the largest distance, in document units, the result may drift from the stroke.
SubPath fitFreehand(const std::vector<QPointF> &points, double tolerance, bool closed);

// Point on the cubic segment at t.
QPointF pointAt(const PathNode &from, const PathNode &to, double t);

} // namespace opendraw::pathops

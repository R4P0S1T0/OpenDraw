#include "core/PathOps.h"

#include <QStringList>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

namespace opendraw::pathops {

namespace {

bool samePoint(const QPointF &a, const QPointF &b, double epsilon = 1e-6)
{
    return std::abs(a.x() - b.x()) <= epsilon && std::abs(a.y() - b.y()) <= epsilon;
}

QPointF lerp(const QPointF &a, const QPointF &b, double t)
{
    return a + (b - a) * t;
}

double length(const QPointF &v)
{
    return std::hypot(v.x(), v.y());
}

bool isStraight(const PathNode &from, const PathNode &to)
{
    return samePoint(from.out, from.point) && samePoint(to.in, to.point);
}

const PathNode &segmentEnd(const SubPath &subpath, int segment)
{
    return subpath.nodes[(segment + 1) % subpath.nodes.size()];
}

double distanceToSegment(const QPointF &p, const QPointF &a, const QPointF &b)
{
    const QPointF ab = b - a;
    const double lengthSquared = QPointF::dotProduct(ab, ab);
    if (lengthSquared <= 0.0)
        return length(p - a);
    const double t = std::clamp(QPointF::dotProduct(p - a, ab) / lengthSquared, 0.0, 1.0);
    return length(p - (a + ab * t));
}

// Ramer–Douglas–Peucker: keeps only the points needed to stay within `epsilon` of the stroke.
std::vector<QPointF> simplify(const std::vector<QPointF> &points, double epsilon)
{
    const int n = static_cast<int>(points.size());
    if (n < 3)
        return points;
    std::vector<bool> keep(n, false);
    keep.front() = keep.back() = true;
    std::vector<std::pair<int, int>> ranges{{0, n - 1}};
    while (!ranges.empty()) {
        const auto [start, end] = ranges.back();
        ranges.pop_back();
        double farthest = 0.0;
        int index = -1;
        for (int i = start + 1; i < end; ++i) {
            const double d = distanceToSegment(points[i], points[start], points[end]);
            if (d > farthest) {
                farthest = d;
                index = i;
            }
        }
        if (index >= 0 && farthest > epsilon) {
            keep[index] = true;
            ranges.emplace_back(start, index);
            ranges.emplace_back(index, end);
        }
    }
    std::vector<QPointF> result;
    for (int i = 0; i < n; ++i) {
        if (keep[i])
            result.push_back(points[i]);
    }
    return result;
}

QString number(double value)
{
    if (std::abs(value) < 1e-9)
        value = 0.0;
    return QString::number(value, 'g', 10);
}

QString point(const QPointF &p)
{
    return number(p.x()) + QLatin1Char(' ') + number(p.y());
}

double angleBetween(const QPointF &u, const QPointF &v)
{
    return std::atan2(u.x() * v.y() - u.y() * v.x(), u.x() * v.x() + u.y() * v.y());
}

// Reads SVG path data. Stops at the first malformed token, keeping what was read so far,
// which is how browsers render broken path data.
class SvgPathParser {
public:
    explicit SvgPathParser(QStringView d)
        : m_d(d)
    {
    }

    std::optional<PathData> parse()
    {
        bool ok = true;
        QChar command;
        QChar previous;
        while (ok) {
            skipSeparators();
            if (m_pos >= m_d.size())
                break;
            const QChar c = m_d[m_pos];
            if (c.isLetter()) {
                command = c;
                ++m_pos;
            } else if (command.isNull() || command.toUpper() == u'Z') {
                ok = false;
                break;
            } else if (command == u'M') {
                command = u'L'; // extra coordinates after a moveto are linetos
            } else if (command == u'm') {
                command = u'l';
            }
            ok = execute(command, previous);
            previous = command.toUpper();
        }

        for (SubPath &sp : m_result) {
            for (PathNode &node : sp.nodes)
                node.type = inferNodeType(node);
        }
        std::erase_if(m_result, [](const SubPath &sp) { return sp.nodes.size() < 2; });
        if (!ok && m_result.empty())
            return std::nullopt;
        return m_result;
    }

private:
    bool execute(QChar command, QChar previous)
    {
        const bool relative = command.isLower();
        switch (command.toUpper().unicode()) {
        case 'M': {
            const auto p = readPoint(relative);
            if (!p)
                return false;
            m_result.push_back({});
            m_result.back().nodes.push_back(PathNode::corner(*p));
            m_current = m_start = *p;
            m_needSubpath = false;
            return true;
        }
        case 'L': {
            const auto p = readPoint(relative);
            if (!p)
                return false;
            lineTo(*p);
            return true;
        }
        case 'H': {
            const auto x = readNumber();
            if (!x)
                return false;
            lineTo({relative ? m_current.x() + *x : *x, m_current.y()});
            return true;
        }
        case 'V': {
            const auto y = readNumber();
            if (!y)
                return false;
            lineTo({m_current.x(), relative ? m_current.y() + *y : *y});
            return true;
        }
        case 'C': {
            const auto c1 = readPoint(relative);
            const auto c2 = c1 ? readPoint(relative) : std::nullopt;
            const auto p = c2 ? readPoint(relative) : std::nullopt;
            if (!p)
                return false;
            cubicTo(*c1, *c2, *p);
            return true;
        }
        case 'S': {
            const auto c2 = readPoint(relative);
            const auto p = c2 ? readPoint(relative) : std::nullopt;
            if (!p)
                return false;
            const bool reflect = previous == u'C' || previous == u'S';
            cubicTo(reflect ? 2 * m_current - m_lastCubicControl : m_current, *c2, *p);
            return true;
        }
        case 'Q': {
            const auto q = readPoint(relative);
            const auto p = q ? readPoint(relative) : std::nullopt;
            if (!p)
                return false;
            quadTo(*q, *p);
            return true;
        }
        case 'T': {
            const auto p = readPoint(relative);
            if (!p)
                return false;
            const bool reflect = previous == u'Q' || previous == u'T';
            quadTo(reflect ? 2 * m_current - m_lastQuadControl : m_current, *p);
            return true;
        }
        case 'A': {
            const auto rx = readNumber();
            const auto ry = rx ? readNumber() : std::nullopt;
            const auto rotation = ry ? readNumber() : std::nullopt;
            const auto largeArc = rotation ? readFlag() : std::nullopt;
            const auto sweep = largeArc ? readFlag() : std::nullopt;
            const auto p = sweep ? readPoint(relative) : std::nullopt;
            if (!p)
                return false;
            arcTo(*rx, *ry, *rotation, *largeArc, *sweep, *p);
            return true;
        }
        case 'Z':
            close();
            return true;
        default:
            return false;
        }
    }

    void ensureSubpath()
    {
        if (m_result.empty() || m_needSubpath) {
            m_result.push_back({});
            m_result.back().nodes.push_back(PathNode::corner(m_current));
            m_start = m_current;
            m_needSubpath = false;
        }
    }

    void lineTo(const QPointF &p)
    {
        ensureSubpath();
        m_result.back().nodes.push_back(PathNode::corner(p));
        m_current = p;
    }

    void cubicTo(const QPointF &c1, const QPointF &c2, const QPointF &p)
    {
        ensureSubpath();
        m_result.back().nodes.back().out = c1;
        m_result.back().nodes.push_back({p, c2, p, NodeType::Corner});
        m_current = p;
        m_lastCubicControl = c2;
    }

    void quadTo(const QPointF &q, const QPointF &p)
    {
        // A quadratic curve is exactly a cubic with controls 2/3 of the way to q.
        const QPointF from = m_current;
        cubicTo(from + (q - from) * (2.0 / 3.0), p + (q - p) * (2.0 / 3.0), p);
        m_lastQuadControl = q;
    }

    // Elliptical arc, following the SVG implementation notes (endpoint to centre
    // parameterisation), split into pieces of at most 90 degrees.
    void arcTo(double rx, double ry, double rotationDegrees, bool largeArc, bool sweep, const QPointF &p)
    {
        const QPointF from = m_current;
        rx = std::abs(rx);
        ry = std::abs(ry);
        if (samePoint(from, p, 1e-12))
            return;
        if (rx == 0.0 || ry == 0.0) {
            lineTo(p);
            return;
        }
        const double phi = rotationDegrees * std::numbers::pi / 180.0;
        const double cosPhi = std::cos(phi);
        const double sinPhi = std::sin(phi);

        const double dx = (from.x() - p.x()) / 2.0;
        const double dy = (from.y() - p.y()) / 2.0;
        const double x1 = cosPhi * dx + sinPhi * dy;
        const double y1 = -sinPhi * dx + cosPhi * dy;

        const double lambda = (x1 * x1) / (rx * rx) + (y1 * y1) / (ry * ry);
        if (lambda > 1.0) {
            rx *= std::sqrt(lambda);
            ry *= std::sqrt(lambda);
        }
        const double numerator = rx * rx * ry * ry - rx * rx * y1 * y1 - ry * ry * x1 * x1;
        const double denominator = rx * rx * y1 * y1 + ry * ry * x1 * x1;
        double coefficient = std::sqrt(std::max(0.0, numerator / denominator));
        if (largeArc == sweep)
            coefficient = -coefficient;
        const double cxPrime = coefficient * (rx * y1 / ry);
        const double cyPrime = coefficient * (-ry * x1 / rx);
        const double cx = cosPhi * cxPrime - sinPhi * cyPrime + (from.x() + p.x()) / 2.0;
        const double cy = sinPhi * cxPrime + cosPhi * cyPrime + (from.y() + p.y()) / 2.0;

        const QPointF u((x1 - cxPrime) / rx, (y1 - cyPrime) / ry);
        const QPointF v((-x1 - cxPrime) / rx, (-y1 - cyPrime) / ry);
        const double theta = angleBetween({1.0, 0.0}, u);
        double delta = angleBetween(u, v);
        if (!sweep && delta > 0.0)
            delta -= 2.0 * std::numbers::pi;
        else if (sweep && delta < 0.0)
            delta += 2.0 * std::numbers::pi;

        const int pieces = std::max(1, static_cast<int>(std::ceil(std::abs(delta) / (std::numbers::pi / 2.0) - 1e-9)));
        const double step = delta / pieces;
        const double k = 4.0 / 3.0 * std::tan(step / 4.0);
        auto map = [&](double x, double y) {
            return QPointF(cx + rx * cosPhi * x - ry * sinPhi * y, cy + rx * sinPhi * x + ry * cosPhi * y);
        };
        for (int i = 0; i < pieces; ++i) {
            const double a1 = theta + i * step;
            const double a2 = a1 + step;
            const QPointF c1 = map(std::cos(a1) - k * std::sin(a1), std::sin(a1) + k * std::cos(a1));
            const QPointF c2 = map(std::cos(a2) + k * std::sin(a2), std::sin(a2) - k * std::cos(a2));
            const QPointF end = i == pieces - 1 ? p : map(std::cos(a2), std::sin(a2));
            cubicTo(c1, c2, end);
        }
    }

    void close()
    {
        if (!m_result.empty() && !m_needSubpath) {
            SubPath &sp = m_result.back();
            if (sp.nodes.size() > 1 && samePoint(sp.nodes.front().point, sp.nodes.back().point)) {
                sp.nodes.front().in = sp.nodes.back().in;
                sp.nodes.pop_back();
            }
            sp.closed = true;
        }
        m_current = m_start;
        m_needSubpath = true;
    }

    void skipSeparators()
    {
        while (m_pos < m_d.size() && (m_d[m_pos].isSpace() || m_d[m_pos] == u','))
            ++m_pos;
    }

    std::optional<double> readNumber()
    {
        skipSeparators();
        const qsizetype start = m_pos;
        auto digits = [this] {
            const qsizetype before = m_pos;
            while (m_pos < m_d.size() && m_d[m_pos].isDigit())
                ++m_pos;
            return m_pos > before;
        };
        if (m_pos < m_d.size() && (m_d[m_pos] == u'+' || m_d[m_pos] == u'-'))
            ++m_pos;
        bool any = digits();
        if (m_pos < m_d.size() && m_d[m_pos] == u'.') {
            ++m_pos;
            any = digits() || any;
        }
        if (!any) {
            m_pos = start;
            return std::nullopt;
        }
        if (m_pos < m_d.size() && (m_d[m_pos] == u'e' || m_d[m_pos] == u'E')) {
            const qsizetype exponentStart = m_pos;
            ++m_pos;
            if (m_pos < m_d.size() && (m_d[m_pos] == u'+' || m_d[m_pos] == u'-'))
                ++m_pos;
            if (!digits())
                m_pos = exponentStart;
        }
        bool ok = false;
        const double value = m_d.mid(start, m_pos - start).toDouble(&ok);
        if (!ok)
            return std::nullopt;
        return value;
    }

    // Arc flags are a single 0 or 1 and may be written without separators ("a1 1 0 01 10 10").
    std::optional<bool> readFlag()
    {
        skipSeparators();
        if (m_pos < m_d.size() && (m_d[m_pos] == u'0' || m_d[m_pos] == u'1'))
            return m_d[m_pos++] == u'1';
        return std::nullopt;
    }

    std::optional<QPointF> readPoint(bool relative)
    {
        const auto x = readNumber();
        const auto y = x ? readNumber() : std::nullopt;
        if (!y)
            return std::nullopt;
        const QPointF p(*x, *y);
        return relative ? m_current + p : p;
    }

    QStringView m_d;
    qsizetype m_pos = 0;
    PathData m_result;
    QPointF m_current;
    QPointF m_start;
    QPointF m_lastCubicControl;
    QPointF m_lastQuadControl;
    bool m_needSubpath = true;
};

} // namespace

QPointF pointAt(const PathNode &from, const PathNode &to, double t)
{
    // Straight segments are parameterised linearly, matching how insertNode splits them.
    if (isStraight(from, to))
        return lerp(from.point, to.point, t);
    const double mt = 1.0 - t;
    return from.point * (mt * mt * mt) + from.out * (3 * mt * mt * t) + to.in * (3 * mt * t * t)
           + to.point * (t * t * t);
}

QPainterPath toPainterPath(const PathData &path)
{
    QPainterPath out;
    for (const SubPath &sp : path) {
        if (sp.nodes.empty())
            continue;
        out.moveTo(sp.nodes.front().point);
        for (int s = 0; s < sp.segmentCount(); ++s) {
            const PathNode &a = sp.nodes[s];
            const PathNode &b = segmentEnd(sp, s);
            if (isStraight(a, b))
                out.lineTo(b.point);
            else
                out.cubicTo(a.out, b.in, b.point);
        }
        if (sp.closed)
            out.closeSubpath();
    }
    return out;
}

PathData fromPainterPath(const QPainterPath &path)
{
    PathData result;
    auto finishSubpath = [&result] {
        if (result.empty())
            return;
        SubPath &sp = result.back();
        if (sp.nodes.size() > 2 && samePoint(sp.nodes.front().point, sp.nodes.back().point)) {
            sp.nodes.front().in = sp.nodes.back().in;
            sp.nodes.pop_back();
            sp.closed = true;
        }
        for (PathNode &node : sp.nodes)
            node.type = inferNodeType(node);
    };

    for (int i = 0; i < path.elementCount(); ++i) {
        const QPainterPath::Element e = path.elementAt(i);
        switch (e.type) {
        case QPainterPath::MoveToElement:
            finishSubpath();
            result.push_back({});
            result.back().nodes.push_back(PathNode::corner(e));
            break;
        case QPainterPath::LineToElement:
            if (result.empty())
                result.push_back({{PathNode::corner({})}});
            result.back().nodes.push_back(PathNode::corner(e));
            break;
        case QPainterPath::CurveToElement: {
            if (i + 2 >= path.elementCount())
                break;
            const QPointF c2 = path.elementAt(i + 1);
            const QPointF end = path.elementAt(i + 2);
            if (result.empty())
                result.push_back({{PathNode::corner({})}});
            result.back().nodes.back().out = e;
            result.back().nodes.push_back({end, c2, end, NodeType::Corner});
            i += 2;
            break;
        }
        case QPainterPath::CurveToDataElement:
            break;
        }
    }
    finishSubpath();
    std::erase_if(result, [](const SubPath &sp) { return sp.nodes.size() < 2; });
    return result;
}

void transform(PathData &path, const QTransform &t)
{
    for (SubPath &sp : path) {
        for (PathNode &node : sp.nodes) {
            node.point = t.map(node.point);
            node.in = t.map(node.in);
            node.out = t.map(node.out);
        }
    }
}

QString toSvg(const PathData &path)
{
    QStringList parts;
    for (const SubPath &sp : path) {
        if (sp.nodes.empty())
            continue;
        parts << QStringLiteral("M ") + point(sp.nodes.front().point);
        const int n = static_cast<int>(sp.nodes.size());
        for (int s = 0; s < sp.segmentCount(); ++s) {
            const PathNode &a = sp.nodes[s];
            const PathNode &b = segmentEnd(sp, s);
            const bool closingSegment = sp.closed && s == n - 1;
            if (!isStraight(a, b))
                parts << QStringLiteral("C ") + point(a.out) + QLatin1Char(' ') + point(b.in) + QLatin1Char(' ')
                             + point(b.point);
            else if (!closingSegment)
                parts << QStringLiteral("L ") + point(b.point);
        }
        if (sp.closed)
            parts << QStringLiteral("Z");
    }
    return parts.join(QLatin1Char(' '));
}

std::optional<PathData> fromSvg(QStringView d)
{
    return SvgPathParser(d).parse();
}

std::optional<SegmentHit> nearestSegment(const PathData &path, const QPointF &point, double maxDistance)
{
    std::optional<SegmentHit> best;
    constexpr int kSamples = 32;
    for (int si = 0; si < static_cast<int>(path.size()); ++si) {
        const SubPath &sp = path[si];
        for (int s = 0; s < sp.segmentCount(); ++s) {
            const PathNode &a = sp.nodes[s];
            const PathNode &b = segmentEnd(sp, s);
            auto distanceAt = [&](double t) { return length(pointAt(a, b, t) - point); };

            if (isStraight(a, b)) {
                const QPointF ab = b.point - a.point;
                const double lengthSquared = QPointF::dotProduct(ab, ab);
                const double t = lengthSquared > 0.0
                                     ? std::clamp(QPointF::dotProduct(point - a.point, ab) / lengthSquared, 0.0, 1.0)
                                     : 0.0;
                const double d = distanceAt(t);
                if (d <= maxDistance && (!best || d < best->distance))
                    best = SegmentHit{si, s, t, pointAt(a, b, t), d};
                continue;
            }

            double bestT = 0.0;
            double bestD = distanceAt(0.0);
            for (int i = 1; i <= kSamples; ++i) {
                const double t = double(i) / kSamples;
                const double d = distanceAt(t);
                if (d < bestD) {
                    bestD = d;
                    bestT = t;
                }
            }
            // Refine around the best sample.
            double lo = std::max(0.0, bestT - 1.0 / kSamples);
            double hi = std::min(1.0, bestT + 1.0 / kSamples);
            for (int i = 0; i < 30; ++i) {
                const double m1 = lo + (hi - lo) / 3.0;
                const double m2 = hi - (hi - lo) / 3.0;
                if (distanceAt(m1) < distanceAt(m2))
                    hi = m2;
                else
                    lo = m1;
            }
            const double t = (lo + hi) / 2.0;
            const double d = distanceAt(t);
            if (d <= maxDistance && (!best || d < best->distance))
                best = SegmentHit{si, s, t, pointAt(a, b, t), d};
        }
    }
    return best;
}

int insertNode(PathData &path, int subpath, int segment, double t)
{
    SubPath &sp = path[subpath];
    const int n = static_cast<int>(sp.nodes.size());
    PathNode &a = sp.nodes[segment];
    PathNode &b = sp.nodes[(segment + 1) % n];

    PathNode node;
    if (isStraight(a, b)) {
        node = PathNode::corner(lerp(a.point, b.point, t));
    } else {
        // de Casteljau subdivision keeps the curve's shape exactly.
        const QPointF p01 = lerp(a.point, a.out, t);
        const QPointF p12 = lerp(a.out, b.in, t);
        const QPointF p23 = lerp(b.in, b.point, t);
        const QPointF p012 = lerp(p01, p12, t);
        const QPointF p123 = lerp(p12, p23, t);
        node = {lerp(p012, p123, t), p012, p123, NodeType::Smooth};
        a.out = p01;
        b.in = p23;
    }
    sp.nodes.insert(sp.nodes.begin() + segment + 1, node);
    return segment + 1;
}

void removeNode(PathData &path, int subpath, int node)
{
    SubPath &sp = path[subpath];
    sp.nodes.erase(sp.nodes.begin() + node);
    if (sp.nodes.size() < 2)
        path.erase(path.begin() + subpath);
}

NodeType inferNodeType(const PathNode &node)
{
    const QPointF in = node.point - node.in;
    const QPointF out = node.out - node.point;
    const double lin = length(in);
    const double lout = length(out);
    if (lin < 1e-9 || lout < 1e-9)
        return NodeType::Corner;
    const double cross = (in.x() * out.y() - in.y() * out.x()) / (lin * lout);
    const double dot = QPointF::dotProduct(in, out);
    return std::abs(cross) < 0.01 && dot > 0.0 ? NodeType::Smooth : NodeType::Corner;
}

SubPath fitFreehand(const std::vector<QPointF> &points, double tolerance, bool closed)
{
    std::vector<QPointF> unique;
    for (const QPointF &p : points) {
        if (unique.empty() || !samePoint(p, unique.back(), 1e-9))
            unique.push_back(p);
    }
    SubPath sp;
    if (unique.size() < 2)
        return sp;

    std::vector<QPointF> kept = simplify(unique, tolerance);
    if (closed && kept.size() > 3 && length(kept.front() - kept.back()) <= 3.0 * tolerance)
        kept.pop_back();
    sp.closed = closed && kept.size() >= 3;

    const int n = static_cast<int>(kept.size());
    if (n == 2) {
        sp.nodes = {PathNode::corner(kept[0]), PathNode::corner(kept[1])};
        return sp;
    }
    // Catmull–Rom tangents give smooth curves through the kept points.
    for (int i = 0; i < n; ++i) {
        const QPointF prev = sp.closed ? kept[(i - 1 + n) % n] : kept[std::max(i - 1, 0)];
        const QPointF next = sp.closed ? kept[(i + 1) % n] : kept[std::min(i + 1, n - 1)];
        const QPointF tangent = (next - prev) / 6.0;
        PathNode node{kept[i], kept[i] - tangent, kept[i] + tangent, NodeType::Smooth};
        if (!sp.closed && i == 0) {
            node.in = node.point;
            node.type = NodeType::Corner;
        } else if (!sp.closed && i == n - 1) {
            node.out = node.point;
            node.type = NodeType::Corner;
        }
        sp.nodes.push_back(node);
    }
    return sp;
}

} // namespace opendraw::pathops

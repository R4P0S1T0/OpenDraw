#include "Units.h"

#include "core/Document.h"

#include <QSettings>

namespace opendraw {

Units &Units::instance()
{
    static Units units;
    return units;
}

Units::Units()
{
    const int stored = QSettings().value(QStringLiteral("view/unit"), int(Unit::Millimeters)).toInt();
    if (stored >= int(Unit::Millimeters) && stored <= int(Unit::Pixels))
        m_unit = Unit(stored);
}

void Units::setUnit(Unit unit)
{
    if (unit == m_unit)
        return;
    m_unit = unit;
    QSettings().setValue(QStringLiteral("view/unit"), int(unit));
    emit unitChanged(unit);
}

double Units::unitsPer(Unit unit)
{
    switch (unit) {
    case Unit::Millimeters:
        return kUnitsPerMm;
    case Unit::Centimeters:
        return kUnitsPerMm * 10.0;
    case Unit::Inches:
        return 96.0;
    case Unit::Points:
        return 96.0 / 72.0;
    case Unit::Pixels:
        return 1.0;
    }
    return kUnitsPerMm;
}

QString Units::suffix(Unit unit)
{
    switch (unit) {
    case Unit::Millimeters:
        return tr("mm");
    case Unit::Centimeters:
        return tr("cm");
    case Unit::Inches:
        return tr("in");
    case Unit::Points:
        return tr("pt");
    case Unit::Pixels:
        return tr("px");
    }
    return {};
}

QString Units::name(Unit unit)
{
    switch (unit) {
    case Unit::Millimeters:
        return tr("millimeters");
    case Unit::Centimeters:
        return tr("centimeters");
    case Unit::Inches:
        return tr("inches");
    case Unit::Points:
        return tr("points");
    case Unit::Pixels:
        return tr("pixels");
    }
    return {};
}

int Units::decimals() const
{
    switch (m_unit) {
    case Unit::Inches:
    case Unit::Centimeters:
        return 3;
    case Unit::Pixels:
    case Unit::Points:
        return 1;
    case Unit::Millimeters:
        break;
    }
    return 2;
}

} // namespace opendraw

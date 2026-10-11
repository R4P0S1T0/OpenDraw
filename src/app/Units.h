#pragma once

#include <QObject>
#include <QString>

namespace opendraw {

// The unit measurements are shown in (property bar, rulers, status bar). Documents always
// store document units; this is only a display preference, remembered between sessions.
enum class Unit { Millimeters, Centimeters, Inches, Points, Pixels };

class Units : public QObject {
    Q_OBJECT

public:
    static Units &instance();

    Unit unit() const { return m_unit; }
    void setUnit(Unit unit);

    // Document units in one display unit.
    static double unitsPer(Unit unit);
    double toDisplay(double documentUnits) const { return documentUnits / unitsPer(m_unit); }
    double fromDisplay(double value) const { return value * unitsPer(m_unit); }
    static QString suffix(Unit unit);          // "mm", "in", ...
    static QString name(Unit unit);            // "millimeters", ...
    QString suffix() const { return suffix(m_unit); }
    int decimals() const;

signals:
    void unitChanged(opendraw::Unit unit);

private:
    Units();
    Unit m_unit = Unit::Millimeters;
};

} // namespace opendraw

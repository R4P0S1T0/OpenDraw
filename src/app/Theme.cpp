#include "Theme.h"

#include <QApplication>
#include <QPalette>
#include <QSettings>
#include <QStyleFactory>

namespace opendraw::theme {

namespace {

QPalette darkPalette()
{
    QPalette p;
    const QColor window(0x2b, 0x2b, 0x2b);
    const QColor text(0xe4, 0xe4, 0xe4);
    const QColor disabled(0x7a, 0x7a, 0x7a);
    p.setColor(QPalette::Window, window);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, QColor(0x1e, 0x1e, 0x1e));
    p.setColor(QPalette::AlternateBase, QColor(0x26, 0x26, 0x26));
    p.setColor(QPalette::ToolTipBase, QColor(0x3a, 0x3a, 0x3a));
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::PlaceholderText, disabled);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, QColor(0x33, 0x33, 0x33));
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::BrightText, Qt::white);
    p.setColor(QPalette::Light, QColor(0x44, 0x44, 0x44));
    p.setColor(QPalette::Midlight, QColor(0x3a, 0x3a, 0x3a));
    p.setColor(QPalette::Mid, QColor(0x3c, 0x3c, 0x3c)); // the desktop around the page
    p.setColor(QPalette::Dark, QColor(0x1a, 0x1a, 0x1a));
    p.setColor(QPalette::Shadow, QColor(0x10, 0x10, 0x10));
    p.setColor(QPalette::Highlight, QColor(0x25, 0x63, 0xa8));
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::Link, QColor(0x5a, 0xa9, 0xf0));
    for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, role, disabled);
    p.setColor(QPalette::Disabled, QPalette::Highlight, QColor(0x44, 0x44, 0x44));
    return p;
}

QPalette lightPalette()
{
    QPalette p;
    const QColor text(0x1d, 0x1d, 0x1d);
    p.setColor(QPalette::Window, QColor(0xf0, 0xf0, 0xf0));
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, Qt::white);
    p.setColor(QPalette::AlternateBase, QColor(0xf5, 0xf5, 0xf5));
    p.setColor(QPalette::ToolTipBase, QColor(0xff, 0xff, 0xe8));
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, QColor(0xe8, 0xe8, 0xe8));
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::Light, Qt::white);
    p.setColor(QPalette::Midlight, QColor(0xe0, 0xe0, 0xe0));
    p.setColor(QPalette::Mid, QColor(0xc8, 0xc8, 0xc8));
    p.setColor(QPalette::Dark, QColor(0xa0, 0xa0, 0xa0));
    p.setColor(QPalette::Shadow, QColor(0x60, 0x60, 0x60));
    p.setColor(QPalette::Highlight, QColor(0x2f, 0x74, 0xc0));
    p.setColor(QPalette::HighlightedText, Qt::white);
    for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, role, QColor(0xa0, 0xa0, 0xa0));
    return p;
}

// Flat toolbars and tool buttons; the checked tool gets the highlight colour.
QString styleSheet(const QPalette &p)
{
    const QString highlight = p.color(QPalette::Highlight).name();
    const QString border = p.color(QPalette::Dark).name();
    const QString hover = p.color(QPalette::Light).name();
    return QStringLiteral(
               "QToolBar { border: none; spacing: 2px; padding: 2px; }"
               "QToolBar::separator { width: 1px; height: 1px; margin: 4px 4px; background: %2; }"
               "QToolButton { border: 1px solid transparent; border-radius: 3px; padding: 2px; }"
               "QToolButton:hover { background: %3; }"
               "QToolButton:checked { background: %1; border-color: %1; }"
               "QStatusBar { border-top: 1px solid %2; }"
               "QStatusBar::item { border: none; }"
               "QMainWindow::separator { background: %2; width: 1px; height: 1px; }"
               "QDockWidget::title { padding: 4px; }")
        .arg(highlight, border, hover);
}

} // namespace

ColorScheme stored()
{
    return QSettings().value(QStringLiteral("view/colorScheme")).toString() == QStringLiteral("light")
               ? ColorScheme::Light
               : ColorScheme::Dark;
}

void apply(ColorScheme scheme)
{
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    const QPalette palette = scheme == ColorScheme::Dark ? darkPalette() : lightPalette();
    QApplication::setPalette(palette);
    qApp->setStyleSheet(styleSheet(palette));
}

void set(ColorScheme scheme)
{
    QSettings().setValue(QStringLiteral("view/colorScheme"),
                         scheme == ColorScheme::Light ? QStringLiteral("light") : QStringLiteral("dark"));
    apply(scheme);
}

} // namespace opendraw::theme

#include "Language.h"
#include "MainWindow.h"
#include "Theme.h"

#include <QApplication>
#include <QIcon>
#include <QTimer>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("OpenDraw"));
    QApplication::setOrganizationName(QStringLiteral("OpenDraw"));
    QApplication::setApplicationVersion(QStringLiteral(OPENDRAW_VERSION));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/opendraw-icon-256.png")));
    QApplication::setDesktopFileName(QStringLiteral("opendraw"));
    opendraw::theme::apply(opendraw::theme::stored());
    opendraw::language::install();

    opendraw::MainWindow window;
    window.show();

    // Open after the window is on screen so any message about the file appears on top of it.
    const QStringList args = QApplication::arguments();
    if (args.size() > 1)
        QTimer::singleShot(0, &window, [&window, path = args.at(1)] { window.openFile(path); });

    return QApplication::exec();
}

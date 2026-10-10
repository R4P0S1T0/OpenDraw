#include "MainWindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("OpenDraw"));
    QApplication::setOrganizationName(QStringLiteral("OpenDraw"));
    QApplication::setApplicationVersion(QStringLiteral(OPENDRAW_VERSION));

    opendraw::MainWindow window;
    const QStringList args = QApplication::arguments();
    if (args.size() > 1)
        window.openFile(args.at(1));
    window.show();

    return QApplication::exec();
}

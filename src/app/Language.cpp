#include "Language.h"

#include <QCoreApplication>
#include <QDir>
#include <QLibraryInfo>
#include <QLocale>
#include <QSettings>
#include <QTranslator>

namespace opendraw::language {

QString stored()
{
    return QSettings().value(QStringLiteral("view/language")).toString();
}

void store(const QString &name)
{
    QSettings().setValue(QStringLiteral("view/language"), name);
}

QStringList available()
{
    QStringList names{QStringLiteral("en")};
    const QStringList files = QDir(QStringLiteral(":/i18n")).entryList({QStringLiteral("opendraw_*.qm")});
    for (const QString &file : files)
        names << file.mid(9, file.size() - 9 - 3); // opendraw_<name>.qm
    return names;
}

void install()
{
    const QString name = stored();
    const QLocale locale = name.isEmpty() ? QLocale::system() : QLocale(name);
    if (locale.language() == QLocale::English)
        return;
    QLocale::setDefault(locale);

    auto *app = new QTranslator(qApp);
    if (app->load(locale, QStringLiteral("opendraw"), QStringLiteral("_"), QStringLiteral(":/i18n")))
        QCoreApplication::installTranslator(app);
    else
        delete app;

    // Qt's own strings: from the Qt installation, or from a "translations" folder next to the
    // program in packaged builds.
    auto *qt = new QTranslator(qApp);
    const QStringList folders{QLibraryInfo::path(QLibraryInfo::TranslationsPath),
                              QCoreApplication::applicationDirPath() + QStringLiteral("/translations")};
    for (const QString &folder : folders) {
        if (qt->load(locale, QStringLiteral("qtbase"), QStringLiteral("_"), folder)) {
            QCoreApplication::installTranslator(qt);
            return;
        }
    }
    delete qt;
}

} // namespace opendraw::language

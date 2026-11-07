#include "app/Localization.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QLocale>
#include <QTranslator>

namespace app::i18n {
namespace {

QTranslator *g_translator = nullptr;
QString g_language = QStringLiteral("ru");

QString translationsDir()
{
    return QCoreApplication::applicationDirPath() + QStringLiteral("/translations");
}

} // namespace

QString resolve(const QString &languageSetting)
{
    const QString setting = languageSetting.trimmed().toLower();

    if (setting == QLatin1String("ru") || setting == QLatin1String("en"))
        return setting;

    const QString system = QLocale::system().name().toLower();
    return system.startsWith(QLatin1String("en")) ? QStringLiteral("en") : QStringLiteral("ru");
}

void install(const QString &languageSetting)
{
    const QString language = resolve(languageSetting);
    g_language = language;

    if (g_translator) {
        QCoreApplication::removeTranslator(g_translator);
        delete g_translator;
        g_translator = nullptr;
    }

    // Русский — исходный язык интерфейса, перевод не требуется.
    if (language == QLatin1String("ru"))
        return;

    auto *translator = new QTranslator(QCoreApplication::instance());
    const QString fileName = QStringLiteral("messenger_%1.qm").arg(language);

    if (!translator->load(translationsDir() + QLatin1Char('/') + fileName)
        && !translator->load(QStringLiteral(":/i18n/") + fileName)) {
        qWarning().noquote() << QStringLiteral("Перевод не найден: %1").arg(fileName);
        delete translator;
        return;
    }

    QCoreApplication::installTranslator(translator);
    g_translator = translator;
}

QString resolvedLanguage()
{
    return g_language;
}

} // namespace app::i18n

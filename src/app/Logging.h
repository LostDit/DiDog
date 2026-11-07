#pragma once

#include <QString>

// Логирование в файл через qInstallMessageHandler.
// Файлы: <dir>/app-YYYY-MM-DD.log, размер ограничен, старые удаляются.
namespace app::logging {

// Устанавливает обработчик. Каталог должен существовать (см. AppPaths::ensureDirectories).
// verbose == false отбрасывает qDebug. echoToConsole дублирует INFO в stderr
// (нужно консольному серверу, клиент дублирует только при verbose).
void install(const QString &logDirectory, bool verbose, bool echoToConsole = false);

// Возвращает прежний обработчик и закрывает файл.
void shutdown();

void setVerbose(bool verbose);
bool isVerbose();

// Путь к текущему файлу лога (может быть пустым до первого сообщения).
QString currentLogFile();

} // namespace app::logging

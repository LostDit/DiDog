#pragma once

#include <QString>

// Каталоги данных приложения. Все пути создаются автоматически при первом запуске.
// Приоритет источников: setOverrideDataDir() → MESSENGER_DATA_DIR → AppDataLocation.
namespace app::paths {

// Явное переопределение (флаг --data / --profile). Пустая строка сбрасывает.
void setOverrideDataDir(const QString &dir);
QString overrideDataDir();

QString dataDir();
QString logsDir();
QString attachmentsDir();
QString thumbnailsDir();
QString tmpDir();
QString settingsFile();
QString clientDbPath();
QString serverDbPath();

// Создаёт дерево каталогов. При ошибке заполняет error и возвращает false.
bool ensureDirectories(QString *error = nullptr);

} // namespace app::paths

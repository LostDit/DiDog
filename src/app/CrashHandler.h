#pragma once

#include <QString>

namespace app {

// Перехват необработанных исключений Windows: сохраняет minidump и текстовый
// отчёт в <data>/crashes, чтобы падение можно было разобрать по стеку.
class CrashHandler
{
public:
    static void install(const QString &dataDir);
};

} // namespace app

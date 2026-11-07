#pragma once

#include <QString>

namespace app::i18n {

// Устанавливает переводчик по настройке языка ("auto" | "ru" | "en").
// Файлы .qm ищутся рядом с исполняемым файлом в каталоге translations/.
void install(const QString &languageSetting);

// Фактически выбранный язык: "ru" или "en".
QString resolvedLanguage();

// Язык, который будет использован с учётом системного окружения.
QString resolve(const QString &languageSetting);

} // namespace app::i18n

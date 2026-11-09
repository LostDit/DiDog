#pragma once

#include <QSqlDatabase>
#include <QString>

namespace data::migrations {

// Версия схемы, которую понимает этот бинарник. Увеличивается при изменении DDL.
constexpr int kCurrentVersion = 8;

// Текущая версия БД (PRAGMA user_version).
int version(QSqlDatabase &db);

// Применяет недостающие шаги в транзакциях. Возвращает false и текст ошибки при сбое.
bool apply(QSqlDatabase &db, QString *error);

} // namespace data::migrations

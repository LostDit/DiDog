#pragma once

#include <QtGlobal>

// Числовые ограничения протокола и модели данных.
// Значения синхронизированы с docs/TZ-v2.md §13.1 и docs/protocol.md.
namespace core::limits {

// ── Транспорт ──────────────────────────────────────────────────────────────
constexpr quint32 kJsonFrameMaxBytes = 1024 * 1024;   // 1 МиБ на один JSON-кадр
constexpr quint32 kChunkPayloadMaxBytes = 256 * 1024; // максимум данных в одном чанке
constexpr quint32 kChunkSizeBytes = 64 * 1024;        // размер чанка при передаче файла

// ── Вложения ───────────────────────────────────────────────────────────────
constexpr qint64 kAttachmentMaxBytes = 25LL * 1024 * 1024; // 25 МиБ
constexpr int kFileNameMaxChars = 255;

// ── Текстовые поля ─────────────────────────────────────────────────────────
constexpr int kMessageMaxChars = 4000;
constexpr int kUserNameMinChars = 3;
constexpr int kUserNameMaxChars = 32;
constexpr int kDisplayNameMaxChars = 48;
constexpr int kPasswordMinChars = 8;
constexpr int kPasswordMaxChars = 128;

// ── Пагинация ──────────────────────────────────────────────────────────────
constexpr int kHistoryPageSize = 50;
constexpr int kHistoryPageSizeMax = 100;

// ── Таймауты (мс) ──────────────────────────────────────────────────────────
constexpr int kPingIntervalMs = 30 * 1000;
constexpr int kIdleTimeoutMs = 120 * 1000;
constexpr int kRequestTimeoutMs = 30 * 1000;
constexpr int kTransferTtlMs = 10 * 60 * 1000;
constexpr int kBusyTimeoutMs = 5000;

} // namespace core::limits

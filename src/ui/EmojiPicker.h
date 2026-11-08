#pragma once

#include <QMenu>
#include <QString>
#include <QVector>

class QLineEdit;
class QGridLayout;
class QWidget;

namespace ui {

struct EmojiEntry {
    QString symbol;
    QString name;     // для поиска: «улыбка», «сердце», «огонь»
};

// Каталог Unicode-эмодзи. Позже сюда можно добавить провайдер кастомных паков.
class EmojiCatalog
{
public:
    static const QVector<EmojiEntry> &all();
    static QVector<EmojiEntry> search(const QString &query);
};

// Всплывающая панель выбора эмодзи с поиском.
class EmojiPicker : public QMenu
{
    Q_OBJECT

public:
    explicit EmojiPicker(QWidget *parent = nullptr);

signals:
    void emojiSelected(const QString &symbol);

private:
    void rebuild(const QString &query);

    QLineEdit *m_search = nullptr;
    QWidget *m_gridHost = nullptr;
    QGridLayout *m_grid = nullptr;
};

} // namespace ui

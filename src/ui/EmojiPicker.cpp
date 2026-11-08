#include "ui/EmojiPicker.h"

#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidgetAction>

namespace ui {
namespace {

struct RawEmoji {
    const char *symbol;
    const char *name;
};

// Небольшой, но покрывающий повседневные сценарии набор.
const RawEmoji kEmojis[] = {
    {"😀", "улыбка смех"},       {"😃", "улыбка"},          {"😄", "смех"},
    {"😁", "ухмылка"},          {"😆", "смех зажмурился"}, {"😅", "смех пот"},
    {"😂", "слёзы смех"},       {"🤣", "катаюсь смех"},    {"😊", "улыбка румянец"},
    {"😇", "ангел"},            {"🙂", "улыбка"},           {"🙃", "перевёрнутая улыбка"},
    {"😉", "подмигивание"},     {"😌", "спокойствие"},      {"😍", "влюблённые глаза"},
    {"🥰", "любовь"},           {"😘", "поцелуй"},          {"😗", "поцелуй"},
    {"😙", "поцелуй"},          {"😚", "поцелуй"},          {"😋", "вкусно"},
    {"😛", "язык"},             {"😝", "язык зажмурился"},  {"😜", "язык подмигивание"},
    {"🤪", "сумасшедший"},      {"🤨", "бровь"},            {"🧐", "монокль"},
    {"🤓", "очки"},             {"😎", "крутой очки"},      {"🥳", "праздник"},
    {"😏", "ухмылка"},          {"😒", "недовольство"},     {"😞", "грусть"},
    {"😔", "задумчивость"},     {"😟", "беспокойство"},     {"😕", "непонимание"},
    {"🙁", "грусть"},           {"😣", "упрямство"},        {"😖", "расстройство"},
    {"😫", "усталость"},        {"😩", "измучен"},          {"🥺", "умоляю"},
    {"😢", "слеза"},            {"😭", "плач"},             {"😤", "пар"},
    {"😠", "злость"},           {"😡", "гнев"},             {"🤬", "ругательство"},
    {"🤯", "взрыв мозга"},      {"😳", "шок"},              {"🥵", "жарко"},
    {"🥶", "холодно"},          {"😱", "страх"},            {"😨", "боюсь"},
    {"😰", "тревога"},          {"😥", "грустный пот"},     {"😓", "пот"},
    {"🤗", "обнимаю"},          {"🤔", "думаю"},            {"🤭", "хихикаю"},
    {"🤫", "тихо"},             {"🤥", "вру"},              {"😶", "без слов"},
    {"😐", "нейтрально"},       {"😑", "без эмоций"},       {"😬", "неловко"},
    {"🙄", "закатил глаза"},    {"😯", "удивление"},        {"😦", "открытый рот"},
    {"😧", "страдание"},        {"😮", "ооо"},              {"😲", "изумление"},
    {"🥱", "зеваю"},            {"😴", "сплю"},             {"🤤", "слюна"},
    {"😪", "сонный"},           {"😵", "голова кругом"},    {"🤐", "молчу"},
    {"🥴", "пьяный"},           {"🤢", "тошнит"},           {"🤮", "рвота"},
    {"🤧", "чихаю"},            {"😷", "маска"},            {"🤒", "температура"},
    {"🤕", "повязка"},          {"🤑", "деньги"},           {"🤠", "ковбой"},
    {"😈", "чёрт"},             {"👿", "злой чёрт"},        {"👻", "призрак"},
    {"💀", "череп"},            {"☠️", "смерть"},           {"🤖", "робот"},
    {"🎃", "тыква"},            {"😺", "кот"},              {"😸", "кот смех"},
    {"😹", "кот слёзы"},        {"😻", "кот любовь"},       {"😼", "кот ухмылка"},
    {"😽", "кот поцелуй"},      {"🙀", "кот крик"},          {"😿", "кот плачет"},
    {"😾", "кот злой"},         {"👍", "палец вверх лайк"},  {"👎", "палец вниз дислайк"},
    {"👌", "ок"},               {"✌️", "мир победа"},        {"🤞", "удача"},
    {"🤟", "любовь жест"},      {"🤘", "рок"},              {"🤙", "позвони"},
    {"👈", "влево"},            {"👉", "вправо"},           {"👆", "вверх"},
    {"👇", "вниз"},             {"☝️", "указательный"},     {"✋", "стоп"},
    {"🤚", "рука"},             {"🖐️", "ладонь"},           {"🖖", "вулкан"},
    {"👋", "привет"},           {"🤝", "рукопожатие"},      {"🙏", "спасибо пожалуйста"},
    {"💪", "бицепс сила"},      {"🫶", "сердце руками"},     {"❤️", "сердце красное"},
    {"🧡", "сердце оранжевое"},  {"💛", "сердце жёлтое"},    {"💚", "сердце зелёное"},
    {"💙", "сердце синее"},     {"💜", "сердце фиолетовое"}, {"🖤", "сердце чёрное"},
    {"🤍", "сердце белое"},     {"🤎", "сердце коричневое"}, {"💔", "разбитое сердце"},
    {"💕", "два сердца"},       {"💞", "сердца"},           {"💓", "сердцебиение"},
    {"💗", "растущее сердце"},  {"💖", "блеск"},            {"💘", "стрела"},
    {"💝", "подарок сердце"},   {"🔥", "огонь"},            {"✨", "искры"},
    {"⭐", "звезда"},           {"🌟", "сияние"},           {"💫", "звёзды"},
    {"⚡", "молния"},           {"💥", "взрыв"},            {"❄️", "снег"},
    {"🌈", "радуга"},           {"☀️", "солнце"},           {"🌙", "луна"},
    {"☁️", "облако"},           {"🌧️", "дождь"},           {"⛅", "облачно"},
    {"🎉", "праздник"},         {"🎊", "конфетти"},         {"🎁", "подарок"},
    {"🎂", "торт"},             {"🍰", "пирожное"},         {"🍕", "пицца"},
    {"🍔", "бургер"},           {"🍟", "картошка"},         {"🌮", "тако"},
    {"🍣", "суши"},             {"🍜", "лапша"},            {"🍎", "яблоко"},
    {"🍌", "банан"},            {"🍓", "клубника"},         {"🍇", "виноград"},
    {"☕", "кофе"},             {"🍵", "чай"},              {"🍺", "пиво"},
    {"🍷", "вино"},             {"🥂", "шампанское"},       {"🥤", "напиток"},
    {"⚽", "футбол"},           {"🏀", "баскетбол"},        {"🎮", "игра"},
    {"🎧", "наушники"},         {"🎵", "музыка"},           {"🎬", "кино"},
    {"📷", "фото"},             {"💻", "ноутбук"},          {"📱", "телефон"},
    {"💡", "идея"},             {"📌", "закреп"},           {"✅", "галочка да"},
    {"❌", "крестик нет"},      {"❗", "восклицание"},       {"❓", "вопрос"},
    {"💯", "сто"},              {"👀", "глаза"},            {"🙈", "обезьяна не вижу"},
    {"🙉", "обезьяна не слышу"}, {"🙊", "обезьяна не говорю"}, {"💤", "сон"},
    {"🚀", "ракета"},           {"✈️", "самолёт"},          {"🚗", "машина"},
    {"🏠", "дом"},              {"🌍", "земля"},            {"🗺️", "карта"},
    {"⏰", "будильник"},         {"⌛", "время"},            {"📅", "календарь"},
    {"📝", "запись"},           {"📎", "скрепка"},          {"🔗", "ссылка"},
    {"🔒", "замок"},            {"🔑", "ключ"},             {"🔔", "колокол"},
    {"🎯", "цель"},             {"🏆", "кубок"},            {"🥇", "медаль"},
    {"💬", "сообщение"},        {"👍🏻", "лайк светлый"},     {"🙌", "руки вверх"},
};

QVector<EmojiEntry> buildCatalog()
{
    QVector<EmojiEntry> result;
    result.reserve(int(sizeof(kEmojis) / sizeof(kEmojis[0])));
    for (const RawEmoji &raw : kEmojis)
        result.append({QString::fromUtf8(raw.symbol), QString::fromUtf8(raw.name)});
    return result;
}

} // namespace

const QVector<EmojiEntry> &EmojiCatalog::all()
{
    static const QVector<EmojiEntry> catalog = buildCatalog();
    return catalog;
}

QVector<EmojiEntry> EmojiCatalog::search(const QString &query)
{
    const QString needle = query.trimmed().toLower();
    if (needle.isEmpty())
        return all();

    QVector<EmojiEntry> result;
    for (const EmojiEntry &entry : all()) {
        if (entry.name.contains(needle))
            result.append(entry);
    }
    return result;
}

EmojiPicker::EmojiPicker(QWidget *parent)
    : QMenu(parent)
{
    setObjectName(QStringLiteral("EmojiPicker"));

    auto *host = new QWidget(this);
    auto *layout = new QVBoxLayout(host);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    m_search = new QLineEdit(host);
    m_search->setPlaceholderText(tr("Поиск эмодзи"));
    m_search->setClearButtonEnabled(true);
    layout->addWidget(m_search);

    auto *scroll = new QScrollArea(host);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setFixedHeight(220);

    m_gridHost = new QWidget(scroll);
    m_grid = new QGridLayout(m_gridHost);
    m_grid->setContentsMargins(0, 0, 0, 0);
    m_grid->setSpacing(2);
    scroll->setWidget(m_gridHost);
    layout->addWidget(scroll);

    auto *action = new QWidgetAction(this);
    action->setDefaultWidget(host);
    addAction(action);

    connect(m_search, &QLineEdit::textChanged, this, &EmojiPicker::rebuild);

    rebuild(QString());
}

void EmojiPicker::rebuild(const QString &query)
{
    while (QLayoutItem *item = m_grid->takeAt(0)) {
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }

    const QVector<EmojiEntry> entries = EmojiCatalog::search(query);
    const int columns = 8;

    for (int i = 0; i < entries.size(); ++i) {
        const EmojiEntry &entry = entries.at(i);

        auto *button = new QToolButton(m_gridHost);
        button->setObjectName(QStringLiteral("EmojiButton"));
        button->setText(entry.symbol);
        button->setToolTip(entry.name);
        button->setFixedSize(32, 32);
        button->setCursor(Qt::PointingHandCursor);
        button->setAutoRaise(true);

        const QString symbol = entry.symbol;
        connect(button, &QToolButton::clicked, this, [this, symbol]() {
            emit emojiSelected(symbol);
            close();
        });

        m_grid->addWidget(button, i / columns, i % columns);
    }

    if (entries.isEmpty()) {
        auto *empty = new QLabel(tr("Ничего не найдено"), m_gridHost);
        empty->setObjectName(QStringLiteral("EmptySubtitle"));
        m_grid->addWidget(empty, 0, 0, 1, columns);
    }
}

} // namespace ui

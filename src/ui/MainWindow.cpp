#include "ui/MainWindow.h"

#include "app/SettingsStore.h"
#include "app/ThemeManager.h"
#include "ui/AvatarWidget.h"

#include <QCloseEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

namespace ui {
namespace {

QToolButton *makeIconButton(const QString &iconPath, const QString &toolTip)
{
    auto *button = new QToolButton;
    button->setObjectName(QStringLiteral("IconButton"));
    button->setIcon(QIcon(iconPath));
    button->setIconSize(QSize(20, 20));
    button->setToolTip(toolTip);
    button->setCursor(Qt::PointingHandCursor);
    button->setFixedSize(34, 34);
    return button;
}

QLabel *makeLabel(const QString &text, const char *objectName, Qt::Alignment alignment)
{
    auto *label = new QLabel(text);
    label->setObjectName(QString::fromLatin1(objectName));
    label->setAlignment(alignment);
    return label;
}

} // namespace

MainWindow::MainWindow(app::ThemeManager *theme, app::SettingsStore *settings, QWidget *parent)
    : QMainWindow(parent)
    , m_theme(theme)
    , m_settings(settings)
{
    setObjectName(QStringLiteral("Root"));
    setWindowTitle(QStringLiteral("MessengerApp"));
    setWindowIcon(QIcon(QStringLiteral(":/icons/logo.svg")));
    setMinimumSize(900, 600);

    auto *central = new QWidget;
    central->setObjectName(QStringLiteral("Root"));

    auto *layout = new QHBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(buildSidebar(), 0);
    layout->addWidget(buildChatPane(), 1);

    setCentralWidget(central);

    restoreWindowState();
    updateThemeButton();

    if (m_theme) {
        connect(m_theme, &app::ThemeManager::themeChanged, this,
                [this](app::ThemeManager::Theme) { updateThemeButton(); });
    }
}

MainWindow::~MainWindow() = default;

QWidget *MainWindow::buildSidebar()
{
    auto *sidebar = new QFrame;
    sidebar->setObjectName(QStringLiteral("Sidebar"));
    sidebar->setFixedWidth(320);

    auto *layout = new QVBoxLayout(sidebar);
    layout->setContentsMargins(16, 16, 16, 12);
    layout->setSpacing(12);

    // ── Профиль ────────────────────────────────────────────────────────────
    auto *profileRow = new QHBoxLayout;
    profileRow->setSpacing(10);

    m_profileAvatar = new AvatarWidget;
    m_profileAvatar->setDiameter(38);
    m_profileAvatar->setIdentity(QStringLiteral("Гость"), 212);
    profileRow->addWidget(m_profileAvatar);

    auto *nameColumn = new QVBoxLayout;
    nameColumn->setSpacing(1);
    m_profileName = makeLabel(QStringLiteral("Гость"), "ProfileName", Qt::AlignLeft);
    m_profileSub = makeLabel(QStringLiteral("Не подключено"), "ProfileSub", Qt::AlignLeft);
    nameColumn->addWidget(m_profileName);
    nameColumn->addWidget(m_profileSub);
    profileRow->addLayout(nameColumn);
    profileRow->addStretch(1);

    m_themeButton = makeIconButton(QStringLiteral(":/icons/moon.svg"),
                                   tr("Переключить тему"));
    connect(m_themeButton, &QToolButton::clicked, this, &MainWindow::onThemeToggleClicked);
    profileRow->addWidget(m_themeButton);

    layout->addLayout(profileRow);

    // ── Заголовок раздела ──────────────────────────────────────────────────
    auto *chatsRow = new QHBoxLayout;
    chatsRow->setSpacing(8);
    chatsRow->addWidget(makeLabel(tr("Чаты"), "SectionTitle", Qt::AlignLeft));
    chatsRow->addStretch(1);

    auto *newChatButton = makeIconButton(QStringLiteral(":/icons/plus.svg"),
                                         tr("Новый чат"));
    newChatButton->setEnabled(false);
    newChatButton->setToolTip(tr("Новый чат появится вместе со списком чатов"));
    chatsRow->addWidget(newChatButton);
    layout->addLayout(chatsRow);

    // ── Поиск ──────────────────────────────────────────────────────────────
    m_searchField = new QLineEdit;
    m_searchField->setObjectName(QStringLiteral("SearchField"));
    m_searchField->setPlaceholderText(tr("Поиск"));
    m_searchField->setClearButtonEnabled(true);
    m_searchField->addAction(QIcon(QStringLiteral(":/icons/search.svg")),
                             QLineEdit::LeadingPosition);
    m_searchField->setFixedHeight(38);
    layout->addWidget(m_searchField);

    // ── Список чатов / пустое состояние ────────────────────────────────────
    m_chatList = new QListWidget;
    m_chatList->setObjectName(QStringLiteral("ChatList"));
    m_chatList->setFrameShape(QFrame::NoFrame);
    m_chatList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_chatList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto *emptyList = makeLabel(tr("Пока нет чатов.\nНачните первую переписку."),
                                "EmptySubtitle", Qt::AlignCenter);
    emptyList->setWordWrap(true);

    auto *listStack = new QStackedWidget;
    listStack->addWidget(m_chatList);
    listStack->addWidget(emptyList);
    listStack->setCurrentIndex(m_chatList->count() == 0 ? 1 : 0);

    layout->addWidget(listStack, 1);

    layout->addWidget(makeLabel(QStringLiteral("v%1").arg(QStringLiteral(MESSENGER_VERSION)),
                                "EmptySubtitle", Qt::AlignLeft));

    return sidebar;
}

QWidget *MainWindow::buildChatPane()
{
    auto *pane = new QFrame;
    pane->setObjectName(QStringLiteral("ChatPane"));

    auto *layout = new QVBoxLayout(pane);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_chatStack = new QStackedWidget;
    m_chatStack->addWidget(buildEmptyChatPane());
    layout->addWidget(m_chatStack, 1);

    return pane;
}

QWidget *MainWindow::buildEmptyChatPane()
{
    auto *page = new QWidget;

    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(32, 32, 32, 32);
    layout->setSpacing(16);
    layout->addStretch(1);

    auto *badge = new QLabel;
    badge->setObjectName(QStringLiteral("EmptyBadge"));
    badge->setFixedSize(64, 64);
    badge->setAlignment(Qt::AlignCenter);
    badge->setPixmap(QIcon(QStringLiteral(":/icons/logo.svg")).pixmap(40, 40));

    auto *title = makeLabel(tr("Выберите чат"), "EmptyTitle", Qt::AlignCenter);
    auto *subtitle = makeLabel(tr("Слева — список переписок.\nИстория сохраняется на этом устройстве."),
                               "EmptySubtitle", Qt::AlignCenter);

    layout->addWidget(badge, 0, Qt::AlignHCenter);
    layout->addWidget(title);
    layout->addWidget(subtitle);
    layout->addStretch(1);

    return page;
}

void MainWindow::onThemeToggleClicked()
{
    if (m_theme)
        m_theme->toggle();
}

void MainWindow::updateThemeButton()
{
    if (!m_themeButton || !m_theme)
        return;

    const bool dark = (m_theme->theme() == app::ThemeManager::Theme::Dark);
    m_themeButton->setIcon(QIcon(dark ? QStringLiteral(":/icons/sun.svg")
                                      : QStringLiteral(":/icons/moon.svg")));
    m_themeButton->setToolTip(dark ? tr("Светлая тема") : tr("Тёмная тема"));
}

void MainWindow::restoreWindowState()
{
    resize(1100, 720);

    if (!m_settings)
        return;

    const QByteArray geometry = m_settings->windowGeometry();
    if (!geometry.isEmpty())
        restoreGeometry(geometry);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_settings) {
        m_settings->setWindowGeometry(saveGeometry());
        m_settings->sync();
    }
    QMainWindow::closeEvent(event);
}

} // namespace ui

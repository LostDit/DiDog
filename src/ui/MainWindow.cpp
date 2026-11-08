#include "ui/MainWindow.h"

#include "app/SessionService.h"
#include "app/SettingsStore.h"
#include "app/ThemeManager.h"
#include "net/ClientConnection.h"
#include "ui/AvatarWidget.h"
#include "ui/ChatItemDelegate.h"
#include "ui/ChatListModel.h"
#include "ui/ComposerWidget.h"
#include "ui/GroupDialog.h"
#include "ui/ImageViewerDialog.h"
#include "ui/MessageView.h"
#include "ui/ParticipantsDialog.h"
#include "ui/ProfileCardDialog.h"
#include "ui/SearchDialog.h"
#include "ui/SettingsDialog.h"
#include "ui/VideoPlayerDialog.h"
#include "app/UpdateService.h"
#include "voice/VoiceService.h"

#include <QAction>
#include <QCloseEvent>
#include <QCursor>
#include <QDebug>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QShortcut>
#include <QSortFilterProxyModel>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace ui {
namespace {

QLabel *makeLabel(const QString &text, const char *objectName, Qt::Alignment alignment)
{
    auto *label = new QLabel(text);
    label->setObjectName(QString::fromLatin1(objectName));
    label->setAlignment(alignment);
    return label;
}

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

} // namespace

MainWindow::MainWindow(app::ChatService *chats, app::SessionService *session,
                       net::ClientConnection *connection, app::SettingsStore *settings,
                       app::ThemeManager *theme, app::BillingService *billing,
                       app::VoiceService *voice, QWidget *parent)
    : QMainWindow(parent)
    , m_chats(chats)
    , m_session(session)
    , m_settings(settings)
    , m_theme(theme)
    , m_billing(billing)
    , m_voice(voice)
    , m_updater(new app::UpdateService(connection, this))
    , m_connection(connection)
{
    setObjectName(QStringLiteral("Root"));
    setWindowTitle(QStringLiteral("MessengerApp"));
    setWindowIcon(QIcon(QStringLiteral(":/icons/logo.svg")));
    setMinimumSize(900, 600);

    buildUi();
    restoreWindowState();

    connect(m_chats, &app::ChatService::chatsChanged, this, &MainWindow::onChatsChanged);
    connect(m_chats, &app::ChatService::membersLoaded, this,
            [this](qint64 chatId, const QList<data::ChatMember> &members) {
                if (chatId != m_currentChatId)
                    return;
                m_groupMemberCount = members.size();
                updateHeader();
            });
    connect(m_chats, &app::ChatService::groupCreated, this,
            [this](qint64 chatId) { openChatById(chatId); });

    if (m_voice) {
        connect(m_voice, &app::VoiceService::stateChanged, this, [this](app::VoiceService::State state) {
            if (state == app::VoiceService::State::Idle)
                m_speakingUserId = 0;
            updateVoiceUi();
            updateHeader();
        });        connect(m_voice, &app::VoiceService::mutedChanged, this,
                [this](bool) { updateVoiceUi(); });
        connect(m_voice, &app::VoiceService::errorOccurred, this, &MainWindow::onError);
        connect(m_voice, &app::VoiceService::incomingCall, this, &MainWindow::onIncomingCall);
        connect(m_voice, &app::VoiceService::callAccepted, this, &MainWindow::onCallAccepted);
        connect(m_voice, &app::VoiceService::callDeclined, this, &MainWindow::onCallDeclined);
        connect(m_voice, &app::VoiceService::callCancelled, this, &MainWindow::onCallCancelled);
        connect(m_voice, &app::VoiceService::speakingChanged, this,
                [this](qint64 chatId, qint64 userId, bool speaking) {
                    if (chatId != m_currentChatId)
                        return;
                    m_speakingUserId = speaking ? userId : 0;
                    updateHeader();
                });
    }
    connect(m_chats, &app::ChatService::messagesChanged, this, &MainWindow::onMessagesChanged);
    connect(m_chats, &app::ChatService::errorOccurred, this, &MainWindow::onError);
    connect(m_chats, &app::ChatService::typingChanged, this, &MainWindow::onTypingChanged);
    connect(m_chats, &app::ChatService::presenceChanged, this, &MainWindow::onPresenceChanged);

    if (connection) {
        connect(connection, &net::ClientConnection::stateChanged, this,
                [this](net::ClientConnection::State) { updateConnectionBanner(); });
        updateConnectionBanner();
    }

    if (m_theme) {
        connect(m_theme, &app::ThemeManager::themeChanged, this,
                [this](app::ThemeManager::Theme) { updateThemeButton(); });
        updateThemeButton();
    }

    auto *findShortcut = new QShortcut(QKeySequence::Find, this);
    connect(findShortcut, &QShortcut::activated, this, &MainWindow::openSearch);

    connect(m_chats, &app::ChatService::searchResults, this,
            [this](const QString &, const QList<data::Message> &messages) {
                if (m_searchDialog)
                    m_searchDialog->setResults(messages, chatNames());
            });

    m_typingTimer = new QTimer(this);
    m_typingTimer->setSingleShot(true);
    m_typingTimer->setInterval(5000);
    connect(m_typingTimer, &QTimer::timeout, this, [this]() {
        m_typingUserId = 0;
        updateHeader();
    });

    m_toastTimer = new QTimer(this);
    m_toastTimer->setSingleShot(true);
    m_toastTimer->setInterval(4000);
    connect(m_toastTimer, &QTimer::timeout, this, [this]() {
        if (m_toast)
            m_toast->hide();
    });

    auto *newChatShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+N")), this);
    connect(newChatShortcut, &QShortcut::activated, this, &MainWindow::onNewChatClicked);
}

MainWindow::~MainWindow() = default;

void MainWindow::setUser(const data::User &user)
{
    m_user = user;
    m_profileAvatar->setIdentity(user.displayName, user.avatarHue);
    m_profileName->setText(user.displayName);
    m_profileSub->setText(QStringLiteral("@%1").arg(user.username));
    m_messageView->setCurrentUserId(user.id);

    if (m_updater) {
        connect(m_updater, &app::UpdateService::checkFinished, this,
                [this](bool available, const QString &version, qint64) {
                    if (available)
                        showToast(tr("Доступно обновление %1 — Настройки → Обновления")
                                      .arg(version));
                });

        // Автопроверка через несколько секунд после входа.
        QTimer::singleShot(8000, this, [this]() {
            if (m_updater && m_connection && m_connection->isConnected())
                m_updater->checkForUpdates();
        });
    }
}

void MainWindow::buildUi()
{
    auto *central = new QWidget;
    central->setObjectName(QStringLiteral("Root"));

    auto *layout = new QHBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(buildSidebar(), 0);
    layout->addWidget(buildChatPane(), 1);

    setCentralWidget(central);
}

QWidget *MainWindow::buildSidebar()
{
    auto *sidebar = new QFrame;
    sidebar->setObjectName(QStringLiteral("Sidebar"));
    sidebar->setFixedWidth(320);

    auto *layout = new QVBoxLayout(sidebar);
    layout->setContentsMargins(16, 16, 16, 12);
    layout->setSpacing(12);

    auto *profileRow = new QHBoxLayout;
    profileRow->setSpacing(10);

    m_profileAvatar = new AvatarWidget;
    m_profileAvatar->setDiameter(38);
    m_profileAvatar->setIdentity(QStringLiteral("Гость"), 212);
    m_profileAvatar->setCursor(Qt::PointingHandCursor);
    m_profileAvatar->installEventFilter(this);
    profileRow->addWidget(m_profileAvatar);

    auto *nameColumn = new QVBoxLayout;
    nameColumn->setSpacing(1);
    m_profileName = makeLabel(QStringLiteral("Гость"), "ProfileName", Qt::AlignLeft);
    m_profileSub = makeLabel(QString(), "ProfileSub", Qt::AlignLeft);
    nameColumn->addWidget(m_profileName);
    nameColumn->addWidget(m_profileSub);
    profileRow->addLayout(nameColumn);
    profileRow->addStretch(1);

    m_themeButton = makeIconButton(QStringLiteral(":/icons/moon.svg"), tr("Переключить тему"));
    connect(m_themeButton, &QToolButton::clicked, this, &MainWindow::onThemeToggleClicked);
    profileRow->addWidget(m_themeButton);

    auto *menuButton = makeIconButton(QStringLiteral(":/icons/more.svg"), tr("Меню"));
    auto *menu = new QMenu(menuButton);

    QAction *settingsAction = menu->addAction(tr("Настройки…"));
    connect(settingsAction, &QAction::triggered, this, &MainWindow::openSettings);

    menu->addSeparator();

    QAction *logoutAction = menu->addAction(tr("Выйти"));
    connect(logoutAction, &QAction::triggered, this, [this]() {
        if (m_session)
            m_session->logout();
    });
    menuButton->setMenu(menu);
    menuButton->setPopupMode(QToolButton::InstantPopup);
    profileRow->addWidget(menuButton);

    layout->addLayout(profileRow);

    auto *chatsRow = new QHBoxLayout;
    chatsRow->setSpacing(8);
    chatsRow->addWidget(makeLabel(tr("Чаты"), "SectionTitle", Qt::AlignLeft));
    chatsRow->addStretch(1);

    auto *newChatButton = makeIconButton(QStringLiteral(":/icons/plus.svg"), tr("Новый чат"));
    connect(newChatButton, &QToolButton::clicked, this, &MainWindow::onNewChatClicked);
    chatsRow->addWidget(newChatButton);
    layout->addLayout(chatsRow);

    m_searchField = new QLineEdit;
    m_searchField->setObjectName(QStringLiteral("SearchField"));
    m_searchField->setPlaceholderText(tr("Поиск"));
    m_searchField->setClearButtonEnabled(true);
    m_searchField->addAction(QIcon(QStringLiteral(":/icons/search.svg")),
                             QLineEdit::LeadingPosition);
    m_searchField->setFixedHeight(38);
    m_searchField->setAccessibleName(tr("Поиск по чатам"));
    layout->addWidget(m_searchField);

    m_chatModel = new ChatListModel(this);
    m_chatProxy = new QSortFilterProxyModel(this);
    m_chatProxy->setSourceModel(m_chatModel);
    m_chatProxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_chatProxy->setFilterRole(Qt::DisplayRole);
    m_chatProxy->setDynamicSortFilter(true);

    m_chatList = new QListView;
    m_chatList->setObjectName(QStringLiteral("ChatList"));
    m_chatList->setModel(m_chatProxy);
    m_chatList->setItemDelegate(new ChatItemDelegate(this));
    m_chatList->setFrameShape(QFrame::NoFrame);
    m_chatList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_chatList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_chatList->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_chatList->setUniformItemSizes(true);
    m_chatList->setMouseTracking(true);
    m_chatList->viewport()->setMouseTracking(true);
    m_chatList->setAccessibleName(tr("Список чатов"));
    layout->addWidget(m_chatList, 1);

    connect(m_searchField, &QLineEdit::textChanged, this, [this](const QString &text) {
        m_chatProxy->setFilterFixedString(text);
    });
    connect(m_chatList->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex &, const QModelIndex &) { onChatSelectionChanged(); });

    m_chatList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_chatList, &QWidget::customContextMenuRequested,
            this, &MainWindow::onChatListMenu);

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
    m_chatStack->addWidget(buildEmptyPane());
    m_chatStack->addWidget(buildConversationPane());
    m_chatStack->setCurrentIndex(0);
    layout->addWidget(m_chatStack, 1);

    return pane;
}

QWidget *MainWindow::buildEmptyPane()
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

QWidget *MainWindow::buildConversationPane()
{
    auto *page = new QWidget;

    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_connectionBanner = makeLabel(tr("Нет соединения. Переподключение…"), "ConnectionBanner",
                                   Qt::AlignCenter);
    m_connectionBanner->hide();
    layout->addWidget(m_connectionBanner);

    m_toast = makeLabel(QString(), "Toast", Qt::AlignCenter);
    m_toast->setWordWrap(true);
    m_toast->hide();
    layout->addWidget(m_toast);

    // Шапка чата.
    auto *header = new QFrame;
    header->setObjectName(QStringLiteral("ChatHeader"));

    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(18, 10, 18, 10);
    headerLayout->setSpacing(12);

    m_headerAvatar = new AvatarWidget;
    m_headerAvatar->setDiameter(40);
    m_headerAvatar->setCursor(Qt::PointingHandCursor);
    m_headerAvatar->installEventFilter(this);
    headerLayout->addWidget(m_headerAvatar);

    auto *titleColumn = new QVBoxLayout;
    titleColumn->setSpacing(0);
    m_headerTitle = makeLabel(QString(), "ChatTitle", Qt::AlignLeft);
    m_headerTitle->setCursor(Qt::PointingHandCursor);
    m_headerTitle->installEventFilter(this);
    m_headerStatus = makeLabel(QString(), "ChatStatus", Qt::AlignLeft);
    titleColumn->addWidget(m_headerTitle);
    titleColumn->addWidget(m_headerStatus);
    headerLayout->addLayout(titleColumn);

    m_membersButton = new QToolButton(header);
    m_membersButton->setText(tr("Участники"));
    m_membersButton->setObjectName(QStringLiteral("LinkButton"));
    m_membersButton->setCursor(Qt::PointingHandCursor);
    m_membersButton->setVisible(false);
    connect(m_membersButton, &QToolButton::clicked, this, &MainWindow::onMembersClicked);
    headerLayout->addWidget(m_membersButton);

    m_voiceButton = new QToolButton(header);
    m_voiceButton->setText(tr("Голос"));
    m_voiceButton->setObjectName(QStringLiteral("LinkButton"));
    m_voiceButton->setCursor(Qt::PointingHandCursor);
    m_voiceButton->setVisible(false);
    connect(m_voiceButton, &QToolButton::clicked, this, &MainWindow::onVoiceClicked);
    headerLayout->addWidget(m_voiceButton);

    m_muteButton = new QToolButton(header);
    m_muteButton->setText(tr("Микрофон"));
    m_muteButton->setObjectName(QStringLiteral("LinkButton"));
    m_muteButton->setCursor(Qt::PointingHandCursor);
    m_muteButton->setVisible(false);
    connect(m_muteButton, &QToolButton::clicked, this, &MainWindow::onMuteClicked);
    headerLayout->addWidget(m_muteButton);

    headerLayout->addStretch(1);

    layout->addWidget(header);

    m_messageView = new MessageView;
    m_messageView->setAccessibleName(tr("Лента сообщений"));
    layout->addWidget(m_messageView, 1);

    m_composer = new ComposerWidget;
    layout->addWidget(m_composer);

    connect(m_composer, &ComposerWidget::attachRequested, this, [this](const QString &path) {
        sendFile(path);
    });
    connect(m_composer, &ComposerWidget::sendRequested, this, &MainWindow::onSendRequested);
    connect(m_messageView, &MessageView::loadOlderRequested, this, [this]() {
        if (m_currentChatId > 0)
            m_chats->loadOlder(m_currentChatId);
    });
    connect(m_messageView, &MessageView::retryRequested, this, [this](const QString &clientMsgId) {
        if (m_currentChatId > 0)
            m_chats->retryFailed(m_currentChatId, clientMsgId);
    });
    connect(m_messageView, &MessageView::attachmentRetryRequested, this,
            [this](qint64 attachmentId) { m_chats->retryAttachment(attachmentId); });
    connect(m_messageView, &MessageView::editRequested, this, &MainWindow::onEditMessage);
    connect(m_messageView, &MessageView::deleteForEveryoneRequested, this,
            &MainWindow::onDeleteMessageForEveryone);
    connect(m_messageView, &MessageView::deleteForMeRequested, this,
            &MainWindow::onDeleteMessageForMe);
    connect(m_messageView, &MessageView::avatarActivated, this, &MainWindow::openProfile);

    connect(m_messageView, &MessageView::imageActivated, this, [this](const QString &filePath) {
        ui::ImageViewerDialog viewer(filePath, this);
        viewer.exec();
    });

    connect(m_messageView, &MessageView::videoActivated, this, [this](const QString &filePath) {
        ui::VideoPlayerDialog player(filePath, this);
        player.exec();
    });

    connect(m_messageView, &MessageView::fileActivated, this,
            [this](const QString &fileName, const QString &filePath) {
                const QString target = QFileDialog::getSaveFileName(
                    this, tr("Сохранить файл"),
                    fileName.isEmpty() ? QFileInfo(filePath).fileName() : fileName);

                if (target.isEmpty())
                    return;

                if (QFileInfo::exists(target) && !QFile::remove(target)) {
                    QMessageBox::warning(this, tr("Не удалось сохранить файл"),
                                         tr("Файл занят или недоступен для записи."));
                    return;
                }

                if (!QFile::copy(filePath, target)) {
                    QMessageBox::warning(this, tr("Не удалось сохранить файл"),
                                         tr("Не удалось скопировать файл в выбранное место."));
                }
            });

    return page;
}

void MainWindow::onEditMessage(qint64 messageId)
{
    if (m_currentChatId <= 0)
        return;

    QString currentText;
    for (const data::Message &message : m_chats->messages(m_currentChatId)) {
        if (message.id == messageId) {
            currentText = message.body;
            break;
        }
    }

    bool ok = false;
    const QString text = QInputDialog::getMultiLineText(
        this, tr("Изменить сообщение"), tr("Новый текст"), currentText, &ok);

    if (!ok || text.trimmed().isEmpty())
        return;

    m_chats->editMessage(m_currentChatId, messageId, text);
}

void MainWindow::onDeleteMessageForEveryone(qint64 messageId)
{
    if (m_currentChatId <= 0)
        return;

    const auto answer = QMessageBox::question(
        this, tr("Удалить у всех"),
        tr("Сообщение исчезнет у всех участников чата. Продолжить?"));
    if (answer == QMessageBox::Yes)
        m_chats->deleteMessageForEveryone(m_currentChatId, messageId);
}

void MainWindow::onDeleteMessageForMe(qint64 messageId)
{
    if (m_currentChatId <= 0)
        return;

    const auto answer = QMessageBox::question(
        this, tr("Удалить у себя"),
        tr("Сообщение будет скрыто только на этом компьютере."));
    if (answer == QMessageBox::Yes)
        m_chats->deleteMessageForMe(m_currentChatId, messageId);
}

void MainWindow::onChatListMenu(const QPoint &position)
{
    const QModelIndex index = m_chatList->indexAt(position);
    if (!index.isValid())
        return;

    const data::ChatSummary summary =
        index.data(ChatListModel::SummaryRole).value<data::ChatSummary>();
    if (summary.chat.id <= 0)
        return;

    const qint64 chatId = summary.chat.id;

    QMenu menu(this);
    QAction *open = menu.addAction(tr("Открыть"));
    menu.addSeparator();
    QAction *clear = menu.addAction(tr("Очистить историю"));
    QAction *remove = menu.addAction(tr("Удалить чат"));

    QAction *chosen = menu.exec(m_chatList->viewport()->mapToGlobal(position));
    if (!chosen)
        return;

    if (chosen == open) {
        openChatById(chatId);
        return;
    }

    if (chosen == clear) {
        const auto answer = QMessageBox::question(
            this, tr("Очистить историю"),
            tr("Удалить все сообщения этого чата на этом компьютере? "
               "У собеседника они останутся."));
        if (answer == QMessageBox::Yes)
            m_chats->clearHistory(chatId);
        return;
    }

    if (chosen != remove)
        return;

    const auto answer = QMessageBox::question(
        this, tr("Удалить чат"),
        tr("Чат исчезнет из списка, локальная история будет удалена. "
           "У собеседника сообщения останутся. Новое сообщение вернёт чат."));
    if (answer != QMessageBox::Yes)
        return;

    if (m_currentChatId == chatId) {
        m_currentChatId = 0;
        if (m_messageView)
            m_messageView->setMessages(QList<data::Message>());
    }

    m_chats->deleteChat(chatId);
}

void MainWindow::onChatsChanged()
{
    m_chatModel->setChats(m_chats->chats());

    if (m_currentChatId > 0) {
        selectChatRow(m_currentChatId);
    } else if (!m_chats->chats().isEmpty()) {
        openChat(m_chats->chats().first().chat.id);
        selectChatRow(m_currentChatId);
    }

    updateHeader();
}

void MainWindow::onMessagesChanged(qint64 chatId)
{
    if (chatId != m_currentChatId)
        return;

    m_messageView->setHasMore(m_chats->hasMore(chatId));
    m_messageView->setMessages(m_chats->messages(chatId));
    m_composer->setInputEnabled(true);

    if (m_pendingScrollMessageId > 0
        && m_messageView->scrollToMessage(m_pendingScrollMessageId)) {
        m_pendingScrollMessageId = 0;
    }
}

void MainWindow::onChatSelectionChanged()
{
    const QModelIndex current = m_chatList->currentIndex();
    if (!current.isValid())
        return;

    const QModelIndex source = m_chatProxy->mapToSource(current);
    const data::ChatSummary summary = m_chatModel->chatAt(source.row());
    if (summary.chat.id > 0)
        openChat(summary.chat.id);
}

void MainWindow::openChat(qint64 chatId)
{
    if (chatId <= 0)
        return;

    if (m_voice && m_voice->state() == app::VoiceService::State::Active
        && m_voice->chatId() != chatId) {
        m_voice->hangup();
    }

    m_currentChatId = chatId;
    m_speakingUserId = 0;
    m_chats->setActiveChat(chatId);

    m_chatStack->setCurrentIndex(1);
    updateHeader();

    m_messageView->setCurrentUserId(m_chats->currentUserId());
    m_messageView->setHasMore(m_chats->hasMore(chatId));
    m_messageView->setLoading(!m_chats->isLoaded(chatId));
    m_messageView->setMessages(m_chats->messages(chatId));

    m_chats->openChat(chatId);
    m_chats->markRead(chatId);

    m_composer->setInputEnabled(true);
    m_composer->setFocus();
}

void MainWindow::selectChatRow(qint64 chatId)
{
    for (int row = 0; row < m_chatProxy->rowCount(); ++row) {
        const QModelIndex proxyIndex = m_chatProxy->index(row, 0);
        const QModelIndex source = m_chatProxy->mapToSource(proxyIndex);
        if (m_chatModel->chatAt(source.row()).chat.id == chatId) {
            if (m_chatList->currentIndex() != proxyIndex)
                m_chatList->setCurrentIndex(proxyIndex);
            return;
        }
    }
}

void MainWindow::updateHeader()
{
    if (m_currentChatId <= 0) {
        m_chatStack->setCurrentIndex(0);
        return;
    }

    const data::ChatSummary summary = m_chats->chat(m_currentChatId);
    if (summary.chat.id <= 0)
        return;

    m_headerAvatar->setIdentity(summary.peerDisplayName, summary.peerHue);
    m_headerTitle->setText(summary.peerDisplayName);

    const bool isGroup = summary.chat.kind == QLatin1String("group");
    if (m_membersButton)
        m_membersButton->setVisible(isGroup);

    QString status;
    if (m_voice && m_voice->chatId() == m_currentChatId
        && m_voice->state() != app::VoiceService::State::Idle) {
        if (m_voice->state() == app::VoiceService::State::Calling)
            status = tr("Звоним…");
        else if (m_voice->state() == app::VoiceService::State::Ringing)
            status = tr("Входящий вызов");
        else if (m_speakingUserId > 0)
            status = tr("Говорит: %1")
                         .arg(summary.chat.kind == QLatin1String("direct")
                                  ? summary.peerDisplayName
                                  : tr("участник"));
        else
            status = m_voice->isMuted() ? tr("Голос: микрофон выключен") : tr("Голос: в эфире");
    } else if (m_typingUserId > 0) {
        status = tr("печатает…");
    } else if (isGroup) {
        const int count = m_groupMemberCount;
        QString countText;
        if (count % 10 == 1 && count % 100 != 11)
            countText = tr("%1 участник").arg(count);
        else if (count % 10 >= 2 && count % 10 <= 4
                 && (count % 100 < 10 || count % 100 >= 20))
            countText = tr("%1 участника").arg(count);
        else
            countText = tr("%1 участников").arg(count);

        status = countText;
        m_chats->loadMembers(m_currentChatId);
    } else {
        status = m_online.value(summary.peerId, false) ? tr("в сети") : tr("не в сети");
    }

    m_headerStatus->setText(status);
    updateVoiceUi();

    if (m_messageView)
        m_messageView->setShowSenderNames(isGroup);
}

void MainWindow::openProfile(qint64 userId)
{
    if (m_currentChatId <= 0) {
        showToast(tr("Сначала откройте чат."));
        return;
    }
    if (userId <= 0)
        return;

    ProfileCardDialog dialog(m_chats, m_currentChatId, userId, m_user.id, this);
    dialog.exec();
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonRelease) {
        if (watched == m_profileAvatar) {
            if (m_currentChatId > 0)
                openProfile(m_user.id);
            return true;
        }

        if (watched == m_headerAvatar || watched == m_headerTitle) {
            const data::ChatSummary summary = m_chats->chat(m_currentChatId);
            if (summary.chat.kind == QLatin1String("direct") && summary.peerId > 0)
                openProfile(summary.peerId);
            return true;
        }
    }

    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::onVoiceClicked()
{
    if (!m_voice || m_currentChatId <= 0)
        return;

    switch (m_voice->state()) {
    case app::VoiceService::State::Idle:
        m_voice->call(m_currentChatId);
        break;
    case app::VoiceService::State::Calling:
        m_voice->cancel(m_currentChatId);
        break;
    case app::VoiceService::State::Active:
        m_voice->hangup();
        break;
    case app::VoiceService::State::Ringing:
        break;
    }

    updateVoiceUi();
    updateHeader();
}

void MainWindow::onMuteClicked()
{
    if (!m_voice)
        return;

    m_voice->setMuted(!m_voice->isMuted());
    updateVoiceUi();
    updateHeader();
}

void MainWindow::updateVoiceUi()
{
    const app::VoiceService::State state =
        m_voice ? m_voice->state() : app::VoiceService::State::Idle;
    const bool inCurrentChat = m_voice && m_voice->chatId() == m_currentChatId;

    if (m_voiceButton) {
        m_voiceButton->setVisible(m_currentChatId > 0);
        if (state == app::VoiceService::State::Calling && inCurrentChat)
            m_voiceButton->setText(tr("Отменить"));
        else if (state == app::VoiceService::State::Active && inCurrentChat)
            m_voiceButton->setText(tr("Завершить голос"));
        else
            m_voiceButton->setText(tr("Голос"));
    }

    if (m_muteButton) {
        const bool active = state == app::VoiceService::State::Active && inCurrentChat;
        m_muteButton->setVisible(active);
        m_muteButton->setText(m_voice && m_voice->isMuted() ? tr("Включить микрофон")
                                                           : tr("Выключить микрофон"));
    }
}

void MainWindow::onIncomingCall(qint64 chatId, qint64 callerId, const QString &callerName)
{
    auto *box = new QMessageBox(this);
    box->setWindowTitle(tr("Входящий вызов"));
    box->setText(tr("%1 звонит.").arg(callerName.isEmpty() ? tr("Собеседник") : callerName));
    box->setIcon(QMessageBox::Information);
    QPushButton *acceptButton = box->addButton(tr("Принять"), QMessageBox::AcceptRole);
    QPushButton *declineButton = box->addButton(tr("Отклонить"), QMessageBox::RejectRole);
    box->setAttribute(Qt::WA_DeleteOnClose);

    connect(acceptButton, &QPushButton::clicked, this, [this, chatId, callerId, box]() {
        box->close();
        m_voice->accept(chatId, callerId);
    });
    connect(declineButton, &QPushButton::clicked, this, [this, chatId, callerId, box]() {
        box->close();
        m_voice->decline(chatId, callerId);
    });

    box->open();
}

void MainWindow::onCallAccepted(qint64 chatId, qint64 userId)
{
    Q_UNUSED(chatId)
    Q_UNUSED(userId)
    showToast(tr("Вызов принят"));
    updateHeader();
}

void MainWindow::onCallDeclined(qint64 chatId, qint64 userId)
{
    Q_UNUSED(chatId)
    Q_UNUSED(userId)
    showToast(tr("Вызов отклонён"));
    updateHeader();
}

void MainWindow::onCallCancelled(qint64 chatId)
{
    Q_UNUSED(chatId)
    showToast(tr("Вызов отменён"));
    updateHeader();
}

void MainWindow::onSendRequested(const QString &text)
{
    if (m_currentChatId <= 0 || text.trimmed().isEmpty())
        return;

    m_chats->sendText(m_currentChatId, text);
}

void MainWindow::sendFile(const QString &filePath)
{
    if (m_currentChatId <= 0 || filePath.isEmpty())
        return;

    m_chats->sendFile(m_currentChatId, filePath);
}

void MainWindow::openSearch()
{
    if (!m_searchDialog) {
        m_searchDialog = new SearchDialog(this);

        connect(m_searchDialog, &SearchDialog::searchRequested, this,
                [this](const QString &query) { m_chats->searchMessages(query); });

        connect(m_searchDialog, &SearchDialog::messageActivated, this,
                [this](qint64 chatId, qint64 messageId) {
                    openChatById(chatId);
                    m_pendingScrollMessageId = messageId;
                    m_messageView->scrollToMessage(messageId);
                });
    }

    m_searchDialog->show();
    m_searchDialog->raise();
    m_searchDialog->activateWindow();
}

QHash<qint64, QString> MainWindow::chatNames() const
{
    QHash<qint64, QString> names;
    for (const data::ChatSummary &summary : m_chats->chats())
        names.insert(summary.chat.id, summary.peerDisplayName);
    return names;
}

void MainWindow::openChatById(qint64 chatId)
{
    if (chatId <= 0)
        return;

    if (m_chats->chat(chatId).chat.id <= 0) {
        m_chats->loadChats();
        return;
    }

    selectChatRow(chatId);
    openChat(chatId);
}

void MainWindow::activate()
{
    showNormal();
    raise();
    activateWindow();
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

void MainWindow::onNewChatClicked()
{
    QMenu menu(this);
    QAction *direct = menu.addAction(tr("Личный чат"));
    QAction *group = menu.addAction(tr("Новая группа…"));

    QAction *chosen = menu.exec(QCursor::pos());
    if (!chosen)
        return;

    if (chosen == group) {
        onCreateGroup();
        return;
    }

    if (chosen != direct)
        return;

    bool ok = false;
    const QString username = QInputDialog::getText(this, tr("Новый чат"),
                                                   tr("Логин собеседника:"),
                                                   QLineEdit::Normal, QString(), &ok);
    if (!ok || username.trimmed().isEmpty())
        return;

    m_chats->createDirect(username.trimmed());
}

void MainWindow::onCreateGroup()
{
    GroupDialog dialog(m_chats, 0, QSet<qint64>(), this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    m_chats->createGroup(dialog.groupTitle(), dialog.selectedIds());
}

void MainWindow::onMembersClicked()
{
    if (m_currentChatId <= 0)
        return;

    ParticipantsDialog dialog(m_chats, m_currentChatId, m_user.id, this);
    dialog.exec();
}

void MainWindow::onTypingChanged(qint64 chatId, qint64 userId, bool typing)
{
    if (chatId != m_currentChatId || userId == m_user.id)
        return;

    m_typingUserId = typing ? userId : 0;

    if (typing)
        m_typingTimer->start();
    else
        m_typingTimer->stop();

    updateHeader();
}

void MainWindow::onPresenceChanged(qint64 userId, bool online)
{
    m_online.insert(userId, online);
    updateHeader();
}

void MainWindow::openSettings()
{
    if (!m_updater)
        m_updater = new app::UpdateService(m_connection ? m_connection : nullptr, this);

    ui::SettingsDialog dialog(m_settings, m_theme, m_billing, m_updater, this);
    dialog.exec();
}

void MainWindow::onError(const QString &message)
{
    showToast(message);
}

void MainWindow::showToast(const QString &text)
{
    if (!m_toast)
        return;

    m_toast->setText(text);
    m_toast->show();

    if (m_toastTimer)
        m_toastTimer->start();
}

void MainWindow::updateConnectionBanner()
{
    if (!m_connectionBanner)
        return;

    const bool connected = m_session && m_session->isAuthenticated();
    m_connectionBanner->setVisible(!connected);
    if (m_composer)
        m_composer->setInputEnabled(connected);
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

# Ядро приложения: общая статическая библиотека для клиента и сервера.
set(MESSENGER_CORE_SOURCES
    app/AppPaths.h              app/AppPaths.cpp
    app/AttachmentService.h     app/AttachmentService.cpp
    app/ChatService.h           app/ChatService.cpp
    app/ClientController.h      app/ClientController.cpp
    app/HistoryStore.h          app/HistoryStore.cpp
    app/Logging.h               app/Logging.cpp
    app/NotificationService.h   app/NotificationService.cpp
    app/SettingsStore.h         app/SettingsStore.cpp
    app/SessionService.h        app/SessionService.cpp
    app/SingleInstance.h        app/SingleInstance.cpp
    app/ThemeManager.h          app/ThemeManager.cpp

    core/Error.h                core/Error.cpp
    core/FrameCodec.h           core/FrameCodec.cpp
    core/Ids.h                  core/Ids.cpp
    core/Limits.h
    core/Protocol.h             core/Protocol.cpp

    data/AttachmentRepository.h data/AttachmentRepository.cpp
    data/ChatCacheRepository.h  data/ChatCacheRepository.cpp
    data/ChatRepository.h       data/ChatRepository.cpp
    data/Database.h             data/Database.cpp
    data/JsonMapping.h          data/JsonMapping.cpp
    data/MessageRepository.h    data/MessageRepository.cpp
    data/Migrations.h           data/Migrations.cpp
    data/Models.h               data/Models.cpp
    data/OutboxRepository.h     data/OutboxRepository.cpp
    data/ReceiptRepository.h    data/ReceiptRepository.cpp
    data/SqlUtil.h
    data/UserRepository.h       data/UserRepository.cpp

    security/PasswordHasher.h   security/PasswordHasher.cpp
    security/Validator.h        security/Validator.cpp

    net/ClientConnection.h      net/ClientConnection.cpp
    net/RequestTracker.h        net/RequestTracker.cpp

    server/AttachmentStore.h    server/AttachmentStore.cpp
    server/ClientSession.h      server/ClientSession.cpp
    server/HandlerResult.h
    server/ServerApp.h          server/ServerApp.cpp
    server/ServerCore.h         server/ServerCore.cpp

    ui/AvatarPainter.h          ui/AvatarPainter.cpp
    ui/AvatarWidget.h           ui/AvatarWidget.cpp
    ui/ChatItemDelegate.h       ui/ChatItemDelegate.cpp
    ui/ChatListModel.h          ui/ChatListModel.cpp
    ui/ComposerWidget.h         ui/ComposerWidget.cpp
    ui/Formatting.h             ui/Formatting.cpp
    ui/ImageViewerDialog.h      ui/ImageViewerDialog.cpp
    ui/LoginWindow.h            ui/LoginWindow.cpp
    ui/MainWindow.h             ui/MainWindow.cpp
    ui/MessageBubble.h          ui/MessageBubble.cpp
    ui/MessageView.h            ui/MessageView.cpp
    ui/SearchDialog.h           ui/SearchDialog.cpp
    ui/VideoPlayerDialog.h      ui/VideoPlayerDialog.cpp
)

add_library(messenger_core STATIC ${MESSENGER_CORE_SOURCES})

target_include_directories(messenger_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(messenger_core PUBLIC
    Qt5::Core
    Qt5::Gui
    Qt5::Widgets
    Qt5::Network
    Qt5::Sql
    Qt5::Multimedia
    Qt5::MultimediaWidgets
    Qt5::Concurrent
    Qt5::Svg
)

target_compile_definitions(messenger_core PUBLIC
    MESSENGER_VERSION="${PROJECT_VERSION}"
)

# ── Клиент ─────────────────────────────────────────────────────────────────
add_executable(MessengerClient WIN32
    main_client.cpp
    ${CMAKE_SOURCE_DIR}/resources/resources.qrc
)

target_link_libraries(MessengerClient PRIVATE messenger_core)

if(WIN32)
    target_link_libraries(MessengerClient PRIVATE Qt5::WinMain)
endif()

# ── Сервер (консольное приложение) ─────────────────────────────────────────
add_executable(MessengerServer
    main_server.cpp
    ${CMAKE_SOURCE_DIR}/resources/resources.qrc
)

target_link_libraries(MessengerServer PRIVATE messenger_core)

set_target_properties(MessengerClient MessengerServer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}"
)

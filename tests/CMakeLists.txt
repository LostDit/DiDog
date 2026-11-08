function(add_messenger_test target source)
    add_executable(${target} ${source}
        ${CMAKE_SOURCE_DIR}/resources/resources.qrc)

    target_link_libraries(${target} PRIVATE messenger_core Qt5::Test)
    target_compile_definitions(${target} PRIVATE TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

    if(WIN32)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${MESSENGER_OPENSSL_DLLS}
                    $<TARGET_FILE_DIR:${target}>)
    endif()

    add_test(NAME ${target} COMMAND ${target})

    if(UNIX AND NOT APPLE)
        set_tests_properties(${target} PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
            TIMEOUT 180)
    else()
        set_tests_properties(${target} PROPERTIES TIMEOUT 180)
    endif()
endfunction()

add_messenger_test(tst_password tst_password.cpp)
add_messenger_test(tst_validation tst_validation.cpp)
add_messenger_test(tst_db tst_db.cpp)
add_messenger_test(tst_repositories tst_repositories.cpp)
add_messenger_test(tst_protocol tst_protocol.cpp)
add_messenger_test(tst_server_smoke tst_server_smoke.cpp)
add_messenger_test(tst_client tst_client.cpp)
add_messenger_test(tst_session tst_session.cpp)
add_messenger_test(tst_ui_flow tst_ui_flow.cpp)
add_messenger_test(tst_history tst_history.cpp)
add_messenger_test(tst_media tst_media.cpp)
add_messenger_test(tst_settings tst_settings.cpp)
add_messenger_test(tst_tls tst_tls.cpp)
add_messenger_test(tst_attachment_history tst_attachment_history.cpp)
add_messenger_test(tst_message_ops tst_message_ops.cpp)
add_messenger_test(tst_group_chat tst_group_chat.cpp)

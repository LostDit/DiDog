function(add_messenger_test target source)
    add_executable(${target} ${source})
    target_link_libraries(${target} PRIVATE messenger_core Qt5::Test)
    add_test(NAME ${target} COMMAND ${target})
    set_tests_properties(${target} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        TIMEOUT 120)
endfunction()

add_messenger_test(tst_password tst_password.cpp)
add_messenger_test(tst_validation tst_validation.cpp)
add_messenger_test(tst_db tst_db.cpp)
add_messenger_test(tst_repositories tst_repositories.cpp)

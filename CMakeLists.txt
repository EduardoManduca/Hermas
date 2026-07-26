cmake_minimum_required(VERSION 3.20)
project(hermas2 C)

option(HERMAS2_SANITIZE "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_library(hermas2_image STATIC src/image.c)
target_include_directories(hermas2_image PUBLIC include)
add_library(hermas2_protocol STATIC src/protocol.c)
target_include_directories(hermas2_protocol PUBLIC include)
add_library(hermas2_runtime STATIC src/runtime.c)
target_include_directories(hermas2_runtime PUBLIC include)
target_link_libraries(hermas2_runtime PUBLIC hermas2_image hermas2_protocol)
add_library(hermas2_journal STATIC src/journal.c)
target_include_directories(hermas2_journal PUBLIC include)
add_library(hermas2_compensation STATIC src/compensation.c)
target_include_directories(hermas2_compensation PUBLIC include)
add_library(hermas2_saga_log STATIC src/saga_log.c)
target_include_directories(hermas2_saga_log PUBLIC include)
add_library(hermas2_saga STATIC src/saga.c)
target_include_directories(hermas2_saga PUBLIC include)
target_link_libraries(hermas2_saga PUBLIC
    hermas2_image hermas2_protocol hermas2_journal hermas2_compensation
    hermas2_saga_log)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(hermas2_image PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_compile_options(hermas2_protocol PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_compile_options(hermas2_runtime PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_compile_options(hermas2_journal PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_compile_options(hermas2_compensation PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_compile_options(hermas2_saga PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_compile_options(hermas2_saga_log PRIVATE -Wall -Wextra -Wpedantic -Werror)
    if(HERMAS2_SANITIZE)
        target_compile_options(hermas2_image PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(hermas2_image PRIVATE -fsanitize=address,undefined)
        target_compile_options(hermas2_protocol PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_compile_options(hermas2_runtime PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_compile_options(hermas2_journal PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_compile_options(hermas2_compensation PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_compile_options(hermas2_saga PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_compile_options(hermas2_saga_log PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer)
    endif()
endif()

if(UNIX)
    target_sources(hermas2_journal PRIVATE src/journal_linux.c)
    target_sources(hermas2_compensation PRIVATE
                   src/compensation_linux.c)
    target_sources(hermas2_saga_log PRIVATE src/saga_log_linux.c)
    add_executable(hermas2_history tools/hermas2_history.c)
    target_link_libraries(hermas2_history PRIVATE hermas2_journal)
    target_compile_options(hermas2_history PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
    if(HERMAS2_SANITIZE)
        target_compile_options(hermas2_history PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(hermas2_history PRIVATE
            -fsanitize=address,undefined)
    endif()
    add_library(hermas2_daemon STATIC src/daemon_linux.c)
    target_sources(hermas2_daemon PRIVATE src/loop_linux.c)
    target_include_directories(hermas2_daemon PUBLIC include)
    target_link_libraries(hermas2_daemon PUBLIC
        hermas2_image hermas2_protocol hermas2_runtime hermas2_journal
        hermas2_compensation)
    add_library(hermas2_edge STATIC src/edge_linux.c)
    target_include_directories(hermas2_edge PUBLIC include)
    target_link_libraries(hermas2_edge PUBLIC hermas2_protocol)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(hermas2_daemon PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(hermas2_edge PRIVATE -Wall -Wextra -Wpedantic -Werror)
        if(HERMAS2_SANITIZE)
            target_compile_options(hermas2_daemon PRIVATE
                -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_compile_options(hermas2_edge PRIVATE
                -fsanitize=address,undefined -fno-omit-frame-pointer)
        endif()
    endif()

    function(add_hermas2_grade_app target app_id)
        add_executable(${target} apps/grade-pipeline/grade_app.c)
        target_link_libraries(${target} PRIVATE hermas2_edge)
        target_compile_definitions(${target} PRIVATE
            HERMAS2_GRADE_APP=${app_id})
        if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(${target} PRIVATE
                -Wall -Wextra -Wpedantic -Werror)
            if(HERMAS2_SANITIZE)
                target_compile_options(${target} PRIVATE
                    -fsanitize=address,undefined -fno-omit-frame-pointer)
                target_link_options(${target} PRIVATE
                    -fsanitize=address,undefined)
            endif()
        endif()
    endfunction()

    add_hermas2_grade_app(hermas2_grade_list 1)
    add_hermas2_grade_app(hermas2_mean_calculator 2)
    add_hermas2_grade_app(hermas2_printer 3)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(hermas2_compensation_tests
                   tests/test_compensation.c)
    target_link_libraries(hermas2_compensation_tests
                          PRIVATE hermas2_compensation)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(hermas2_compensation_tests PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
        if(HERMAS2_SANITIZE)
            target_compile_options(hermas2_compensation_tests PRIVATE
                -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_link_options(hermas2_compensation_tests PRIVATE
                -fsanitize=address,undefined)
        endif()
    endif()
    add_test(NAME hermas2_compensation_tests
             COMMAND hermas2_compensation_tests)
    add_executable(hermas2_journal_tests tests/test_journal.c)
    target_link_libraries(hermas2_journal_tests PRIVATE hermas2_journal)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(hermas2_journal_tests PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
        if(HERMAS2_SANITIZE)
            target_compile_options(hermas2_journal_tests PRIVATE
                -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_link_options(hermas2_journal_tests PRIVATE
                -fsanitize=address,undefined)
        endif()
    endif()
    add_test(NAME hermas2_journal_tests COMMAND hermas2_journal_tests)
    add_executable(hermas2_saga_log_tests tests/test_saga_log.c)
    target_link_libraries(hermas2_saga_log_tests PRIVATE hermas2_saga_log)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(hermas2_saga_log_tests PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
        if(HERMAS2_SANITIZE)
            target_compile_options(hermas2_saga_log_tests PRIVATE
                -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_link_options(hermas2_saga_log_tests PRIVATE
                -fsanitize=address,undefined)
        endif()
    endif()
    add_test(NAME hermas2_saga_log_tests COMMAND hermas2_saga_log_tests)
    if(DEFINED HERMAS2_TEST_SAGA_IMAGE)
        add_executable(hermas2_saga_tests tests/test_saga.c)
        target_link_libraries(hermas2_saga_tests PRIVATE hermas2_saga)
        if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(hermas2_saga_tests PRIVATE
                -Wall -Wextra -Wpedantic -Werror)
            if(HERMAS2_SANITIZE)
                target_compile_options(hermas2_saga_tests PRIVATE
                    -fsanitize=address,undefined -fno-omit-frame-pointer)
                target_link_options(hermas2_saga_tests PRIVATE
                    -fsanitize=address,undefined)
            endif()
        endif()
        add_test(NAME hermas2_saga_tests
                 COMMAND hermas2_saga_tests "${HERMAS2_TEST_SAGA_IMAGE}")
    endif()
    add_executable(hermas2_image_tests tests/test_image.c)
    target_link_libraries(hermas2_image_tests PRIVATE hermas2_image)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(hermas2_image_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
        if(HERMAS2_SANITIZE)
            target_compile_options(hermas2_image_tests PRIVATE
                -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_link_options(hermas2_image_tests PRIVATE -fsanitize=address,undefined)
        endif()
    endif()
    add_executable(hermas2_protocol_tests tests/test_protocol.c)
    target_link_libraries(hermas2_protocol_tests PRIVATE hermas2_protocol)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(hermas2_protocol_tests PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
        if(HERMAS2_SANITIZE)
            target_compile_options(hermas2_protocol_tests PRIVATE
                -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_link_options(hermas2_protocol_tests PRIVATE
                -fsanitize=address,undefined)
        endif()
    endif()
    add_test(NAME hermas2_protocol_tests COMMAND hermas2_protocol_tests)
    if(UNIX)
        add_executable(hermas2_compensation_linux_tests
                       tests/test_compensation_linux.c)
        target_link_libraries(hermas2_compensation_linux_tests
                              PRIVATE hermas2_compensation)
        target_compile_options(hermas2_compensation_linux_tests PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
        if(HERMAS2_SANITIZE)
            target_compile_options(
                hermas2_compensation_linux_tests PRIVATE
                -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_link_options(
                hermas2_compensation_linux_tests PRIVATE
                -fsanitize=address,undefined)
        endif()
        add_test(NAME hermas2_compensation_linux_tests
                 COMMAND hermas2_compensation_linux_tests)
        add_executable(hermas2_journal_linux_tests
                       tests/test_journal_linux.c)
        target_link_libraries(hermas2_journal_linux_tests
                              PRIVATE hermas2_journal)
        target_compile_options(hermas2_journal_linux_tests PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
        if(HERMAS2_SANITIZE)
            target_compile_options(hermas2_journal_linux_tests PRIVATE
                -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_link_options(hermas2_journal_linux_tests PRIVATE
                -fsanitize=address,undefined)
        endif()
        add_test(NAME hermas2_journal_linux_tests
                 COMMAND hermas2_journal_linux_tests)
        add_executable(hermas2_saga_log_linux_tests
                       tests/test_saga_log_linux.c)
        target_link_libraries(hermas2_saga_log_linux_tests
                              PRIVATE hermas2_saga_log)
        target_compile_options(hermas2_saga_log_linux_tests PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
        if(HERMAS2_SANITIZE)
            target_compile_options(hermas2_saga_log_linux_tests PRIVATE
                -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_link_options(hermas2_saga_log_linux_tests PRIVATE
                -fsanitize=address,undefined)
        endif()
        add_test(NAME hermas2_saga_log_linux_tests
                 COMMAND hermas2_saga_log_linux_tests)
        add_executable(hermas2_edge_tests tests/test_edge_linux.c)
        target_link_libraries(hermas2_edge_tests PRIVATE hermas2_edge)
        if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(hermas2_edge_tests PRIVATE
                -Wall -Wextra -Wpedantic -Werror)
            if(HERMAS2_SANITIZE)
                target_compile_options(hermas2_edge_tests PRIVATE
                    -fsanitize=address,undefined -fno-omit-frame-pointer)
                target_link_options(hermas2_edge_tests PRIVATE
                    -fsanitize=address,undefined)
            endif()
        endif()
        add_test(NAME hermas2_edge_tests COMMAND hermas2_edge_tests)
        if(DEFINED HERMAS2_TEST_IMAGE)
            add_executable(hermas2_loop_tests tests/test_loop_linux.c)
            target_link_libraries(hermas2_loop_tests PRIVATE hermas2_daemon)
            add_executable(hermas2_pipeline_tests tests/test_pipeline_linux.c)
            target_link_libraries(hermas2_pipeline_tests PRIVATE
                hermas2_daemon hermas2_edge hermas2_runtime)
            if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
                target_compile_options(hermas2_loop_tests PRIVATE
                    -Wall -Wextra -Wpedantic -Werror)
                target_compile_options(hermas2_pipeline_tests PRIVATE
                    -Wall -Wextra -Wpedantic -Werror)
                if(HERMAS2_SANITIZE)
                    target_compile_options(hermas2_loop_tests PRIVATE
                        -fsanitize=address,undefined -fno-omit-frame-pointer)
                    target_link_options(hermas2_loop_tests PRIVATE
                        -fsanitize=address,undefined)
                    target_compile_options(hermas2_pipeline_tests PRIVATE
                        -fsanitize=address,undefined -fno-omit-frame-pointer)
                    target_link_options(hermas2_pipeline_tests PRIVATE
                        -fsanitize=address,undefined)
                endif()
            endif()
            add_test(NAME hermas2_loop_tests
                     COMMAND hermas2_loop_tests "${HERMAS2_TEST_IMAGE}")
            add_test(NAME hermas2_pipeline_tests
                     COMMAND hermas2_pipeline_tests
                         "${HERMAS2_TEST_IMAGE}"
                         "$<TARGET_FILE:hermas2_grade_list>"
                         "$<TARGET_FILE:hermas2_mean_calculator>"
                         "$<TARGET_FILE:hermas2_printer>")
        endif()
        if(DEFINED HERMAS2_TEST_SAGA_IMAGE)
            add_executable(hermas2_saga_loop_tests
                           tests/test_saga_loop_linux.c)
            target_link_libraries(hermas2_saga_loop_tests PRIVATE
                hermas2_daemon hermas2_compensation)
            target_compile_options(hermas2_saga_loop_tests PRIVATE
                -Wall -Wextra -Wpedantic -Werror)
            if(HERMAS2_SANITIZE)
                target_compile_options(hermas2_saga_loop_tests PRIVATE
                    -fsanitize=address,undefined -fno-omit-frame-pointer)
                target_link_options(hermas2_saga_loop_tests PRIVATE
                    -fsanitize=address,undefined)
            endif()
            add_test(NAME hermas2_saga_loop_tests
                     COMMAND hermas2_saga_loop_tests
                             "${HERMAS2_TEST_SAGA_IMAGE}")
        endif()
    endif()
    if(DEFINED HERMAS2_TEST_IMAGE)
        add_test(NAME hermas2_image_tests
                 COMMAND hermas2_image_tests "${HERMAS2_TEST_IMAGE}"
                         ${HERMAS2_TEST_PARALLEL_IMAGE}
                         ${HERMAS2_TEST_SAGA_IMAGE})
    add_executable(hermas2_runtime_tests tests/test_runtime.c)
        target_link_libraries(hermas2_runtime_tests PRIVATE hermas2_runtime)
        if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(hermas2_runtime_tests PRIVATE
                -Wall -Wextra -Wpedantic -Werror)
            if(HERMAS2_SANITIZE)
                target_compile_options(hermas2_runtime_tests PRIVATE
                    -fsanitize=address,undefined -fno-omit-frame-pointer)
                target_link_options(hermas2_runtime_tests PRIVATE
                    -fsanitize=address,undefined)
            endif()
        endif()
        if(DEFINED HERMAS2_TEST_CHOICE_IMAGE)
            add_test(NAME hermas2_runtime_tests
                     COMMAND hermas2_runtime_tests
                         "${HERMAS2_TEST_IMAGE}"
                         "${HERMAS2_TEST_CHOICE_IMAGE}")
        else()
            add_test(NAME hermas2_runtime_tests
                     COMMAND hermas2_runtime_tests "${HERMAS2_TEST_IMAGE}")
        endif()
    endif()

    if(DEFINED HERMAS2_TEST_PARALLEL_IMAGE AND
       DEFINED HERMAS2_TEST_SAME_APP_IMAGE AND
       DEFINED HERMAS2_TEST_DEADLINE_IMAGE AND
       DEFINED HERMAS2_TEST_NESTED_DEADLINE_IMAGE AND
       DEFINED HERMAS2_TEST_EACH_IMAGE)
        add_executable(hermas2_parallel_runtime_tests
                       tests/test_parallel_runtime.c)
        target_link_libraries(hermas2_parallel_runtime_tests
                              PRIVATE hermas2_runtime)
        target_compile_options(hermas2_parallel_runtime_tests
                               PRIVATE -Wall -Wextra -Wpedantic -Werror)
        if(HERMAS2_SANITIZE)
            target_compile_options(hermas2_parallel_runtime_tests PRIVATE
                                   -fsanitize=address,undefined
                                   -fno-omit-frame-pointer)
            target_link_options(hermas2_parallel_runtime_tests PRIVATE
                                -fsanitize=address,undefined)
        endif()
        add_test(NAME hermas2_parallel_runtime_tests
                 COMMAND hermas2_parallel_runtime_tests
                         "${HERMAS2_TEST_PARALLEL_IMAGE}"
                         "${HERMAS2_TEST_SAME_APP_IMAGE}"
                         "${HERMAS2_TEST_DEADLINE_IMAGE}"
                         "${HERMAS2_TEST_NESTED_DEADLINE_IMAGE}"
                         "${HERMAS2_TEST_EACH_IMAGE}")
    endif()
endif()

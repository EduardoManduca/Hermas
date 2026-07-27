cmake_minimum_required(VERSION 3.20)
project(hermas2 C)
include(GNUInstallDirs)

option(HERMAS2_SANITIZE "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(HERMAS2_BUILD_FUZZERS "Build Clang libFuzzer targets" OFF)

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
add_library(hermas2_result STATIC src/result.c)
target_include_directories(hermas2_result PUBLIC include)
add_library(hermas2_saga_log STATIC src/saga_log.c)
target_include_directories(hermas2_saga_log PUBLIC include)
add_library(hermas2_saga STATIC src/saga.c)
target_include_directories(hermas2_saga PUBLIC include)
target_link_libraries(hermas2_saga PUBLIC
    hermas2_image hermas2_protocol hermas2_journal hermas2_compensation
    hermas2_saga_log)
add_executable(hermas2_image_check tools/hermas2_image_check.c)
target_link_libraries(hermas2_image_check PRIVATE hermas2_image)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(hermas2_image_check PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
    if(HERMAS2_SANITIZE)
        target_compile_options(hermas2_image_check PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(hermas2_image_check PRIVATE
            -fsanitize=address,undefined)
    endif()
endif()

if(HERMAS2_BUILD_FUZZERS)
    if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "HERMAS2_BUILD_FUZZERS requires Clang")
    endif()
    add_executable(hermas2_c_fuzz fuzz/fuzz_c_decoders.c)
    target_link_libraries(hermas2_c_fuzz PRIVATE
        hermas2_image hermas2_protocol hermas2_journal hermas2_result
        hermas2_compensation hermas2_saga_log)
    target_compile_options(hermas2_c_fuzz PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -fsanitize=fuzzer,address,undefined
        -fno-omit-frame-pointer)
    target_link_options(hermas2_c_fuzz PRIVATE
        -fsanitize=fuzzer,address,undefined)
    foreach(target
            hermas2_image hermas2_protocol hermas2_journal
            hermas2_result hermas2_compensation hermas2_saga_log)
        target_compile_options(${target} PRIVATE
            -fsanitize=fuzzer-no-link)
    endforeach()
endif()

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(hermas2_image PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_compile_options(hermas2_protocol PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_compile_options(hermas2_runtime PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_compile_options(hermas2_journal PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_compile_options(hermas2_compensation PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_compile_options(hermas2_result PRIVATE -Wall -Wextra -Wpedantic -Werror)
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
        target_compile_options(hermas2_result PRIVATE
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
    target_sources(hermas2_result PRIVATE src/result_linux.c)
    target_sources(hermas2_saga_log PRIVATE src/saga_log_linux.c)
    target_sources(hermas2_saga PRIVATE src/saga_linux.c)
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
    target_sources(hermas2_daemon PRIVATE
        src/loop_linux.c src/registration_linux.c)
    target_include_directories(hermas2_daemon PUBLIC include)
    target_link_libraries(hermas2_daemon PUBLIC
        hermas2_image hermas2_protocol hermas2_runtime hermas2_journal
        hermas2_compensation hermas2_result hermas2_saga)
    add_library(hermas2_control STATIC src/control.c)
    target_sources(hermas2_control PRIVATE src/control_linux.c)
    target_include_directories(hermas2_control PUBLIC include)
    target_link_libraries(hermas2_control PUBLIC
        hermas2_daemon hermas2_protocol)
    add_library(hermas2_edge STATIC src/edge_linux.c)
    target_include_directories(hermas2_edge PUBLIC include)
    target_link_libraries(hermas2_edge PUBLIC hermas2_protocol)
    add_library(hermas2_client STATIC src/client_linux.c)
    target_include_directories(hermas2_client PUBLIC include)
    target_link_libraries(hermas2_client PUBLIC hermas2_protocol)
    add_library(hermas2_host STATIC src/host_linux.c)
    target_include_directories(hermas2_host PUBLIC include)
    target_link_libraries(hermas2_host PUBLIC hermas2_control)
    add_executable(hermas2d tools/hermas2d.c)
    target_link_libraries(hermas2d PRIVATE hermas2_host)
    add_executable(hermas2_run tools/hermas2_run.c)
    target_link_libraries(hermas2_run PRIVATE hermas2_client hermas2_image)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(hermas2_daemon PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(hermas2_control PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(hermas2_edge PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(hermas2_client PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(hermas2_host PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(hermas2d PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(hermas2_run PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
        if(HERMAS2_SANITIZE)
            target_compile_options(hermas2_daemon PRIVATE
                -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_compile_options(hermas2_control PRIVATE
                -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_compile_options(hermas2_edge PRIVATE
                -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_compile_options(hermas2_client PRIVATE
                -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_compile_options(hermas2_host PRIVATE
                -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_compile_options(hermas2d PRIVATE
                -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_link_options(hermas2d PRIVATE
                -fsanitize=address,undefined)
            target_compile_options(hermas2_run PRIVATE
                -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_link_options(hermas2_run PRIVATE
                -fsanitize=address,undefined)
        endif()
    endif()

    function(add_hermas2_edge_app target)
        add_executable(${target} ${ARGN})
        target_link_libraries(${target} PRIVATE hermas2_edge)
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

    add_hermas2_edge_app(
        hermas2_grade_list
        apps/grade-pipeline/grade_list_app.c
        apps/example_app.c)
    add_hermas2_edge_app(
        hermas2_mean_calculator
        apps/grade-pipeline/mean_calculator_app.c
        apps/example_app.c)
    add_hermas2_edge_app(
        hermas2_printer
        apps/grade-pipeline/printer_app.c
        apps/example_app.c)
    add_hermas2_edge_app(
        hermas2_discount
        apps/order-total/discount_app.c
        apps/example_app.c)
    add_hermas2_edge_app(
        hermas2_tax
        apps/order-total/tax_app.c
        apps/example_app.c)
    add_hermas2_edge_app(
        hermas2_receipt
        apps/order-total/receipt_app.c
        apps/example_app.c)
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
    add_executable(hermas2_result_tests tests/test_result.c)
    target_link_libraries(hermas2_result_tests PRIVATE hermas2_result)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(hermas2_result_tests PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
        if(HERMAS2_SANITIZE)
            target_compile_options(hermas2_result_tests PRIVATE
                -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_link_options(hermas2_result_tests PRIVATE
                -fsanitize=address,undefined)
        endif()
    endif()
    add_test(NAME hermas2_result_tests COMMAND hermas2_result_tests)
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
        add_executable(hermas2_result_linux_tests
                       tests/test_result_linux.c)
        target_link_libraries(hermas2_result_linux_tests
                              PRIVATE hermas2_result)
        target_compile_options(hermas2_result_linux_tests PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
        if(HERMAS2_SANITIZE)
            target_compile_options(hermas2_result_linux_tests PRIVATE
                -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_link_options(hermas2_result_linux_tests PRIVATE
                -fsanitize=address,undefined)
        endif()
        add_test(NAME hermas2_result_linux_tests
                 COMMAND hermas2_result_linux_tests)
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
        add_executable(hermas2_client_tests tests/test_client_linux.c)
        target_link_libraries(hermas2_client_tests PRIVATE
            hermas2_client)
        target_compile_options(hermas2_client_tests PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
        if(HERMAS2_SANITIZE)
            target_compile_options(hermas2_client_tests PRIVATE
                -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_link_options(hermas2_client_tests PRIVATE
                -fsanitize=address,undefined)
        endif()
        add_test(NAME hermas2_client_tests COMMAND hermas2_client_tests)
        add_executable(hermas2_registration_server_tests
                       tests/test_registration_server_linux.c)
        target_link_libraries(hermas2_registration_server_tests PRIVATE
            hermas2_daemon)
        target_compile_options(hermas2_registration_server_tests PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
        if(HERMAS2_SANITIZE)
            target_compile_options(
                hermas2_registration_server_tests PRIVATE
                -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_link_options(
                hermas2_registration_server_tests PRIVATE
                -fsanitize=address,undefined)
        endif()
        if(DEFINED HERMAS2_TEST_SAGA_IMAGE)
            add_test(NAME hermas2_registration_server_tests
                     COMMAND hermas2_registration_server_tests
                             "${HERMAS2_TEST_SAGA_IMAGE}")
        endif()
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
            add_executable(hermas2d_tests
                           tests/test_hermas2d_linux.c)
            add_dependencies(hermas2d_tests
                hermas2d hermas2_run hermas2_grade_list
                hermas2_mean_calculator hermas2_printer)
            target_link_libraries(hermas2d_tests PRIVATE
                hermas2_client hermas2_journal)
            target_compile_options(hermas2d_tests PRIVATE
                -Wall -Wextra -Wpedantic -Werror)
            if(HERMAS2_SANITIZE)
                target_compile_options(hermas2d_tests PRIVATE
                    -fsanitize=address,undefined
                    -fno-omit-frame-pointer)
                target_link_options(hermas2d_tests PRIVATE
                    -fsanitize=address,undefined)
            endif()
            add_test(NAME hermas2d_tests
                     COMMAND hermas2d_tests
                         "${HERMAS2_TEST_IMAGE}"
                         "$<TARGET_FILE:hermas2d>"
                         "$<TARGET_FILE:hermas2_run>"
                         "$<TARGET_FILE:hermas2_grade_list>"
                         "$<TARGET_FILE:hermas2_mean_calculator>"
                         "$<TARGET_FILE:hermas2_printer>")
        endif()
        if(DEFINED HERMAS2_TEST_SAGA_IMAGE)
            add_executable(hermas2_host_recovery_tests
                           tests/test_host_recovery_linux.c)
            target_link_libraries(hermas2_host_recovery_tests PRIVATE
                hermas2_host)
            target_compile_options(hermas2_host_recovery_tests PRIVATE
                -Wall -Wextra -Wpedantic -Werror)
            if(HERMAS2_SANITIZE)
                target_compile_options(
                    hermas2_host_recovery_tests PRIVATE
                    -fsanitize=address,undefined
                    -fno-omit-frame-pointer)
                target_link_options(
                    hermas2_host_recovery_tests PRIVATE
                    -fsanitize=address,undefined)
            endif()
            add_test(NAME hermas2_host_recovery_tests
                     COMMAND hermas2_host_recovery_tests
                             "${HERMAS2_TEST_SAGA_IMAGE}")
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
            add_executable(hermas2_control_tests
                           tests/test_control_linux.c)
            target_link_libraries(hermas2_control_tests PRIVATE
                hermas2_control)
            target_compile_options(hermas2_control_tests PRIVATE
                -Wall -Wextra -Wpedantic -Werror)
            if(HERMAS2_SANITIZE)
                target_compile_options(hermas2_control_tests PRIVATE
                    -fsanitize=address,undefined
                    -fno-omit-frame-pointer)
                target_link_options(hermas2_control_tests PRIVATE
                    -fsanitize=address,undefined)
            endif()
            add_test(NAME hermas2_control_tests
                     COMMAND hermas2_control_tests
                             "${HERMAS2_TEST_SAGA_IMAGE}")
            add_executable(hermas2_control_server_tests
                           tests/test_control_server_linux.c)
            target_link_libraries(hermas2_control_server_tests PRIVATE
                hermas2_control)
            target_compile_options(hermas2_control_server_tests PRIVATE
                -Wall -Wextra -Wpedantic -Werror)
            if(HERMAS2_SANITIZE)
                target_compile_options(
                    hermas2_control_server_tests PRIVATE
                    -fsanitize=address,undefined
                    -fno-omit-frame-pointer)
                target_link_options(
                    hermas2_control_server_tests PRIVATE
                    -fsanitize=address,undefined)
            endif()
            add_test(NAME hermas2_control_server_tests
                     COMMAND hermas2_control_server_tests
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

install(TARGETS
        hermas2_image hermas2_protocol hermas2_runtime hermas2_journal
        hermas2_compensation hermas2_result hermas2_saga_log
        hermas2_saga hermas2_image_check
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY include/hermas2
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

if(UNIX)
    install(TARGETS
            hermas2_daemon hermas2_control hermas2_edge hermas2_client
            hermas2_host hermas2d hermas2_run hermas2_history
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(TARGETS
            hermas2_grade_list hermas2_mean_calculator hermas2_printer
            hermas2_discount hermas2_tax hermas2_receipt
        RUNTIME DESTINATION
            ${CMAKE_INSTALL_LIBEXECDIR}/hermas/examples)
    install(PROGRAMS tools/quickstart.sh tools/test_order_total.sh
        DESTINATION ${CMAKE_INSTALL_DATADIR}/hermas)
endif()

install(FILES
        README.md CONTRIBUTING.md SECURITY.md CODE_OF_CONDUCT.md ROADMAP.md
        LICENSE-MIT LICENSE-APACHE
        docs/QUICKSTART.md docs/APP_AUTHOR_GUIDE.md
        docs/COMPATIBILITY.md docs/THREAT_MODEL.md
        docs/CRASH_RECOVERY_MATRIX.md docs/FEATURE_ADMISSION.md
        docs/RELEASE_CHECKLIST.md
    DESTINATION ${CMAKE_INSTALL_DOCDIR})

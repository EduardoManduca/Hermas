cmake_minimum_required(VERSION 3.20)
project(hermas C)
include(GNUInstallDirs)

option(HERMAS_SANITIZE "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(HERMAS_BUILD_FUZZERS "Build Clang libFuzzer targets" OFF)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_library(hermas_image STATIC src/image.c)
target_include_directories(hermas_image PUBLIC include)
add_library(hermas_protocol STATIC src/protocol.c)
target_include_directories(hermas_protocol PUBLIC include)
add_library(hermas_runtime STATIC src/runtime.c)
target_include_directories(hermas_runtime PUBLIC include)
target_link_libraries(hermas_runtime PUBLIC hermas_image hermas_protocol)
add_library(hermas_journal STATIC src/journal.c)
target_include_directories(hermas_journal PUBLIC include)
add_library(hermas_compensation STATIC src/compensation.c)
target_include_directories(hermas_compensation PUBLIC include)
add_library(hermas_result STATIC src/result.c)
target_include_directories(hermas_result PUBLIC include)
add_library(hermas_saga_log STATIC src/saga_log.c)
target_include_directories(hermas_saga_log PUBLIC include)
add_library(hermas_saga STATIC src/saga.c)
target_include_directories(hermas_saga PUBLIC include)
target_link_libraries(hermas_saga PUBLIC
    hermas_image hermas_protocol hermas_journal hermas_compensation
    hermas_saga_log)
add_executable(hermas_image_check tools/hermas_image_check.c)
target_link_libraries(hermas_image_check PRIVATE hermas_image)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(hermas_image_check PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
    if(HERMAS_SANITIZE)
        target_compile_options(hermas_image_check PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(hermas_image_check PRIVATE
            -fsanitize=address,undefined)
    endif()
endif()

if(HERMAS_BUILD_FUZZERS)
    if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "HERMAS_BUILD_FUZZERS requires Clang")
    endif()
    add_executable(hermas_c_fuzz fuzz/fuzz_c_decoders.c)
    target_link_libraries(hermas_c_fuzz PRIVATE
        hermas_image hermas_protocol hermas_journal hermas_result
        hermas_compensation hermas_saga_log)
    target_compile_options(hermas_c_fuzz PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -fsanitize=fuzzer,address,undefined
        -fno-omit-frame-pointer)
    target_link_options(hermas_c_fuzz PRIVATE
        -fsanitize=fuzzer,address,undefined)
    foreach(target
            hermas_image hermas_protocol hermas_journal
            hermas_result hermas_compensation hermas_saga_log)
        target_compile_options(${target} PRIVATE
            -fsanitize=fuzzer-no-link)
    endforeach()
endif()

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(hermas_image PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_compile_options(hermas_protocol PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_compile_options(hermas_runtime PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_compile_options(hermas_journal PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_compile_options(hermas_compensation PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_compile_options(hermas_result PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_compile_options(hermas_saga PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_compile_options(hermas_saga_log PRIVATE -Wall -Wextra -Wpedantic -Werror)
    if(HERMAS_SANITIZE)
        target_compile_options(hermas_image PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(hermas_image PRIVATE -fsanitize=address,undefined)
        target_compile_options(hermas_protocol PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_compile_options(hermas_runtime PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_compile_options(hermas_journal PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_compile_options(hermas_compensation PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_compile_options(hermas_result PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_compile_options(hermas_saga PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_compile_options(hermas_saga_log PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer)
    endif()
endif()

if(UNIX)
    add_library(hermas_workspace STATIC src/workspace_linux.c)
    target_include_directories(hermas_workspace PUBLIC include)
    target_link_libraries(hermas_workspace PUBLIC
        hermas_image hermas_journal)
    target_compile_options(hermas_workspace PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
    if(HERMAS_SANITIZE)
        target_compile_options(hermas_workspace PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer)
    endif()
    target_sources(hermas_journal PRIVATE src/journal_linux.c)
    target_sources(hermas_compensation PRIVATE
                   src/compensation_linux.c)
    target_sources(hermas_result PRIVATE src/result_linux.c)
    target_sources(hermas_saga_log PRIVATE src/saga_log_linux.c)
    target_sources(hermas_saga PRIVATE src/saga_linux.c)
    add_executable(hermas_history tools/hermas_history.c)
    target_link_libraries(hermas_history PRIVATE
        hermas_journal hermas_workspace)
    target_compile_options(hermas_history PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
    if(HERMAS_SANITIZE)
        target_compile_options(hermas_history PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(hermas_history PRIVATE
            -fsanitize=address,undefined)
    endif()
    add_library(hermas_daemon STATIC src/daemon_linux.c)
    target_sources(hermas_daemon PRIVATE
        src/loop_linux.c src/registration_linux.c)
    target_include_directories(hermas_daemon PUBLIC include)
    target_link_libraries(hermas_daemon PUBLIC
        hermas_image hermas_protocol hermas_runtime hermas_journal
        hermas_compensation hermas_result hermas_saga)
    add_library(hermas_control STATIC src/control.c)
    target_sources(hermas_control PRIVATE src/control_linux.c)
    target_include_directories(hermas_control PUBLIC include)
    target_link_libraries(hermas_control PUBLIC
        hermas_daemon hermas_protocol)
    add_library(hermas_edge STATIC src/edge_linux.c)
    target_include_directories(hermas_edge PUBLIC include)
    target_link_libraries(hermas_edge PUBLIC hermas_protocol)
    add_library(hermas_client STATIC src/client_linux.c)
    target_include_directories(hermas_client PUBLIC include)
    target_link_libraries(hermas_client PUBLIC hermas_protocol)
    add_library(hermas_host STATIC src/host_linux.c)
    target_include_directories(hermas_host PUBLIC include)
    target_link_libraries(hermas_host PUBLIC hermas_control)
    add_executable(hermasd tools/hermasd.c)
    target_link_libraries(hermasd PRIVATE
        hermas_host hermas_workspace)
    add_executable(hermas_run tools/hermas_run.c)
    target_link_libraries(hermas_run PRIVATE
        hermas_client hermas_image hermas_workspace)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(hermas_daemon PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(hermas_control PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(hermas_edge PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(hermas_client PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(hermas_host PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(hermasd PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(hermas_run PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
        if(HERMAS_SANITIZE)
            target_compile_options(hermas_daemon PRIVATE
                -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_compile_options(hermas_control PRIVATE
                -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_compile_options(hermas_edge PRIVATE
                -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_compile_options(hermas_client PRIVATE
                -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_compile_options(hermas_host PRIVATE
                -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_compile_options(hermasd PRIVATE
                -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_link_options(hermasd PRIVATE
                -fsanitize=address,undefined)
            target_compile_options(hermas_run PRIVATE
                -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_link_options(hermas_run PRIVATE
                -fsanitize=address,undefined)
        endif()
    endif()

    function(add_hermas_edge_app target)
        add_executable(${target} ${ARGN})
        target_link_libraries(${target} PRIVATE
            hermas_edge hermas_workspace)
        if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(${target} PRIVATE
                -Wall -Wextra -Wpedantic -Werror)
            if(HERMAS_SANITIZE)
                target_compile_options(${target} PRIVATE
                    -fsanitize=address,undefined -fno-omit-frame-pointer)
                target_link_options(${target} PRIVATE
                    -fsanitize=address,undefined)
            endif()
        endif()
    endfunction()

    add_hermas_edge_app(
        hermas_grade_list
        apps/grade-pipeline/grade_list_app.c
        apps/example_app.c)
    add_hermas_edge_app(
        hermas_mean_calculator
        apps/grade-pipeline/mean_calculator_app.c
        apps/example_app.c)
    add_hermas_edge_app(
        hermas_printer
        apps/grade-pipeline/printer_app.c
        apps/example_app.c)
    add_hermas_edge_app(
        hermas_discount
        apps/order-total/discount_app.c
        apps/example_app.c)
    add_hermas_edge_app(
        hermas_tax
        apps/order-total/tax_app.c
        apps/example_app.c)
    add_hermas_edge_app(
        hermas_receipt
        apps/order-total/receipt_app.c
        apps/example_app.c)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(hermas_compensation_tests
                   tests/test_compensation.c)
    target_link_libraries(hermas_compensation_tests
                          PRIVATE hermas_compensation)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(hermas_compensation_tests PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
        if(HERMAS_SANITIZE)
            target_compile_options(hermas_compensation_tests PRIVATE
                -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_link_options(hermas_compensation_tests PRIVATE
                -fsanitize=address,undefined)
        endif()
    endif()
    add_test(NAME hermas_compensation_tests
             COMMAND hermas_compensation_tests)
    add_executable(hermas_result_tests tests/test_result.c)
    target_link_libraries(hermas_result_tests PRIVATE hermas_result)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(hermas_result_tests PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
        if(HERMAS_SANITIZE)
            target_compile_options(hermas_result_tests PRIVATE
                -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_link_options(hermas_result_tests PRIVATE
                -fsanitize=address,undefined)
        endif()
    endif()
    add_test(NAME hermas_result_tests COMMAND hermas_result_tests)
    add_executable(hermas_journal_tests tests/test_journal.c)
    target_link_libraries(hermas_journal_tests PRIVATE hermas_journal)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(hermas_journal_tests PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
        if(HERMAS_SANITIZE)
            target_compile_options(hermas_journal_tests PRIVATE
                -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_link_options(hermas_journal_tests PRIVATE
                -fsanitize=address,undefined)
        endif()
    endif()
    add_test(NAME hermas_journal_tests COMMAND hermas_journal_tests)
    add_executable(hermas_saga_log_tests tests/test_saga_log.c)
    target_link_libraries(hermas_saga_log_tests PRIVATE hermas_saga_log)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(hermas_saga_log_tests PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
        if(HERMAS_SANITIZE)
            target_compile_options(hermas_saga_log_tests PRIVATE
                -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_link_options(hermas_saga_log_tests PRIVATE
                -fsanitize=address,undefined)
        endif()
    endif()
    add_test(NAME hermas_saga_log_tests COMMAND hermas_saga_log_tests)
    if(DEFINED HERMAS_TEST_SAGA_IMAGE)
        add_executable(hermas_saga_tests tests/test_saga.c)
        target_link_libraries(hermas_saga_tests PRIVATE hermas_saga)
        if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(hermas_saga_tests PRIVATE
                -Wall -Wextra -Wpedantic -Werror)
            if(HERMAS_SANITIZE)
                target_compile_options(hermas_saga_tests PRIVATE
                    -fsanitize=address,undefined -fno-omit-frame-pointer)
                target_link_options(hermas_saga_tests PRIVATE
                    -fsanitize=address,undefined)
            endif()
        endif()
        add_test(NAME hermas_saga_tests
                 COMMAND hermas_saga_tests "${HERMAS_TEST_SAGA_IMAGE}")
    endif()
    add_executable(hermas_image_tests tests/test_image.c)
    target_link_libraries(hermas_image_tests PRIVATE hermas_image)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(hermas_image_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
        if(HERMAS_SANITIZE)
            target_compile_options(hermas_image_tests PRIVATE
                -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_link_options(hermas_image_tests PRIVATE -fsanitize=address,undefined)
        endif()
    endif()
    add_executable(hermas_protocol_tests tests/test_protocol.c)
    target_link_libraries(hermas_protocol_tests PRIVATE hermas_protocol)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(hermas_protocol_tests PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
        if(HERMAS_SANITIZE)
            target_compile_options(hermas_protocol_tests PRIVATE
                -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_link_options(hermas_protocol_tests PRIVATE
                -fsanitize=address,undefined)
        endif()
    endif()
    add_test(NAME hermas_protocol_tests COMMAND hermas_protocol_tests)
    if(UNIX)
        add_executable(hermas_workspace_tests
                       tests/test_workspace_linux.c)
        target_link_libraries(hermas_workspace_tests
                              PRIVATE hermas_workspace)
        target_compile_options(hermas_workspace_tests PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
        if(HERMAS_SANITIZE)
            target_compile_options(hermas_workspace_tests PRIVATE
                -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_link_options(hermas_workspace_tests PRIVATE
                -fsanitize=address,undefined)
        endif()
        if(DEFINED HERMAS_TEST_IMAGE
           AND DEFINED HERMAS_TEST_PARALLEL_IMAGE
           AND DEFINED HERMAS_TEST_EACH_IMAGE)
            add_test(NAME hermas_workspace_tests
                     COMMAND hermas_workspace_tests
                             "${HERMAS_TEST_IMAGE}")
        else()
            add_test(NAME hermas_workspace_tests
                     COMMAND hermas_workspace_tests)
        endif()
        add_executable(hermas_compensation_linux_tests
                       tests/test_compensation_linux.c)
        target_link_libraries(hermas_compensation_linux_tests
                              PRIVATE hermas_compensation)
        target_compile_options(hermas_compensation_linux_tests PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
        if(HERMAS_SANITIZE)
            target_compile_options(
                hermas_compensation_linux_tests PRIVATE
                -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_link_options(
                hermas_compensation_linux_tests PRIVATE
                -fsanitize=address,undefined)
        endif()
        add_test(NAME hermas_compensation_linux_tests
                 COMMAND hermas_compensation_linux_tests)
        add_executable(hermas_result_linux_tests
                       tests/test_result_linux.c)
        target_link_libraries(hermas_result_linux_tests
                              PRIVATE hermas_result)
        target_compile_options(hermas_result_linux_tests PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
        if(HERMAS_SANITIZE)
            target_compile_options(hermas_result_linux_tests PRIVATE
                -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_link_options(hermas_result_linux_tests PRIVATE
                -fsanitize=address,undefined)
        endif()
        add_test(NAME hermas_result_linux_tests
                 COMMAND hermas_result_linux_tests)
        add_executable(hermas_journal_linux_tests
                       tests/test_journal_linux.c)
        target_link_libraries(hermas_journal_linux_tests
                              PRIVATE hermas_journal)
        target_compile_options(hermas_journal_linux_tests PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
        if(HERMAS_SANITIZE)
            target_compile_options(hermas_journal_linux_tests PRIVATE
                -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_link_options(hermas_journal_linux_tests PRIVATE
                -fsanitize=address,undefined)
        endif()
        add_test(NAME hermas_journal_linux_tests
                 COMMAND hermas_journal_linux_tests)
        find_package(Python3 COMPONENTS Interpreter QUIET)
        if(Python3_Interpreter_FOUND)
            add_test(
                NAME hermas_history_json_tests
                COMMAND "${Python3_EXECUTABLE}"
                        "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_history_json.py"
                        contract
                        "$<TARGET_FILE:hermas_history>")
            add_test(
                NAME hermas_daemon_capabilities_tests
                COMMAND "${Python3_EXECUTABLE}"
                        "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_daemon_capabilities.py"
                        "$<TARGET_FILE:hermasd>")
        endif()
        add_executable(hermas_saga_log_linux_tests
                       tests/test_saga_log_linux.c)
        target_link_libraries(hermas_saga_log_linux_tests
                              PRIVATE hermas_saga_log)
        target_compile_options(hermas_saga_log_linux_tests PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
        if(HERMAS_SANITIZE)
            target_compile_options(hermas_saga_log_linux_tests PRIVATE
                -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_link_options(hermas_saga_log_linux_tests PRIVATE
                -fsanitize=address,undefined)
        endif()
        add_test(NAME hermas_saga_log_linux_tests
                 COMMAND hermas_saga_log_linux_tests)
        add_executable(hermas_edge_tests tests/test_edge_linux.c)
        target_link_libraries(hermas_edge_tests PRIVATE hermas_edge)
        if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(hermas_edge_tests PRIVATE
                -Wall -Wextra -Wpedantic -Werror)
            if(HERMAS_SANITIZE)
                target_compile_options(hermas_edge_tests PRIVATE
                    -fsanitize=address,undefined -fno-omit-frame-pointer)
                target_link_options(hermas_edge_tests PRIVATE
                    -fsanitize=address,undefined)
            endif()
        endif()
        add_test(NAME hermas_edge_tests COMMAND hermas_edge_tests)
        add_executable(hermas_client_tests tests/test_client_linux.c)
        target_link_libraries(hermas_client_tests PRIVATE
            hermas_client)
        target_compile_options(hermas_client_tests PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
        if(HERMAS_SANITIZE)
            target_compile_options(hermas_client_tests PRIVATE
                -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_link_options(hermas_client_tests PRIVATE
                -fsanitize=address,undefined)
        endif()
        add_test(NAME hermas_client_tests COMMAND hermas_client_tests)
        add_executable(hermas_registration_server_tests
                       tests/test_registration_server_linux.c)
        target_link_libraries(hermas_registration_server_tests PRIVATE
            hermas_daemon)
        target_compile_options(hermas_registration_server_tests PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
        if(HERMAS_SANITIZE)
            target_compile_options(
                hermas_registration_server_tests PRIVATE
                -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_link_options(
                hermas_registration_server_tests PRIVATE
                -fsanitize=address,undefined)
        endif()
        if(DEFINED HERMAS_TEST_SAGA_IMAGE)
            add_test(NAME hermas_registration_server_tests
                     COMMAND hermas_registration_server_tests
                             "${HERMAS_TEST_SAGA_IMAGE}")
        endif()
        if(DEFINED HERMAS_TEST_IMAGE)
            add_executable(hermas_loop_tests tests/test_loop_linux.c)
            target_link_libraries(hermas_loop_tests PRIVATE hermas_daemon)
            add_executable(hermas_pipeline_tests tests/test_pipeline_linux.c)
            target_link_libraries(hermas_pipeline_tests PRIVATE
                hermas_daemon hermas_edge hermas_runtime)
            if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
                target_compile_options(hermas_loop_tests PRIVATE
                    -Wall -Wextra -Wpedantic -Werror)
                target_compile_options(hermas_pipeline_tests PRIVATE
                    -Wall -Wextra -Wpedantic -Werror)
                if(HERMAS_SANITIZE)
                    target_compile_options(hermas_loop_tests PRIVATE
                        -fsanitize=address,undefined -fno-omit-frame-pointer)
                    target_link_options(hermas_loop_tests PRIVATE
                        -fsanitize=address,undefined)
                    target_compile_options(hermas_pipeline_tests PRIVATE
                        -fsanitize=address,undefined -fno-omit-frame-pointer)
                    target_link_options(hermas_pipeline_tests PRIVATE
                        -fsanitize=address,undefined)
                endif()
            endif()
            add_test(NAME hermas_loop_tests
                     COMMAND hermas_loop_tests
                         "${HERMAS_TEST_IMAGE}"
                         "${HERMAS_TEST_PARALLEL_IMAGE}"
                         "${HERMAS_TEST_EACH_IMAGE}")
            add_test(NAME hermas_pipeline_tests
                     COMMAND hermas_pipeline_tests
                         "${HERMAS_TEST_IMAGE}"
                         "$<TARGET_FILE:hermas_grade_list>"
                         "$<TARGET_FILE:hermas_mean_calculator>"
                         "$<TARGET_FILE:hermas_printer>")
            add_executable(hermasd_tests
                           tests/test_hermasd_linux.c)
            add_dependencies(hermasd_tests
                hermasd hermas_run hermas_grade_list
                hermas_mean_calculator hermas_printer)
            target_link_libraries(hermasd_tests PRIVATE
                hermas_client hermas_journal hermas_host)
            target_compile_options(hermasd_tests PRIVATE
                -Wall -Wextra -Wpedantic -Werror)
            if(HERMAS_SANITIZE)
                target_compile_options(hermasd_tests PRIVATE
                    -fsanitize=address,undefined
                    -fno-omit-frame-pointer)
                target_link_options(hermasd_tests PRIVATE
                    -fsanitize=address,undefined)
            endif()
            add_test(NAME hermasd_tests
                     COMMAND hermasd_tests
                         "${HERMAS_TEST_IMAGE}"
                         "$<TARGET_FILE:hermasd>"
                         "$<TARGET_FILE:hermas_run>"
                         "$<TARGET_FILE:hermas_grade_list>"
                         "$<TARGET_FILE:hermas_mean_calculator>"
                         "$<TARGET_FILE:hermas_printer>"
                         "${HERMAS_TEST_PARALLEL_IMAGE}"
                         "${HERMAS_TEST_EACH_IMAGE}")
        endif()
        if(DEFINED HERMAS_TEST_SAGA_IMAGE)
            add_executable(hermas_host_recovery_tests
                           tests/test_host_recovery_linux.c)
            target_link_libraries(hermas_host_recovery_tests PRIVATE
                hermas_host)
            target_compile_options(hermas_host_recovery_tests PRIVATE
                -Wall -Wextra -Wpedantic -Werror)
            if(HERMAS_SANITIZE)
                target_compile_options(
                    hermas_host_recovery_tests PRIVATE
                    -fsanitize=address,undefined
                    -fno-omit-frame-pointer)
                target_link_options(
                    hermas_host_recovery_tests PRIVATE
                    -fsanitize=address,undefined)
            endif()
            add_test(NAME hermas_host_recovery_tests
                     COMMAND hermas_host_recovery_tests
                             "${HERMAS_TEST_SAGA_IMAGE}"
                             "${HERMAS_TEST_EACH_IMAGE}")
            add_executable(hermas_saga_loop_tests
                           tests/test_saga_loop_linux.c)
            target_link_libraries(hermas_saga_loop_tests PRIVATE
                hermas_daemon hermas_compensation)
            target_compile_options(hermas_saga_loop_tests PRIVATE
                -Wall -Wextra -Wpedantic -Werror)
            if(HERMAS_SANITIZE)
                target_compile_options(hermas_saga_loop_tests PRIVATE
                    -fsanitize=address,undefined -fno-omit-frame-pointer)
                target_link_options(hermas_saga_loop_tests PRIVATE
                    -fsanitize=address,undefined)
            endif()
            add_test(NAME hermas_saga_loop_tests
                     COMMAND hermas_saga_loop_tests
                             "${HERMAS_TEST_SAGA_IMAGE}")
            add_executable(hermas_control_tests
                           tests/test_control_linux.c)
            target_link_libraries(hermas_control_tests PRIVATE
                hermas_control)
            target_compile_options(hermas_control_tests PRIVATE
                -Wall -Wextra -Wpedantic -Werror)
            if(HERMAS_SANITIZE)
                target_compile_options(hermas_control_tests PRIVATE
                    -fsanitize=address,undefined
                    -fno-omit-frame-pointer)
                target_link_options(hermas_control_tests PRIVATE
                    -fsanitize=address,undefined)
            endif()
            add_test(NAME hermas_control_tests
                     COMMAND hermas_control_tests
                             "${HERMAS_TEST_SAGA_IMAGE}")
            add_executable(hermas_control_server_tests
                           tests/test_control_server_linux.c)
            target_link_libraries(hermas_control_server_tests PRIVATE
                hermas_control)
            target_compile_options(hermas_control_server_tests PRIVATE
                -Wall -Wextra -Wpedantic -Werror)
            if(HERMAS_SANITIZE)
                target_compile_options(
                    hermas_control_server_tests PRIVATE
                    -fsanitize=address,undefined
                    -fno-omit-frame-pointer)
                target_link_options(
                    hermas_control_server_tests PRIVATE
                    -fsanitize=address,undefined)
            endif()
            add_test(NAME hermas_control_server_tests
                     COMMAND hermas_control_server_tests
                             "${HERMAS_TEST_SAGA_IMAGE}")
        endif()
    endif()
    if(DEFINED HERMAS_TEST_IMAGE)
        add_test(NAME hermas_image_tests
                 COMMAND hermas_image_tests "${HERMAS_TEST_IMAGE}"
                         ${HERMAS_TEST_PARALLEL_IMAGE}
                         ${HERMAS_TEST_SAGA_IMAGE})
    add_executable(hermas_runtime_tests tests/test_runtime.c)
        target_link_libraries(hermas_runtime_tests PRIVATE hermas_runtime)
        if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(hermas_runtime_tests PRIVATE
                -Wall -Wextra -Wpedantic -Werror)
            if(HERMAS_SANITIZE)
                target_compile_options(hermas_runtime_tests PRIVATE
                    -fsanitize=address,undefined -fno-omit-frame-pointer)
                target_link_options(hermas_runtime_tests PRIVATE
                    -fsanitize=address,undefined)
            endif()
        endif()
        if(DEFINED HERMAS_TEST_CHOICE_IMAGE)
            add_test(NAME hermas_runtime_tests
                     COMMAND hermas_runtime_tests
                         "${HERMAS_TEST_IMAGE}"
                         "${HERMAS_TEST_CHOICE_IMAGE}")
        else()
            add_test(NAME hermas_runtime_tests
                     COMMAND hermas_runtime_tests "${HERMAS_TEST_IMAGE}")
        endif()
    endif()

    if(DEFINED HERMAS_TEST_PARALLEL_IMAGE AND
       DEFINED HERMAS_TEST_SAME_APP_IMAGE AND
       DEFINED HERMAS_TEST_DEADLINE_IMAGE AND
       DEFINED HERMAS_TEST_NESTED_DEADLINE_IMAGE AND
       DEFINED HERMAS_TEST_EACH_IMAGE)
        add_executable(hermas_parallel_runtime_tests
                       tests/test_parallel_runtime.c)
        target_link_libraries(hermas_parallel_runtime_tests
                              PRIVATE hermas_runtime)
        target_compile_options(hermas_parallel_runtime_tests
                               PRIVATE -Wall -Wextra -Wpedantic -Werror)
        if(HERMAS_SANITIZE)
            target_compile_options(hermas_parallel_runtime_tests PRIVATE
                                   -fsanitize=address,undefined
                                   -fno-omit-frame-pointer)
            target_link_options(hermas_parallel_runtime_tests PRIVATE
                                -fsanitize=address,undefined)
        endif()
        add_test(NAME hermas_parallel_runtime_tests
                 COMMAND hermas_parallel_runtime_tests
                         "${HERMAS_TEST_PARALLEL_IMAGE}"
                         "${HERMAS_TEST_SAME_APP_IMAGE}"
                         "${HERMAS_TEST_DEADLINE_IMAGE}"
                         "${HERMAS_TEST_NESTED_DEADLINE_IMAGE}"
                         "${HERMAS_TEST_EACH_IMAGE}")
    endif()
endif()

install(TARGETS
        hermas_image hermas_protocol hermas_runtime hermas_journal
        hermas_compensation hermas_result hermas_saga_log
        hermas_saga hermas_image_check
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY include/hermas
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

if(UNIX)
    install(TARGETS
            hermas_daemon hermas_control hermas_edge hermas_client
            hermas_host hermas_workspace
            hermasd hermas_run hermas_history
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(TARGETS
            hermas_grade_list hermas_mean_calculator hermas_printer
            hermas_discount hermas_tax hermas_receipt
        RUNTIME DESTINATION
            ${CMAKE_INSTALL_LIBEXECDIR}/hermas/examples)
    install(PROGRAMS
            tools/quickstart.sh tools/test_order_total.sh
            tools/test_workspace_compatibility.sh
        DESTINATION ${CMAKE_INSTALL_DATADIR}/hermas)
endif()

install(FILES
        README.md CONTRIBUTING.md SECURITY.md CODE_OF_CONDUCT.md ROADMAP.md
        LICENSE-MIT LICENSE-APACHE
        docs/QUICKSTART.md docs/APP_AUTHOR_GUIDE.md
        docs/EDGE_ABI.md
        docs/EXECUTION_RESULT_JSON_V1.md
        docs/WORKSPACE.md docs/WORKSPACE_MANIFEST_V1.md
        docs/COMPATIBILITY.md docs/THREAT_MODEL.md
        docs/CRASH_RECOVERY_MATRIX.md docs/FEATURE_ADMISSION.md
        docs/RELEASE_CHECKLIST.md
    DESTINATION ${CMAKE_INSTALL_DOCDIR})

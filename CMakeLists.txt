cmake_minimum_required(VERSION 3.16.1)
project(CRTP-DynamicPeriodicTask C)
set(CMAKE_C_STANDARD 11)

set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O3")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -g")

set(CMAKE_THREAD_PREFER_PTHREAD TRUE)
find_package(Threads REQUIRED)

set(DYNAMIC_PERIODIC_TASK
        src/main.c
        src/net_core.c
        src/supervisor.c
        src/task_routines.c
        src/task_runtime.c
)

add_executable(dynamic_periodic_task ${DYNAMIC_PERIODIC_TASK})
target_include_directories(dynamic_periodic_task PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/include/utils
)
target_link_libraries(dynamic_periodic_task PRIVATE Threads::Threads rt m)

enable_testing()

find_package(Python3 REQUIRED COMPONENTS Interpreter)

file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration_tests.py
        DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/tests/stress_tests.py
        DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_utils.py
        DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

add_test(
        NAME SystemIntegrationTest
        COMMAND ${Python3_EXECUTABLE} integration_tests.py
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

add_test(
        NAME SystemStressTest
        COMMAND ${Python3_EXECUTABLE} stress_tests.py
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
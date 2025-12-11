cmake_minimum_required(VERSION 3.16.1)
project(CRTP-DynamicPeriodicTask C)
set(CMAKE_C_STANDARD 99)

set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O3")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -g")

# Enable testing
enable_testing()

set(DYNAMIC_PERIODIC_TASK
        src/main.c
        src/net_core.c
        src/supervisor.c
        src/task_routines.c
        src/task_runtime.c
)

add_executable(dynamic_periodic_task ${DYNAMIC_PERIODIC_TASK})
target_include_directories(dynamic_periodic_task PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/include/utils)

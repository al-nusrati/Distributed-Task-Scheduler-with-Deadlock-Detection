cmake_minimum_required(VERSION 3.16)
project(OSScheduler)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# All source files
add_executable(scheduler
    main.cpp
    task.cpp
    worker.cpp
    scheduler.cpp
    resource_manager.cpp
    banker.cpp
    deadlock_detector.cpp
)

# Link threads
find_package(Threads REQUIRED)
target_link_libraries(scheduler Threads::Threads)

# nlohmann/json — using local json.hpp in core/ folder
target_include_directories(scheduler PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
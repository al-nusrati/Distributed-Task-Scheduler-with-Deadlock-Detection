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

# Link threads (pthreads on Linux)
find_package(Threads REQUIRED)
target_link_libraries(scheduler Threads::Threads)

# nlohmann/json (header only - for reading/writing JSON files)
include(FetchContent)
FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
)
FetchContent_MakeAvailable(nlohmann_json)
target_link_libraries(scheduler nlohmann_json::nlohmann_json)
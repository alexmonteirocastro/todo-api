cmake_minimum_required(VERSION 3.16)
project(todo_crow CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)
# Asio: pinned to an older version, because Crow 1.2.0 still uses 
# `asio::io_service`, which newer Asio releases removed 
FetchContent_Declare(
    asio
    GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
    GIT_TAG asio-1-30-2
)
FetchContent_MakeAvailable(asio)
set(ASIO_INCLUDE_DIR "${asio_SOURCE_DIR}/asio/include" CACHE PATH "" FORCE)

# Crow
FetchContent_Declare(
    Crow
    GIT_REPOSITORY https://github.com/CrowCpp/Crow.git
    GIT_TAG v1.2.0
)
set(CROW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CROW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(Crow)

find_package(SQLite3 REQUIRED)
add_executable(todo_crow main.cpp)
target_compile_definitions(todo_crow PRIVATE OPENAPI_PATH="${CMAKE_SOURCE_DIR}/openapi.json")
target_link_libraries(todo_crow PRIVATE Crow::Crow SQLite3::SQLite3)
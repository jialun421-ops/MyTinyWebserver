cmake_minimum_required(VERSION 3.10)
project(TinyWebServer CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -g")

include_directories(${CMAKE_SOURCE_DIR}/include)

find_package(Threads REQUIRED)

# ---------- 测试程序（每个独立 target） ----------
add_executable(test_locker     test/test_locker.cpp)
target_link_libraries(test_locker     Threads::Threads)

add_executable(test_threadpool test/test_threadpool.cpp)
target_link_libraries(test_threadpool Threads::Threads)

add_executable(test_http       test/test_http.cpp  src/http_conn.cpp)
target_link_libraries(test_http       Threads::Threads)

add_executable(test_timer test/test_timer.cpp)
target_link_libraries(test_timer Threads::Threads)

add_executable(test_block_queue test/test_block_queue.cpp)
target_link_libraries(test_block_queue Threads::Threads)

add_executable(test_log test/test_log.cpp src/log.cpp)
target_link_libraries(test_log Threads::Threads)

# ---------- 主程序 ----------
add_executable(server
    src/main.cpp
    src/webserver.cpp
    src/http_conn.cpp
    src/log.cpp
)
target_link_libraries(server Threads::Threads)

# ---------- 主程序（后面再加） ----------
# file(GLOB SRC_FILES src/*.cpp)
# add_executable(server ${SRC_FILES})
# target_link_libraries(server Threads::Threads mysqlclient)
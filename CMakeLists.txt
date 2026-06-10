cmake_minimum_required(VERSION 3.10)
project(http_server)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---- 查找依赖库 ----
find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)
find_package(Boost REQUIRED COMPONENTS system chrono)
find_package(OpenCV QUIET)

find_library(MYSQLCPPCONN_LIBRARY
    NAMES mysqlcppconn mysqlcppconn8
    PATHS /usr/lib /usr/lib64 /usr/local/lib
)

find_library(ONNXRUNTIME_LIBRARY
    NAMES onnxruntime
    PATHS /usr/lib /usr/lib64 /usr/local/lib
)

# ---- 头文件路径 ----
include_directories(
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/HttpServer/include
    ${PROJECT_SOURCE_DIR}/Apps/ChatServer/include
    ${PROJECT_SOURCE_DIR}/Apps/GomokuServer/include
    /usr/include/mysql-cppconn-8
    /usr/include/mysql
    ${OPENSSL_INCLUDE_DIR}
    ${CURL_INCLUDE_DIRS}
    /usr/include/opencv4
    /usr/local/include
)

link_directories(/usr/local/lib)

# ---- HttpServer 公共源码（编译为 object 库避免重复编译） ----
file(GLOB_RECURSE HTTP_SERVER_SRC
    "${PROJECT_SOURCE_DIR}/HttpServer/src/*.cpp"
)
add_library(http_server_obj OBJECT ${HTTP_SERVER_SRC})

# ---- 基础公共库（两个 server 都需要的） ----
set(COMMON_LIBS
    pthread
    muduo_net
    muduo_base
    ${MYSQLCPPCONN_LIBRARY}
    mysqlclient
    ${OPENSSL_LIBRARIES}
)

# ==================== ChatServer ====================
file(GLOB_RECURSE CHAT_SERVER_SRC
    "${PROJECT_SOURCE_DIR}/Apps/ChatServer/src/*.cpp"
)
list(REMOVE_ITEM CHAT_SERVER_SRC "${PROJECT_SOURCE_DIR}/Apps/ChatServer/src/main.cpp")

add_executable(chat_server
    "${PROJECT_SOURCE_DIR}/Apps/ChatServer/src/main.cpp"
    ${CHAT_SERVER_SRC}
    $<TARGET_OBJECTS:http_server_obj>
)

target_link_libraries(chat_server
    ${COMMON_LIBS}
    ${CURL_LIBRARIES}
    ${Boost_LIBRARIES}
    ${OpenCV_LIBS}
    SimpleAmqpClient
    rabbitmq
)

if(ONNXRUNTIME_LIBRARY)
    target_link_libraries(chat_server ${ONNXRUNTIME_LIBRARY})
endif()

# ==================== GomokuServer ====================
file(GLOB_RECURSE GOMOKU_SERVER_SRC
    "${PROJECT_SOURCE_DIR}/Apps/GomokuServer/src/*.cpp"
)
list(REMOVE_ITEM GOMOKU_SERVER_SRC "${PROJECT_SOURCE_DIR}/Apps/GomokuServer/src/main.cpp")

add_executable(gomoku_server
    "${PROJECT_SOURCE_DIR}/Apps/GomokuServer/src/main.cpp"
    ${GOMOKU_SERVER_SRC}
    $<TARGET_OBJECTS:http_server_obj>
)

target_link_libraries(gomoku_server
    ${COMMON_LIBS}
)

# ---- 调试输出 ----
message(STATUS "=== ChatServer ===")
message(STATUS "  sources: ${CHAT_SERVER_SRC}")
message(STATUS "  ONNX Runtime: ${ONNXRUNTIME_LIBRARY}")

message(STATUS "=== GomokuServer ===")
message(STATUS "  sources: ${GOMOKU_SERVER_SRC}")

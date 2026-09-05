cmake_minimum_required(VERSION 3.16)
project(ddz-server CXX)

set(DDZ_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(-Wall -Wextra -Wno-unused-parameter)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${DDZ_ROOT}/lib")

set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_BENCH OFF CACHE BOOL "" FORCE)
set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory("${DDZ_ROOT}/3rd/spdlog" "${CMAKE_BINARY_DIR}/spdlog")

add_library(ddznet STATIC
    "${CMAKE_CURRENT_SOURCE_DIR}/net/http_ws_server.cpp"
)
target_include_directories(ddznet PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${DDZ_ROOT}/3rd/asio/asio/include"
    "${DDZ_ROOT}/3rd/websocketpp"
)
target_compile_definitions(ddznet PRIVATE
    ASIO_STANDALONE
    _WEBSOCKETPP_CPP11_STL_
)
target_link_libraries(ddznet PRIVATE pthread)
set_target_properties(ddznet PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    ARCHIVE_OUTPUT_DIRECTORY "${DDZ_ROOT}/lib"
)

add_executable(ddz-server
    main.cpp
    config_parse.cpp
    log_record.cpp
    mysql_handle.cpp
    json_util.cpp
    table.cpp
    game.cpp
    cards.cpp
)
target_include_directories(ddz-server PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${DDZ_ROOT}/3rd/json/single_include"
    "${DDZ_ROOT}/3rd/spdlog/include"
)
target_compile_definitions(ddz-server PRIVATE SPDLOG_COMPILED_LIB)
target_link_libraries(ddz-server PRIVATE
    ddznet
    spdlog::spdlog
    mysqlclient
    pthread
)
set_target_properties(ddz-server PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${DDZ_ROOT}/deploy/bin"
)

add_executable(ddz-cards-test
    cards.cpp
    cards_test.cpp
)
set_target_properties(ddz-cards-test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
target_compile_options(ddz-cards-test PRIVATE -UNDEBUG)

cmake_minimum_required(VERSION 3.16)

project(tinyredis LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

enable_testing()

add_executable(tinyredis-server
  src/command.cpp
  src/main.cpp
  src/resp.cpp
  src/server.cpp
  src/Database.cpp
)

add_executable(tinyredis-cli
  src/cli.cpp
)

add_executable(tinyredis-core-test
  src/command.cpp
  src/Database.cpp
  src/resp.cpp
  tests/core_test.cpp
)

target_include_directories(tinyredis-core-test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

add_test(NAME tinyredis-core-test COMMAND tinyredis-core-test)

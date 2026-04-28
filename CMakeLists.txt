cmake_minimum_required(VERSION 3.16)

project(tinyredis LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

enable_testing()

add_executable(tinyredis-server
  src/command.cpp
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

add_executable(tinyredis-cli-test
  src/cli.cpp
  tests/cli_test.cpp
)

target_compile_definitions(tinyredis-cli-test PRIVATE
  TINYREDIS_CLI_TEST
)

target_include_directories(tinyredis-core-test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_include_directories(tinyredis-cli-test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

add_test(NAME tinyredis-core-test COMMAND tinyredis-core-test)
add_test(NAME tinyredis-cli-test COMMAND tinyredis-cli-test)

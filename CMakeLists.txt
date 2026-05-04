cmake_minimum_required(VERSION 3.16)

project(tinyredis LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(tinyredis-server
  src/command.cpp
  src/resp.cpp
  src/event_loop.cpp
  src/server.cpp
  src/tinyredis-server.cpp
  src/database.cpp
)

if(APPLE)
  target_sources(tinyredis-server PRIVATE src/event_loop_kqueue.cpp)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_sources(tinyredis-server PRIVATE src/event_loop_epoll.cpp)
else()
  message(FATAL_ERROR "EventLoop backend is not implemented for this platform")
endif()

add_executable(tinyredis-cli
  src/cli.cpp
  src/tinyredis-cli.cpp
)

if(DEFINED ENV{TINYREDIS_BUILD_TESTS} AND "$ENV{TINYREDIS_BUILD_TESTS}")
  enable_testing()

  add_executable(tinyredis-resp-test
    src/resp.cpp
    tests/resp_test.cpp
  )

  add_executable(tinyredis-database-test
    src/database.cpp
    tests/database_test.cpp
  )

  add_executable(tinyredis-command-test
    src/command.cpp
    src/database.cpp
    src/resp.cpp
    tests/command_test.cpp
  )

  add_executable(tinyredis-server-test
    src/server.cpp
    src/command.cpp
    src/database.cpp
    src/event_loop.cpp
    src/resp.cpp
    tests/server_test.cpp
  )

  if(APPLE)
    target_sources(tinyredis-server-test PRIVATE src/event_loop_kqueue.cpp)
  elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(tinyredis-server-test PRIVATE src/event_loop_epoll.cpp)
  else()
    message(FATAL_ERROR "EventLoop backend is not implemented for this platform")
  endif()

  add_executable(tinyredis-cli-test
    src/cli.cpp
    tests/cli_test.cpp
  )

  add_executable(tinyredis-event-loop-test
    src/event_loop.cpp
    tests/event_loop_test.cpp
  )

  if(APPLE)
    target_sources(tinyredis-event-loop-test PRIVATE src/event_loop_kqueue.cpp)
  elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(tinyredis-event-loop-test PRIVATE src/event_loop_epoll.cpp)
  else()
    message(FATAL_ERROR "EventLoop backend is not implemented for this platform")
  endif()

  target_include_directories(tinyredis-resp-test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
  )

  target_include_directories(tinyredis-database-test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
  )

  target_include_directories(tinyredis-command-test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
  )

  target_include_directories(tinyredis-server-test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
  )

  target_include_directories(tinyredis-cli-test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
  )

  target_include_directories(tinyredis-event-loop-test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
  )

  add_test(NAME tinyredis-resp-test COMMAND tinyredis-resp-test)
  add_test(NAME tinyredis-database-test COMMAND tinyredis-database-test)
  add_test(NAME tinyredis-command-test COMMAND tinyredis-command-test)
  add_test(NAME tinyredis-server-test COMMAND tinyredis-server-test)
  add_test(NAME tinyredis-cli-test COMMAND tinyredis-cli-test)
  add_test(NAME tinyredis-event-loop-test COMMAND tinyredis-event-loop-test)
endif()

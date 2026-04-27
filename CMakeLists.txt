cmake_minimum_required(VERSION 3.16)

project(tinyredis LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(tinyredis-server
  src/command.cpp
  src/main.cpp
  src/resp.cpp
  src/server.cpp
)

add_executable(tinyredis-cli
  src/cli.cpp
)

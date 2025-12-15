cmake_minimum_required(VERSION 3.10)
project(kubsh VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ФОРСИРУЕМ /usr как префикс
set(CMAKE_INSTALL_PREFIX "/usr" CACHE PATH "Install prefix" FORCE)

add_executable(kubsh src/main.cpp)

target_compile_options(kubsh PRIVATE -Wall -Wextra -pthread)
target_link_libraries(kubsh pthread)

install(TARGETS kubsh DESTINATION bin)
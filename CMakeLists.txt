# ESPTimer - https://github.com/ESPToolKit/esp-timer
# Copyright zekageri 2025
# MIT License

cmake_minimum_required(VERSION 3.12)
project(ESPTimer)

enable_testing()

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(${COVERAGE})
	set(CMAKE_CXX_FLAGS "-fprofile-arcs -ftest-coverage -g -O0")
endif()

include_directories(${CMAKE_CURRENT_LIST_DIR}/src)
add_subdirectory(test)

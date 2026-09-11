cmake_minimum_required(VERSION 3.20)
project(RioController LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(Rio WIN32 rio.cpp)

if(MSVC)
    target_compile_options(Rio PRIVATE /W4 /permissive-)
endif()

target_link_libraries(Rio PRIVATE user32 gdi32 ws2_32)

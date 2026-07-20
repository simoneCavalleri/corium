cmake_minimum_required(VERSION 3.10)
project(Corium LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)


# Library target
add_library(corium STATIC
    src/AppCore.cpp
    src/Reactor.cpp
    src/ServiceManager.cpp
)

target_include_directories(corium PUBLIC include)

# Sample target
add_executable(corium_demo samples/demo/main.cpp)
target_link_libraries(corium_demo PRIVATE corium)


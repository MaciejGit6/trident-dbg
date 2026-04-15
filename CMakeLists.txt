cmake_minimum_required(VERSION 3.14)

# Project name and language
project(trident-dbg VERSION 0.1.0 LANGUAGES CXX)

# Enforce C++17 (Modern C++)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Disable compiler-specific extensions (e.g., gnu++17) to ensure standard compliance
set(CMAKE_CXX_EXTENSIONS OFF) 

# Define the main executable
add_executable(trident_dbg
    src/main.cpp
    src/debugger.cpp
    src/breakpoint.cpp
)

# Apply strict compiler warnings (Industry Best Practice)
target_compile_options(trident_dbg PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -g          # Include debug symbols (crucial for a debugger!)
    -O0         # Disable optimizations during development
)

# Tell CMake where to look for your public header files
target_include_directories(trident_dbg PRIVATE 
    ${CMAKE_SOURCE_DIR}/include
)
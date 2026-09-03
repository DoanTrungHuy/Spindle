cmake_minimum_required(VERSION 3.15)
project(Spindle CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 1. Declare source files used to build the LIBRARY
set(LIB_SOURCES
    src/reactor.cpp
    src/slab_allocator.cpp
    src/sharded_map.cpp
    src/wal_flusher.cpp
)

# Create static or shared library. We use STATIC here.
add_library(spindle STATIC ${LIB_SOURCES})

# Public include directory so other projects linking to 'spindle' can see header files
target_include_directories(spindle PUBLIC 
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Optimization and multithreading flags
target_compile_options(spindle PRIVATE -Wall -Wextra -O3 -pthread)

# ==============================================================
# 2. Executable File (App/Demo)
# ==============================================================
add_executable(spindle_app src/main.cpp)
# Link the application with the library
target_link_libraries(spindle_app PRIVATE spindle)
target_compile_options(spindle_app PRIVATE -Wall -Wextra -O3 -pthread)
target_link_options(spindle_app PRIVATE -pthread)

add_executable(bench_client src/bench_client.cpp)
target_link_libraries(bench_client PRIVATE spindle)
target_compile_options(bench_client PRIVATE -Wall -Wextra -O3 -pthread)
target_link_options(bench_client PRIVATE -pthread)

# ==============================================================
# 3. Install Configuration (so others can use cmake --install)
# ==============================================================
include(GNUInstallDirs)

# Install the library
install(TARGETS spindle
    EXPORT SpindleTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Install the header files (.h)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

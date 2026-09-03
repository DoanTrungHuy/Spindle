cmake_minimum_required(VERSION 3.15)
project(KVStore CXX)

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
add_library(kv_store STATIC ${LIB_SOURCES})

# Public include directory so other projects linking to 'kv_store' can see header files
target_include_directories(kv_store PUBLIC 
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Optimization and multithreading flags
target_compile_options(kv_store PRIVATE -Wall -Wextra -O3 -pthread)

# ==============================================================
# 2. Executable File (App/Demo)
# ==============================================================
add_executable(kv_store_app src/main.cpp)
# Link the application with the library
target_link_libraries(kv_store_app PRIVATE kv_store)
target_compile_options(kv_store_app PRIVATE -Wall -Wextra -O3 -pthread)
target_link_options(kv_store_app PRIVATE -pthread)

# ==============================================================
# 3. Install Configuration (so others can use cmake --install)
# ==============================================================
include(GNUInstallDirs)

# Install the library
install(TARGETS kv_store
    EXPORT KVStoreTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Install the header files (.h)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

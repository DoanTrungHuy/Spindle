# Spindle

<div align="center">
  <img src="logo.png" alt="Spindle Logo" width="400" style="max-width: 250px;"><br>

  [![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](https://opensource.org/licenses/MIT)
  [![Language](https://img.shields.io/badge/Language-C%2B%2B20-purple.svg)]()
  [![Platform](https://img.shields.io/badge/Platform-Linux%20%7C%20macOS-lightgrey.svg)]()
  [![Build](https://img.shields.io/badge/Build-Passing-brightgreen.svg)]()
  ![Views](https://komarev.com/ghpvc/?username=DoanTrungHuy-Spindle&label=Views&color=7c3aed&style=flat)
</div>

A high-performance, multithreaded Spindle engine written in Modern C++ (C++20).

## Features
- **High Performance**: Optimized with low-level techniques (Slab Allocation, Spinlocks, Reactor pattern).
- **Multithreaded**: Built to handle concurrent workloads efficiently.
- **Persistent**: Includes a Write-Ahead Log (WAL) flusher for data safety.
- **Easy to integrate**: CMake-friendly and exportable as a static/shared library.

## Installation and Usage

You can easily use this library in your own CMake projects using `FetchContent`.

### Using FetchContent in your CMake project

Add the following to your `CMakeLists.txt`:

```cmake
include(FetchContent)
FetchContent_Declare(
  Spindle
  GIT_REPOSITORY https://github.com/DoanTrungHuy/Spindle.git
  GIT_TAG        master
)
FetchContent_MakeAvailable(Spindle)

# Link it to your executable
add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE spindle)
```

### Building from Source

```bash
git clone https://github.com/DoanTrungHuy/Spindle.git
cd spindle
mkdir build && cd build
cmake ..
make -j$(nproc)
```

## Running the Example

The project includes a sample application `spindle_app` that demonstrates how to use the library.

```bash
# Inside the build directory
./spindle_app
```

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.









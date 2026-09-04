# Spindle

<div align="center">
  <img src="logo.png" alt="Spindle Logo" width="400" style="max-width: 250px;"><br>

  [![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](https://opensource.org/licenses/MIT)
  [![Language](https://img.shields.io/badge/Language-C%2B%2B20-purple.svg)]()
  [![Platform](https://img.shields.io/badge/Platform-Linux%20%7C%20macOS-lightgrey.svg)]()
  [![Build](https://img.shields.io/badge/Build-Passing-brightgreen.svg)]()
  ![Views](https://komarev.com/ghpvc/?username=DoanTrungHuy-Spindle&label=Views&color=7c3aed&style=flat)
</div>

A high-performance, multithreaded Key-Value engine written in Modern C++ (C++20).

## Download & Installation

### Option 1: Building from Source (Recommended)
You can clone the repository and build it locally:
```bash
# 1. Clone the repository
git clone https://github.com/DoanTrungHuy/Spindle.git
cd Spindle

# 2. Build the project
mkdir build && cd build
cmake ..
make -j$(nproc)
```

### Option 2: Using FetchContent in your CMake project
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

## Running the Server

Start the Spindle server from the build directory.

```bash
# Start with default settings (Host: 0.0.0.0, Port: 8888, Threads: 4)
./spindle_app

# Start with custom host, port, and number of threads
./spindle_app --host 127.0.0.1 --port 6379 --threads 8
```

## Usage Guides

Spindle uses a simple text-based TCP protocol. You can connect to it using any language that supports TCP sockets. The default server socket is `0.0.0.0:8888` (reachable via `127.0.0.1` locally). Commands are sent with a newline character (`\n`) at the end.

### Python (using the provided SDK)
A Python client is provided in the `clients/python/` directory.

```python
import sys
# Assuming you are in the project root
sys.path.append('clients/python')

from spindle import SpindleClient

# Connect to the server (default is 127.0.0.1:8888)
client = SpindleClient(host='127.0.0.1', port=8888)
client.connect()

client.set("session:1", "active", ex=3600) # Set key with expiration
print(client.get("session:1"))

client.delete("session:1")
client.close()
```

### Node.js / JavaScript
You can easily interact with Spindle using the built-in `net` module.

```javascript
const net = require('net');

const client = new net.Socket();
client.connect(8888, '127.0.0.1', () => {
    console.log('Connected to Spindle');
    
    // Set a key
    client.write('SET user:1 John\n');
    
    // Get a key
    client.write('GET user:1\n');
});

client.on('data', (data) => {
    console.log('Received: ' + data.toString().trim());
});
```

### Go (Golang)
Using the standard `net` package:

```go
package main

import (
    "bufio"
    "fmt"
    "net"
)

func main() {
    conn, err := net.Dial("tcp", "127.0.0.1:8888")
    if err != nil {
        panic(err)
    }
    defer conn.Close()

    // Send command
    fmt.Fprintf(conn, "SET session:go active\n")
    
    // Read response
    response, _ := bufio.NewReader(conn).ReadString('\n')
    fmt.Print("Response: ", response)
}
```

### cURL / Netcat (Command Line)
You can test the server directly from your terminal using `nc` (netcat):

```bash
# Connect to Spindle
nc 127.0.0.1 8888
# Then type commands:
SET mykey hello
GET mykey
DEL mykey
```

## Features
- **High Performance**: Optimized with low-level techniques (Slab Allocation, Spinlocks, Reactor pattern).
- **Multithreaded**: Built to handle concurrent workloads efficiently.
- **Persistent**: Includes a Write-Ahead Log (WAL) flusher for data safety.
- **Easy to integrate**: CMake-friendly and exportable as a static/shared library.

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

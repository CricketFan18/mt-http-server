# Multi-Threaded HTTP Server in C++

A high-performance, multi-threaded HTTP server built from scratch using C++ and POSIX sockets. 

This project implements a **Thread Pool** architecture to handle concurrent client connections efficiently. It avoids external networking libraries to demonstrate a deep understanding of low-level system calls, memory management, and synchronization primitives.

## Key Features

* **Custom Thread Pool:** Implements a fixed-size pool of worker threads to handle requests, reducing the overhead of creating/destroying threads for every connection.
* **Concurrency Control:** Uses `std::mutex` and `std::condition_variable` to safely manage the task queue and synchronize worker threads.
* **Backpressure Handling:** Automatically rejects new clients with `503 Service Unavailable` if the request queue is full, preventing server overload (DoS protection).
* **HTTP 1.1 Support:** Parses raw HTTP requests, handles headers, and maintains persistent connections (`keep-alive`).
* **Session Management:** Implements basic cookie-based authentication (Login/Logout flows) and secure route protection.
* **System Monitoring:** Reads directly from the Linux kernel (`/proc/loadavg`) to display real-time CPU load.

## 🚀 Performance Benchmarks
Tested using wrk on a 6-core Linux environment (WSL 2).

**Command:** `wrk -t6 -c100 -d10s http://localhost:8080/`

| Metric | Result |
| :--- | :--- |
| **Requests/sec** | **15,889.44** |
| **Total Requests** | 159,779 (in 10s) |
| **Avg Latency** | 356.98 µs |
| **Errors** | **0** (0.00%) |

*Configuration: 6 Worker Threads, 1000 Queue Size, 128 Backlog.*

## Tech Stack

* **Language:** C++ (Standard Library)
* **Networking:** POSIX Sockets (socket, bind, listen, accept)
* **Concurrency:** C++ Threads, Mutexes, Condition Variables
* **OS APIs:** Linux File System I/O, Signal Handling

## Project Structure
```
├── main.cpp         # Complete source code (Server, Thread Pool, Request Parser)
├── index.html       # Static HTML entry point
└── README.md        # Documentation
```

## ⚙️ How to Run
Since this project uses standard C++ libraries and POSIX threads, no complex build system is required.

**1. Compile:**

Use `g++` to compile the source. The `-pthread` flag is essential for threading support.
```
g++ main.cpp -o server -pthread
```

**2. Start the Server**

Run the binary. The server will listen on port **8080**.
```
./server [PORT] [Number of Threads]
```

**3. Access**

Open your web browser and navigate to: `http://localhost:8080`


## 📡 API Endpoints
| Method    | Endpoint    | Description                                         |
| --------- | ----------  | -------------------------------------------------- |
| GET       | /           | Serves the index.html file.                         |
| GET       | /cpu        | Returns real-time server CPU load from kernel.      |
| GET       | /info       | Returns a JSON response with server info.           |
| POST      | /login      | Simulates login and sets a session_token cookie.    |
| GET       | /dashboard  | Protected Route. Only accessible if logged in.      |
| GET       | /logout     | Clears the session cookie.                          |

## 🧠 System Architecture
The server follows the Producer-Consumer pattern:

**1. Main Thread (Producer):** Listens on port 8080. When a connection is accepted, it pushes the client socket descriptor into a thread-safe std::queue.

**2. Worker Threads (Consumers):** A pool of threads (default: 5) waits for tasks. When a socket is available, a thread wakes up, locks the queue, retrieves the socket, and processes the HTTP request.

**3. Synchronization:** Access to the queue is protected by a mutex to prevent race conditions. A condition_variable is used to put threads to sleep when idle, saving CPU resources.


## 🛡️ License
This project is open-source and available for educational purposes.
# cpp-winsock-http-server

A minimal HTTP/1.1 server built **from scratch** in C++23 using the raw Windows Sockets API (Winsock).

No frameworks, no HTTP libraries — just sockets and the C++ standard library.

## Features

- HTTP/1.1 request-line and header parsing
- URL routing (`GET /home`, `GET /api/status`)
- Static file serving from `www/` with MIME type detection
- Layered path traversal protection
- RAII wrappers for Winsock lifecycle management
- Clean component separation: `Request`, `Response`, `Router`, and `StaticFileServer`
- **Multithreaded:** thread-per-connection model using `std::thread` after `accept()`

## Build

### MSVC (Visual Studio Developer Command Prompt)

```bat
cl /EHsc /std:c++latest /Fe:server.exe main.cpp
```

### MinGW / GCC (13+ for `<print>`)

```bash
g++ -std=c++23 -O2 -o server.exe main.cpp -lws2_32
```

## Run

```bat
server.exe
```

The server listens at:

[**http://localhost:6666**](http://localhost:6666)

Each accepted client connection is handed to a separate thread, allowing multiple requests to be processed concurrently.

### Test endpoints

```bash
# Routed handler
curl.exe http://localhost:6666/home

# JSON route
curl.exe http://localhost:6666/api/status

# Static file serving (www/index.html)
curl.exe http://localhost:6666/

# Non-existent resource (404)
curl.exe http://localhost:6666/nope

# Path traversal attempt (403)
curl.exe --path-as-is "http://localhost:6666/../../secret"
```

## Architecture

```text
                  TCP request
                       |
              +--------v---------+
              |  Winsock accept  |
              |       loop       |
              +--------+---------+
                       |
              +--------v---------+
              |   std::thread    |
              |     .detach()    |
              +--------+---------+
                       |
              +--------v---------+
              | Receive request  |
              |  until \r\n\r\n   |
              +--------+---------+
                       |
              +--------v---------+
              |  Parse HTTP      |
              | method, path,    |
              | version, headers |
              +--------+---------+
                       |
              +--------v---------+
              |      Router      |
              | METHOD + /path   |
              |      match?      |
              +----+--------+----+
                   |        |
                  YES       NO
                   |        |
            +------v---+ +--v----------------+
            | Handler  | | StaticFileServer |
            +------+---+ +--------+---------+
                   |              |
                   +------+-------+
                          |
                 +--------v---------+
                 | Response::       |
                 | send_response()  |
                 | Serialize HTTP   |
                 | wire bytes       |
                 +--------+---------+
                          |
                 +--------v---------+
                 | send()           |
                 | closesocket()    |
                 +------------------+
```

## Components

### Wsocket

RAII wrapper for Winsock lifecycle management.

- Calls `WSAStartup()` during construction.
- Calls `WSACleanup()` during destruction.
- Removes the need for manual Winsock lifecycle management.

### Request

Represents a parsed HTTP request.

Contains:

- HTTP method
- Request path
- HTTP version
- Request headers

### Response

Represents an HTTP response.

Contains:

- Response content/body
- HTTP status code
- Content type

`send_response()` serializes the response into HTTP wire format and sends it through the client socket.

### Router

Maps `"METHOD /path"` keys to `std::function` handlers.

For example:

- `GET /home`
- `GET /api/status`

If no matching route is found, the request falls through to the static file server.

### StaticFileServer

Handles static file requests from the `www/` directory.

Responsibilities:

- Maps URL paths to filesystem paths.
- Enforces path traversal protection.
- Detects MIME types using file extensions.
- Serves requested files when available.

## Concurrency Model

The server uses a **thread-per-connection** architecture.

1. The main thread waits for incoming connections using `accept()`.
2. Once a connection is accepted, a new `std::thread` is created.
3. The new thread executes `handle_client()`.
4. The main thread immediately returns to the accept loop.
5. The client thread receives, processes, and responds to the request.
6. After sending the response, the client socket is closed.

### Thread ownership

Each client thread owns its client socket throughout the request lifecycle:

```text
accept()
   |
   v
client socket
   |
   v
handle_client()
   |
   +-- recv()
   +-- parse request
   +-- route request
   +-- generate response
   +-- send()
   |
   v
closesocket()
```

### Shared state

No mutexes are currently required because:

- `Router` is read-only after server initialization.
- `StaticFileServer` is read-only after initialization.
- Request-specific data is local to each `handle_client()` invocation.
- Each thread has its own request object, buffers, and response data.

This assumes that route handlers and shared components do not modify shared state concurrently.

### Connection handling

The server uses:

```http
Connection: close
```

Every response closes the connection after processing one request.

This keeps connection management simple and avoids the complexity of persistent HTTP connections.

## Benchmarks

Measured using ApacheBench (`ab`) on loopback.

| Concurrency | Requests |   Throughput | Latency (per request) | Failed |
| ----------: | -------: | -----------: | --------------------: | -----: |
|          10 |  100,000 | 878.90 req/s |              11.38 ms |      0 |
|        1000 |   10,000 | 411.90 req/s |            2427.77 ms |      0 |

### Observations

Performance degrades significantly beyond approximately 10 concurrent connections.

The thread-per-connection model has no upper bound on the number of threads created.

At 1000 concurrent connections:

- 1000 threads compete for CPU resources.
- Each thread reserves approximately 1 MB of stack space under the measured configuration.
- Context switching adds significant overhead.
- Thread creation and scheduling become increasingly expensive.

The result demonstrates a limitation of the thread-per-connection model and motivates the next optimization: **a bounded thread pool**.

## Path Traversal Protection

`StaticFileServer::isPathSafe()` uses multiple layers of validation to prevent access outside the static file directory.

### Layer 1: Reject absolute paths

Absolute filesystem paths such as:

```text
C:\Windows\...
```

are rejected.

### Layer 2: Reject parent-directory components

Any `..` component in the URL path is rejected before filesystem resolution.

For example:

```text
/../../etc/passwd
```

is rejected.

### Layer 3: Canonicalize and verify

The requested path is resolved using `std::filesystem::weakly_canonical()`.

The resulting path is checked to ensure it remains within the canonical `www/` directory.

This provides additional protection against symlinks and unusual filesystem path representations.

### Layer 4: Handle filesystem errors

`std::filesystem::filesystem_error` exceptions are caught and treated as forbidden requests.

This prevents malformed or problematic filesystem paths from crashing request handling.

### Example

```http
GET /../../etc/passwd HTTP/1.1
```

Expected response:

```http
HTTP/1.1 403 Forbidden
```

Each validation layer provides defense in depth rather than relying on a single check.

## Project Structure

```text
cpp-winsock-http-server/
│
├── main.cpp          # Server implementation
│
├── www/
│   └── index.html    # Static file root
│
├── README.md
│
└── .gitignore
```

## Future Improvements

- [ ] Replace thread-per-connection with a bounded thread pool.
- [ ] Add graceful server shutdown.
- [ ] Improve HTTP request validation and error handling.
- [ ] Add request-body parsing.
- [ ] Support persistent connections (`Keep-Alive`).
- [ ] Add request timeouts and connection limits.
- [ ] Introduce structured logging.

## Technologies

- **Language:** C++23
- **Networking:** Windows Sockets API (Winsock2)
- **Concurrency:** `std::thread`
- **Filesystem:** `std::filesystem`
- **Build:** MSVC / MinGW GCC

---

**A learning project focused on understanding HTTP, TCP sockets, filesystem security, RAII, and multithreaded server architecture using modern C++.**

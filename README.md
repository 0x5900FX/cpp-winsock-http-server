# cpp-winsock-http-server

A minimal HTTP/1.1 server built **from scratch** in C++20 using the raw Windows
Sockets API (Winsock) — no frameworks, no HTTP libraries, just sockets and the
standard library.

## Features

- HTTP/1.1 request line + header parsing
- URL routing (`GET /home`, `GET /api/status`)
- Static file serving from `www/` with MIME type detection
- Layered path traversal protection
- RAII wrappers for Winsock lifecycle and socket cleanup
- Clean separation: `Request`, `Response`, `Router`, `StaticFileServer`

## Not yet implemented

- **Concurrency** — currently single-client-at-a-time (blocking accept loop)
- POST request bodies
- Keep-alive / chunked transfer encoding
- HTTPS

These are the natural next steps, roughly in that order.

## Build

### MSVC (Visual Studio Developer Command Prompt)

```bat
cl /EHsc /std:c++latest /Fe:server.exe main.cpp
```

### MinGW / GCC (13+ for `<print>`)

```bat
g++ -std=c++23 -O2 -o server.exe main.cpp -lws2_32
```

## Run

```bat
server.exe
```

Listens on `http://localhost:6666`.

```bash
# Routed handler
curl.exe http://localhost:6666/home

# JSON API route
curl.exe http://localhost:6666/api/status

# Static file (serves www/index.html)
curl.exe http://localhost:6666/

# Unknown route → 404
curl.exe http://localhost:6666/nope

# Path traversal attempt → 403
curl.exe --path-as-is "http://localhost:6666/../../secret"
```

## Architecture

```
                 ┌──────────────────────┐
  TCP request →  │  Winsock accept loop │
                 └──────────┬───────────┘
                            │
                 ┌──────────▼───────────┐
                 │   recv until \r\n\r\n │
                 └──────────┬───────────┘
                            │
                 ┌──────────▼───────────┐
                 │   Parse HTTP request  │
                 │  (method, path, hdrs) │
                 └──────────┬───────────┘
                            │
                 ┌──────────▼───────────┐
                 │        Router         │
                 │  METHOD /path match?  │
                 └─────┬──────────┬──────┘
                       │ yes      │ no
                ┌──────▼───┐  ┌───▼──────────────┐
                │ Handler  │  │ StaticFileServer │
                └──────┬───┘  └───┬──────────────┘
                       │          │
                       └────┬─────┘
                            │
                 ┌──────────▼───────────┐
                 │  Response::send_response │
                 │  → HTTP/1.1 wire bytes │
                 └──────────┬───────────┘
                            │
                 ┌──────────▼───────────┐
                 │  send → closesocket   │
                 └──────────────────────┘
```

**Components:**

- **`Wsocket`** — RAII for `WSAStartup` / `WSACleanup`. Initialization in the
  constructor, cleanup in the destructor, no manual lifecycle management.
- **`Request`** — parsed HTTP request (method, path, version, headers).
- **`Response`** — HTTP response (content, status, content-type);
  `send_response()` serializes to wire format.
- **`Router`** — maps `"METHOD /path"` keys to `std::function` handlers.
  Unmatched paths fall through to the file server.
- **`StaticFileServer`** — resolves URL paths under `www/`, enforces traversal
  protection, detects MIME type from file extension.

`Connection: close` is used for every response — one request per connection.
This keeps the design simple and avoids half-open connection tracking.

## Path traversal protection

`StaticFileServer::isPathSafe` uses **multiple independent layers** so that any
single bypass is caught by the others:

1. **Rejects absolute paths** — `C:\Windows\...` cannot reach the filesystem.
2. **Rejects `..` components** — before canonicalization, any `..` segment in
   the URL path is rejected outright.
3. **Canonicalizes and verifies** — after `fs::weakly_canonical`, the resolved
   path must be lexically under `www/`. This catches symlinks and unusual path
   encodings.
4. **Catches `fs::filesystem_error`** — malformed paths (invalid characters,
   too-long paths) that would throw are treated as forbidden.

Requests like `GET /../../etc/passwd` return `403 Forbidden` at layer 2.
The other layers exist as defense-in-depth.

## Project layout

```
cpp-winsock-http-server/
├── main.cpp          ← the entire server
├── www/
│   └── index.html    ← static root
├── README.md
└── .gitignore
```

## License

MIT

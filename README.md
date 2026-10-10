# ipc

[![CI](https://github.com/alya-lang/ipc/actions/workflows/ci.yml/badge.svg)](https://github.com/alya-lang/ipc/actions/workflows/ci.yml)
[![License](https://img.shields.io/github/license/alya-lang/ipc?color=blue&label=License)](LICENSE)
[![Alya](https://img.shields.io/badge/dynamic/toml?url=https%3A%2F%2Fraw.githubusercontent.com%2Falya-lang%2Fipc%2Fmain%2Falya.toml&query=%24.package.alya-version&label=Alya&color=orange&prefix=%3E%3D)](https://github.com/alya-lang/alya)
[![Package Version](https://img.shields.io/badge/dynamic/toml?url=https%3A%2F%2Fraw.githubusercontent.com%2Falya-lang%2Fipc%2Fmain%2Falya.toml&query=%24.package.version&label=Version&color=brightgreen)](alya.toml)

Cross-platform inter-process communication: TCP loopback, named pipes, Unix sockets, framing and request-reply for Alya

---

## 🌟 Features

- ⚡ **TCP Loopback Channel**: Framing-aware client/server over `std/net` with one frame per direction and one-shot request/reply helpers
- 🔌 **Native Pipe Transport**: Bundled zero-dependency C engine (`c/ipc.c`) — Windows named pipes and POSIX Unix domain sockets behind one slot API
- 🧩 **Length-Prefixed Framing**: `<len>:<payload>` text frames and `<kind>:<id>:<len>:<payload>` message frames; payloads may contain colons and newlines
- 🔒 **Public/Private Visibility (`pub`)**: Fine-grained export control with `pub` for endpoints, messages, framing, and channel functions
- 🎯 **Endpoint & Message Models**: Idiomatic `enum` types (`IpcTransport`, `IpcKind`) and typed containers (`IpcEndpoint`, `IpcMessage`, `IpcError`, `IpcStats`)
- 🛡️ **Defensive Result Pattern**: Structured `IpcError` throws for invalid ports, failed connections, and malformed frames
- 🧪 **Enterprise Test & Benchmark Suite**: Loopback roundtrip coverage with standard assertions (`std/test`) plus framing micro-benchmarks
- 🔌 **Reserved Transports**: `UnixSocket` endpoint variant named early so config code can adopt it when the backend lands

---

## 📁 Project Architecture

```
ipc/
├── .alyalint               # Linter configuration (rules, exclusions, severity overrides)
├── .editorconfig           # Uniform formatting rules across IDEs and editors
├── .gitignore              # Ecosystem standard ignore filters
├── .vscode/                # VS Code workspace settings, DAP launch configurations & tasks
├── alya.toml               # Package manifest with dependencies, [features] and native [build]
├── c/                      # Bundled native pipe engine (Windows named pipes, POSIX Unix sockets)
│   ├── ipc.c               # Slot-based pipe transport (listen/accept/connect/send/recv/poll/close)
│   └── ipc.h               # Native API header
├── src/
│   ├── lib.alya            # Public API facade (constructors and re-exports)
│   ├── types.alya          # Data models, pub enums, pub structs, and struct methods
│   ├── ffi.alya            # Native extern "C" declarations for the pipe engine
│   └── core/               # Subdirectory module hierarchy
│       ├── frame.alya      # Length-prefixed frame and message codec
│       ├── channel.alya    # TCP loopback listen/accept/connect/send/recv
│       └── pipe.alya       # Named pipe listen/accept/connect/send/recv
├── examples/
│   └── demo.alya           # Runnable walkthrough of framing and loopback echo
├── tests/
│   └── test_basic.alya     # Automated test suite with framing and loopback coverage
└── benches/
    └── bench_basic.alya    # Micro-benchmarks for the frame and message codec
```

> [!NOTE]
> **Visibility & Modularity:** Symbols annotated with `pub` (`pub function`, `pub struct`, `pub enum`) are exported to external consumers and re-exporting modules. Symbols without `pub` remain strictly internal to their declaring module, preventing symbol collisions and implementation leakage.

---

## 📦 Installation

Add `ipc` to the `[dependencies]` section in your `alya.toml`:

```toml
[dependencies]
ipc = { git = "https://github.com/alya-lang/ipc", branch = "main" }
```

Or install it directly using the Alya package CLI:

```bash
alya add ipc --git https://github.com/alya-lang/ipc --branch main
alya install
```

### Package Features

| Feature | Default | Description |
|:---|:---:|:---|
| `tcp` | ✅ | TCP loopback channel (`loopback`, `ipc_listen/connect/accept/send/recv/request/serve_once`, `request`). |
| `pipe` | ✅ | Named-pipe transport (`pipe`, `ipc_pipe_listen/connect/accept/send/recv/request/serve_once/poll`). |
| `framing` | ✅ | Length-prefixed wire framing and message constructors (`frame_encode/decode`, `frame_encode/decode_message`, `text_message`, `new_request`, `new_response`). |

Core endpoint/error/statistics vocabulary (`IpcEndpoint`, `IpcError`, `IpcStats`, `new_stats`, `endpoint_is_valid`) always stays available. The `request` facade requires `tcp`; pipe endpoints route through the native engine only when `pipe` is also active.

```bash
# Full build (default)
alya install
alya test

# Slim build (core vocabulary only; helpers become no-ops)
alya install --no-default-features
alya test --no-default-features

# Single-slice builds
alya test --no-default-features --features framing
alya test --no-default-features --features tcp
alya test --no-default-features --features pipe
```

---

## 🚀 Quick Start

```alya
import "ipc" as pkg

function main()
    # 1. Pure framing (no network needed)
    let wire = pkg::frame_encode("hello-ipc")
    say f"Wire: {wire}"

    # 2. Loopback request/reply on one thread
    let server = pkg::ipc_listen(19881, 10)
    let client = pkg::ipc_connect("127.0.0.1", 19881)
    let peer = pkg::ipc_accept(server, 2000)

    pkg::ipc_send(client, "ping")
    let got = pkg::ipc_recv(peer)
    pkg::ipc_send(peer, "pong:" + got)
    say f"Reply: {pkg::ipc_recv(client)}"

    pkg::ipc_close(client)
    pkg::ipc_close(peer)
    pkg::ipc_close(server)

    # 3. Same exchange over a named pipe (native engine)
    let pipe_server = pkg::ipc_pipe_listen("demo")
    let pipe_client = pkg::ipc_pipe_connect("demo")
    let pipe_peer = pkg::ipc_pipe_accept(pipe_server, 2000)

    pkg::ipc_pipe_send(pipe_client, "ping")
    say f"Pipe: {pkg::ipc_pipe_recv(pipe_peer)}"

    pkg::ipc_pipe_close(pipe_client)
    pkg::ipc_pipe_close(pipe_peer)
    pkg::ipc_pipe_close(pipe_server)
end

main()
```

---

## 📖 API Reference

| Symbol | Visibility | Description |
|---|---|---|
| `loopback(port)` | `pub function` | Creates a TCP loopback `IpcEndpoint` for the given port. |
| `pipe(name)` | `pub function` | Creates a named-pipe `IpcEndpoint` for the given channel name. |
| `request(ep, payload, timeout_ms)` | `pub function` | One-shot request routed by transport; returns reply payload. |
| `text_message(payload)` | `pub function` | Creates a plain `Text` message (null payload becomes ""). |
| `new_request(id, payload)` | `pub function` | Creates a `Request` message with the given correlation id. |
| `new_response(id, payload)` | `pub function` | Creates a `Response` message answering the given correlation id. |
| `new_stats()` | `pub function` | Creates a zeroed `IpcStats` record. |
| `endpoint_loopback(port)` | `pub function` | Core constructor for loopback endpoints. |
| `endpoint_pipe(name)` | `pub function` | Constructor for named-pipe endpoints. |
| `endpoint_is_valid(ep)` | `pub function` | True for usable loopback (port 1-65535) or pipe (named) endpoints. |
| `message_text(payload)` | `pub function` | Core constructor for text messages. |
| `message_request(id, payload)` | `pub function` | Core constructor for request messages. |
| `message_response(id, payload)` | `pub function` | Core constructor for response messages. |
| `message_is_valid(msg)` | `pub function` | True for known kinds with a string payload. |
| `stats_new()` | `pub function` | Core constructor for statistics records. |
| `frame_encode(payload)` | `pub function` | Encodes `"len:payload"` wire frame. |
| `frame_decode(buf)` | `pub function` | Decodes first frame; returns `(payload, rest)`, null payload while incomplete. |
| `frame_encode_message(msg)` | `pub function` | Encodes `"kind:id:len:payload"` message frame. |
| `frame_decode_message(buf)` | `pub function` | Decodes first message frame; returns `(message, rest)`. |
| `ipc_listen(port, backlog)` | `pub function` | Opens a TCP loopback listener; -1 when the port is busy. |
| `ipc_accept(server_sock, timeout_ms)` | `pub function` | Accepts next connection; -1 on timeout. |
| `ipc_connect(host, port, timeout_ms)` | `pub function` | Connects to a peer; throws `IpcError` on failure. |
| `ipc_close(sock)` | `pub function` | Closes a socket handle (ignores invalid handles). |
| `ipc_send(sock, payload)` | `pub function` | Sends one framed payload; throws `IpcError` on failure. |
| `ipc_recv(sock, max_bytes)` | `pub function` | Receives one framed payload; "" on close with no frame. |
| `ipc_request(ep, payload, timeout_ms)` | `pub function` | One-shot request over a fresh connection; returns reply payload. |
| `ipc_serve_once(server_sock, handler, timeout_ms)` | `pub function` | Serves one connection via handler; 1 served, 0 on timeout. |
| `ipc_pipe_listen(name)` | `pub function` | Opens a named pipe listener; throws `IpcError` on bad names. |
| `ipc_pipe_accept(server_slot, timeout_ms)` | `pub function` | Accepts next pipe connection; -1 on timeout. |
| `ipc_pipe_connect(name, timeout_ms)` | `pub function` | Connects to a pipe listener; throws `IpcError` on failure. |
| `ipc_pipe_close(slot)` | `pub function` | Closes a pipe slot (ignores invalid handles). |
| `ipc_pipe_poll(slot, timeout_ms)` | `pub function` | 1 when readable, 0 on timeout, -1 on error. |
| `ipc_pipe_send(slot, payload)` | `pub function` | Sends one framed payload; throws `IpcError` on failure. |
| `ipc_pipe_recv(slot, max_bytes, timeout_ms)` | `pub function` | Receives one framed payload; "" on close with no frame. |
| `ipc_pipe_request(name, payload, timeout_ms)` | `pub function` | One-shot pipe request over a fresh connection. |
| `ipc_pipe_serve_once(server_slot, handler, timeout_ms)` | `pub function` | Serves one pipe connection via handler; 1 served, 0 on timeout. |
| `IpcTransport` | `pub enum` | Transport variants (`Loopback = 0`, `Pipe = 1`, `UnixSocket = 2`). |
| `IpcKind` | `pub enum` | Message kinds (`Text = 0`, `Request = 1`, `Response = 2`). |
| `IpcEndpoint` | `pub struct` | Peer location (`transport`, `host`, `port`, `path`). |
| `IpcEndpoint.summary()` | `pub method` | `"host:port"` summary (pipe path for pipe endpoints). |
| `IpcEndpoint.is_valid()` | `pub method` | True for usable loopback endpoints. |
| `IpcMessage` | `pub struct` | Framed message (`kind`, `id`, `payload`). |
| `IpcMessage.summary()` | `pub method` | `"kind#id (n chars)"` summary. |
| `IpcMessage.is_valid()` | `pub method` | True for known kinds with payload. |
| `IpcError` | `pub struct` | Thrown failure (`message`, `code`). |
| `IpcStats` | `pub struct` | Counters (`sent`, `received`, `errors`). |
| `IpcStats.total()` | `pub method` | Sum of sent and received counters. |
| `IpcStats.record_sent()` | `pub method` | Increments the sent counter. |
| `IpcStats.record_received()` | `pub method` | Increments the received counter. |
| `IpcStats.record_error()` | `pub method` | Increments the error counter. |

> [!TIP]
> **Internal Helpers & Documentation:** Public symbols are documented with `##` Markdown docstrings, enabling automatic API documentation generation via `alya doc`. v0.1 supports one frame per connection direction; persistent multi-message streams are planned.

---

## 🧪 Running Tests & Benchmarks

Run the automated test suite using `alya test`:

```bash
alya test
```

Generate static API documentation:

```bash
alya doc . -o docs --markdown
```

Run the benchmark suite:

```bash
alya run benches/bench_basic.alya
```

Run the example demo:

```bash
alya run examples/demo.alya
```

Check code formatting:

```bash
alya fmt . --check
```

Run static code linter:

```bash
alya lint . --check
```

---

### 💻 Developer Tooling & VS Code Integration

This package comes preconfigured with recommended workspace settings and tasks for **Visual Studio Code**:
- **LSP & Formatting**: Auto-formatting on save and real-time Language Server diagnostics via `alya-lang.vscode-alya`.
- **DAP Debugging**: Launch configurations in `.vscode/launch.json` ready for interactive step-debugging via `F5`.
- **Predefined Tasks**: Press `Ctrl+Shift+B` or run tasks (`Test`, `Lint`, `Format`, `Build Docs`) directly from the Command Palette.

---

## 🤝 Contributing

Contributions are welcome! Please follow these steps:

1. Fork the repository and clone it locally
2. Install dependencies:
   ```bash
   alya install
   ```
3. Create your feature branch (`git checkout -b feature/my-feature`)
4. Verify tests and formatting before opening a PR:
   ```bash
   alya test
   ```
5. Commit your changes (`git commit -m "feat: add feature"`) and open a Pull Request

---

## 📄 License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

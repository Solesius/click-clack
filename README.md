<p align="center">
  <img src="logo.svg" alt="click-clack logo: two horseshoe magnets clicking together" width="400"/>
</p>

# click-clack

[![CI](https://github.com/Solesius/click-clack/actions/workflows/ci.yml/badge.svg?branch=main&event=push)](https://github.com/Solesius/click-clack/actions/workflows/ci.yml?query=branch%3Amain+event%3Apush)
[![Coverage](https://github.com/Solesius/click-clack/actions/workflows/coverage.yml/badge.svg?branch=main&event=push)](https://solesius.github.io/click-clack/)
[![License: Apache 2.0](https://img.shields.io/badge/license-Apache%202.0-blue.svg)](LICENSE)
[![C++23](https://img.shields.io/badge/C%2B%2B-23-blue.svg)](https://en.cppreference.com/w/cpp/23)
[![npm](https://img.shields.io/npm/v/%40solesius-oss%2Fclick-clack-mcp.svg)](https://www.npmjs.com/package/@solesius-oss/click-clack-mcp)
[![Angular 20](https://img.shields.io/badge/Angular-20-dd0031.svg)](https://angular.dev)

> **MCP server for agents** → `bun add -g @solesius-oss/click-clack-mcp`  
> npm: [npmjs.com/package/@solesius-oss/click-clack-mcp](https://www.npmjs.com/package/@solesius-oss/click-clack-mcp)

A local-first write-ahead ledger (WAL) for coordinating autonomous agents.

`click-clack` is a single-process hub that accepts append-only **clacks**
(structured JSON-RPC events) from any number of agents or humans, materializes
live views (timeline, tasks, HITL queue, presence, pins, threads, artifacts),
and exposes them via an HTTP+WebSocket API and an MCP (Model Context Protocol)
tool surface.

It is designed to be boring, deterministic, and local-first: one binary, one
append-only log, no external broker, no eventual consistency.

<img width="1273" height="341" alt="Screenshot 2026-04-22 114934" src="https://github.com/user-attachments/assets/4787913c-a7fd-436a-9464-7c29fc02d977" />

## Features

- **Append-only WAL** over a composite-tree storage engine with CRC32C-checked
  frames and monotonic epochs.
- **MCP server** exposing tools like `cc.post_clack`, `cc.claim_task`,
  `cc.query_timeline`, `cc.query_epoch`, `cc.query_thread`,
  `cc.query_tasks`, `cc.query_presence`, `cc.vote_pin`, `cc.pin_override`,
  `cc.ask`, `cc.answer`, `cc.wait`, and more — usable from any MCP-capable
  client (Claude Desktop, VS Code Copilot, Zed, etc.).
- **HTTP + WebSocket API** (`/api/v1/mcp/call`, `/ws/v1/hub`) with live view
  subscriptions (timeline / tasks / agents / hitl).
- **Angular operator console** for inspecting the ledger in real time, posting
  clacks, and pinning important events by peer vote or manual override.
- **Self-hosting npm MCP package** (`@solesius-oss/click-clack-mcp`) that boots
  the hub in-process via FFI — no separate backend daemon required.
- **Repo stdio bridge** (`./build/click-clack-mcp`) for development setups
  where a long-running hub already exists and editors need a stdio MCP shim.

## Repository layout

```
click-clack/
├── CMakeLists.txt
├── backend/                 C++23 hub, MCP server, stdio bridge
│   ├── include/click_clack/
│   └── src/
├── frontend/                Angular 20 operator console
│   └── src/app/
└── scripts/
```

## Build

Requirements: CMake ≥ 3.25, a C++23 compiler (clang 17+ / gcc 13+), Node 20+.

```sh
# backend
cmake -S . -B build -DCC_BUILD_TESTS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure

# frontend
cd frontend
npm install
npm start -- --port 3008   # dev server at http://localhost:3008
```

The hub listens on `:33514` by default and serves `/api/v1/*`, `/ws/v1/hub`,
and `/mcp`.

## Running the hub locally

```sh
./build/click-clack-hub
```

The local hub listens on `:33514` by default and serves:

- `POST /mcp` — streamable HTTP MCP endpoint
- `POST /api/v1/mcp/call` — JSON MCP call endpoint
- `GET /ws/v1/hub` — WebSocket live updates

For repo development, point an MCP-capable client at `./build/click-clack-mcp`
(stdio bridge) or at `http://localhost:33514/mcp`.

## Using the npm package

```sh
bun add -g @solesius-oss/click-clack-mcp
click-clack-mcp
```

The published package is the self-hosting MCP server. It boots the hub
in-process via the native FFI library and stores data under:

- `CC_DATA_ROOT` (default: `<cwd>/.click-clack`)
- `CC_WAL_PATH` / `CC_VIEWS_PATH` for explicit overrides
- `CLICK_CLACK_LIB` to point at a specific native `libclickclack` binary

This is different from the repo-built `./build/click-clack-mcp`, which is only
an HTTP bridge to an already-running hub.

## Platform support

| Platform       | Native lib           | RocksDB handling                          |
|----------------|----------------------|-------------------------------------------|
| `linux-x64`    | `libclickclack.so`   | **statically linked** via `celer`         |
| `darwin-arm64` | `libclickclack.dylib`| **dylib bundled** alongside the native lib (rewritten to `@loader_path`) |
| `win32-x64`    | _not yet published_  | planned — see [issues](https://github.com/Solesius/click-clack/issues) |

No system RocksDB installation is required to run the npm package.

## Common MCP queries

- `cc.query_timeline` — read a filtered range of clacks
- `cc.query_epoch` — fetch one exact clack by epoch
- `cc.query_thread` — walk a thread rooted at a specific epoch
- `cc.query_task` / `cc.query_tasks` — inspect materialized task state
- `cc.query_presence` — inspect agent presence
- `cc.wait` — block until new activity arrives

## License

Apache-2.0 — see [LICENSE](LICENSE).

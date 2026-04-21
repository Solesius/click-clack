# @click-clack/bun

Bun FFI bindings for the [click-clack](../../) MCP hub. Zero C++ required —
just boot a hub from TypeScript and start clacking.

## Install

```bash
bun add @click-clack/bun
# or during development, inside this repo:
bun install
bun run build:native   # compiles libclickclack.<so|dylib|dll> into ./native/
```

The package loads the shared library in this order:

1. `$CLICK_CLACK_LIB` (absolute path override).
2. `./native/libclickclack.<suffix>` bundled with the package.
3. `<repo>/build/libclickclack.<suffix>` (in-tree dev builds).
4. Whatever the OS loader finds on `LD_LIBRARY_PATH` / `PATH`.

## Quickstart

```ts
import { ClickClackHub } from '@click-clack/bun'

const hub = ClickClackHub.open({
  walPath:   './data/wal',
  viewsPath: './data/views',
})

try {
  hub.postClack({
    verb:    'ANNOUNCE',
    taskId:  'release-notes',
    subject: 'Draft v1.0 changelog',
    as:      'alice',
  })

  hub.claimTask('release-notes', 'bob')
  hub.reportProgress('release-notes', 50, 'halfway', 'bob')
  hub.completeTask('release-notes', 'shipped', 'bob')

  console.log(hub.queryTask('release-notes'))
} finally {
  hub.close()
}
```

## API

- `ClickClackHub.open({ walPath, viewsPath })` — boot the hub.
- `hub.close()` — release native resources (idempotent).
- `hub.listTools()` — every registered MCP tool name.
- `hub.call<T>(tool, args, agentId?)` — raw JSON dispatch; works with any tool.
- Sugar: `postClack`, `claimTask`, `reportProgress`, `completeTask`,
  `queryTask`, `queryTimeline`, `wait`.
- `ClickClackHub.nativeVersion()` — version baked into `libclickclack`.

## Constraints

- **Single hub per process.** The underlying celer store is a global singleton;
  closing one hub releases it so you can open another, but two open at once is
  undefined behaviour.
- **JSON-only payloads.** Every tool takes a JSON object in, returns JSON out —
  use `hub.call(tool, args)` for anything not yet sugared.

## Development

```bash
bun run build:native    # cmake --build build-ffi -t click_clack_ffi
bun test                # exercises the FFI surface end-to-end
bun run example         # examples/hello.ts
```

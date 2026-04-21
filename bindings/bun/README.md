# click-clack-mcp

The [click-clack](https://github.com/Solesius/click-clack) coordination hub
packaged as a **stdio MCP server** plus a typed Bun FFI client. Zero C++
required — one `bun add -g` and you have an MCP server any client can spawn.

Supported platforms: **linux-x64**, **darwin-arm64**. Prebuilt `libclickclack`
ships in the npm tarball, so there's no compiler dance on install.

## Install

```bash
bun add -g click-clack-mcp        # global → click-clack-mcp binary on $PATH
# or project-local:
bun add click-clack-mcp
```

From this monorepo (dev):

```bash
bun install
bun run build:native              # compiles libclickclack.<so|dylib> into ./native/<plat>/
```

Native library resolution order:

1. `$CLICK_CLACK_LIB` (absolute path override).
2. `./native/<platform>-<arch>/libclickclack.<suffix>` (shipped prebuild).
3. `./native/libclickclack.<suffix>` (flat legacy layout).
4. `<repo>/build/libclickclack.<suffix>` (in-tree dev builds).
5. Whatever the OS loader finds on `LD_LIBRARY_PATH` / `PATH`.

## Start an MCP server from anywhere

Once installed globally you get a `click-clack-mcp` binary. It speaks
line-delimited JSON-RPC 2.0 over stdin/stdout — point any MCP client
(VS Code, Claude Desktop, Cursor, …) at it:

Once installed globally you get a `click-clack-mcp` binary on your `$PATH`.
It speaks line-delimited JSON-RPC 2.0 over stdin/stdout — point any MCP
client (VS Code, Claude Desktop, Cursor, …) at it:

```json
{
  "mcpServers": {
    "click-clack": {
      "command": "click-clack-mcp",
      "env": {
        "CC_DATA_ROOT": "${HOME}/.click-clack"
      }
    }
  }
}
```

Environment variables (all optional):

| Var              | Default                               |
| ---------------- | ------------------------------------- |
| `CC_DATA_ROOT`   | `<cwd>/.click-clack`                  |
| `CC_WAL_PATH`    | `<CC_DATA_ROOT>/wal`                  |
| `CC_VIEWS_PATH`  | `<CC_DATA_ROOT>/views`                |
| `CLICK_CLACK_LIB`| explicit path to `libclickclack.*`    |

## Library quickstart

```ts
import { ClickClackHub } from 'click-clack-mcp'

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
- `hub.jsonrpc(line)` — feed a single JSON-RPC frame, get the response string
  (or `""` for notifications). The primitive behind `click-clack-mcp`.
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

## Publishing to npm

Releases are cut by pushing a git tag:

```bash
# from the repo root, after bumping bindings/bun/package.json
git tag click-clack-mcp-v0.1.0
git push origin click-clack-mcp-v0.1.0
```

The [`publish-npm.yml`](../../.github/workflows/publish-npm.yml) workflow
builds `libclickclack` on a Linux-x64 and a macOS-arm64 runner, collects
both prebuilds under `native/<platform>-<arch>/`, then runs
`npm publish --access public` using the `NPM_TOKEN` secret.

For a dry run, trigger the workflow manually with `dry_run=true`.



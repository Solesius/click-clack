#!/usr/bin/env bun
// click-clack-mcp — stdio MCP server.
//
// Reads JSON-RPC 2.0 frames one per line on stdin, dispatches them
// through the native hub via FFI, and writes responses one per line
// on stdout. Notifications yield no output. All logs go to stderr so
// stdout stays reserved for the protocol.
//
// Configuration via environment variables:
//   CC_WAL_PATH    default: <cwd>/.click-clack/wal
//   CC_VIEWS_PATH  default: <cwd>/.click-clack/views
//   CLICK_CLACK_LIB  absolute path to libclickclack.<suffix>

import { mkdirSync } from 'node:fs'
import { join, resolve } from 'node:path'
import { dlopen, FFIType, ptr, suffix } from 'bun:ffi'
import { fileURLToPath } from 'node:url'
import { dirname, join as pjoin } from 'node:path'
import { existsSync } from 'node:fs'

// Resolve the native library the same way src/index.ts does, but
// inline so this CLI has zero import-time side effects until FFI.
const HERE = dirname(fileURLToPath(import.meta.url))
function resolveLib(): string {
  if (process.env.CLICK_CLACK_LIB && existsSync(process.env.CLICK_CLACK_LIB)) {
    return process.env.CLICK_CLACK_LIB
  }
  const libName = `libclickclack.${suffix}`
  const platArch = `${process.platform}-${process.arch}`
  const candidates = [
    resolve(HERE, '..', 'native', platArch, libName),
    resolve(HERE, '..', 'native', libName),
    resolve(HERE, '..', '..', '..', 'build', libName),
  ]
  for (const c of candidates) if (existsSync(c)) return c
  return libName
}

const { symbols } = dlopen(resolveLib(), {
  cc_hub_open:     { args: [FFIType.cstring],                returns: FFIType.ptr },
  cc_hub_close:    { args: [FFIType.ptr],                    returns: FFIType.void },
  cc_hub_jsonrpc:  { args: [FFIType.ptr, FFIType.cstring],   returns: FFIType.cstring },
  cc_string_free:  { args: [FFIType.ptr],                    returns: FFIType.void },
  cc_last_error:   { args: [],                               returns: FFIType.cstring },
})

const enc = new TextEncoder()
function cstr(s: string): Uint8Array {
  const b = enc.encode(s)
  const out = new Uint8Array(b.length + 1)
  out.set(b); out[b.length] = 0
  return out
}

function bootHub(): number {
  const root = process.env.CC_DATA_ROOT ?? pjoin(process.cwd(), '.click-clack')
  const walPath   = process.env.CC_WAL_PATH   ?? pjoin(root, 'wal')
  const viewsPath = process.env.CC_VIEWS_PATH ?? pjoin(root, 'views')
  mkdirSync(walPath,   { recursive: true })
  mkdirSync(viewsPath, { recursive: true })

  const cfg = JSON.stringify({ wal_path: walPath, views_path: viewsPath })
  const h = symbols.cc_hub_open(ptr(cstr(cfg))) as unknown as number
  if (!h) {
    const err = symbols.cc_last_error()?.toString() ?? 'unknown'
    process.stderr.write(`click-clack-mcp: cc_hub_open failed: ${err}\n`)
    process.exit(2)
  }
  process.stderr.write(`click-clack-mcp: hub ready (wal=${walPath}, views=${viewsPath})\n`)
  return h
}

const hub = bootHub()

function shutdown(): never {
  symbols.cc_hub_close(hub as unknown as number)
  process.exit(0)
}
process.on('SIGINT',  shutdown)
process.on('SIGTERM', shutdown)

function dispatchLine(line: string): void {
  if (!line.trim()) return
  const retPtr = symbols.cc_hub_jsonrpc(
    hub as unknown as number,
    ptr(cstr(line)),
  )
  if (!retPtr) return
  const raw = retPtr.toString()
  symbols.cc_string_free((retPtr as unknown as { ptr: number }).ptr as unknown as number)
  if (raw) process.stdout.write(raw + '\n')
}

// Line-delimited JSON-RPC framing over stdin.
let buf = ''
const input = Bun.stdin.stream().getReader()

while (true) {
  const { value, done } = await input.read()
  if (done) break
  buf += Buffer.from(value).toString('utf8')
  let nl: number
  while ((nl = buf.indexOf('\n')) !== -1) {
    const line = buf.slice(0, nl)
    buf = buf.slice(nl + 1)
    try { dispatchLine(line) }
    catch (e) { process.stderr.write(`click-clack-mcp: dispatch error: ${String(e)}\n`) }
  }
}

if (buf.trim()) dispatchLine(buf)
shutdown()

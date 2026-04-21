// ──────────────────────────────────────────────────────────────
// @click-clack/bun — typed Bun FFI wrapper for libclickclack.so
//
// The native library is a thin C ABI over the hub's MCP dispatcher.
// Every tool call is JSON in / JSON out, so adding a new tool on
// the backend is immediately callable from here without changes.
// ──────────────────────────────────────────────────────────────

import { dlopen, FFIType, ptr, CString, suffix } from 'bun:ffi'
import { existsSync } from 'node:fs'
import { fileURLToPath } from 'node:url'
import { dirname, join, resolve } from 'node:path'

// ── Native library resolution ──────────────────────────────────
//
// Resolution order:
//   1. $CLICK_CLACK_LIB                          — explicit absolute path
//   2. ./native/<platform>-<arch>/libclickclack  — prebuild for this host
//   3. ./native/libclickclack.<suffix>           — flat prebuild (legacy)
//   4. <repo>/build/libclickclack.<suffix>       — in-tree dev build
//   5. libclickclack.<suffix>                    — loader search (LD_LIBRARY_PATH)
const HERE = dirname(fileURLToPath(import.meta.url))

function resolveLibPath(): string {
  const fromEnv = process.env.CLICK_CLACK_LIB
  if (fromEnv && existsSync(fromEnv)) return fromEnv

  const libName = `libclickclack.${suffix}`
  const platArch = `${process.platform}-${process.arch}`
  const candidates = [
    resolve(HERE, '..', 'native', platArch, libName),
    resolve(HERE, '..', 'native', libName),
    resolve(HERE, '..', '..', '..', 'build', libName),
  ]
  for (const c of candidates) if (existsSync(c)) return c

  // Fall back: let dlopen search the loader path.
  return libName
}

const LIB_PATH = resolveLibPath()

const { symbols: S } = dlopen(LIB_PATH, {
  cc_hub_open:      { args: [FFIType.cstring],                               returns: FFIType.ptr },
  cc_hub_close:     { args: [FFIType.ptr],                                   returns: FFIType.void },
  cc_hub_dispatch:  { args: [FFIType.ptr, FFIType.cstring, FFIType.cstring, FFIType.cstring], returns: FFIType.cstring },
  cc_hub_list_tools:{ args: [FFIType.ptr],                                   returns: FFIType.cstring },
  cc_hub_jsonrpc:   { args: [FFIType.ptr, FFIType.cstring],                  returns: FFIType.cstring },
  cc_string_free:   { args: [FFIType.ptr],                                   returns: FFIType.void },
  cc_last_error:    { args: [],                                              returns: FFIType.cstring },
  cc_version:       { args: [],                                              returns: FFIType.cstring },
})

// ── Helpers ────────────────────────────────────────────────────

const encoder = new TextEncoder()

function toCString(s: string): Uint8Array {
  // NUL-terminate explicitly so the FFI sees a proper C string.
  const bytes = encoder.encode(s)
  const out = new Uint8Array(bytes.length + 1)
  out.set(bytes)
  out[bytes.length] = 0
  return out
}

function readAndFree(retPtr: CString | null): string {
  if (!retPtr) return ''
  const raw = (retPtr as unknown as { ptr: number }).ptr
  const str = retPtr.toString()
  // libclickclack hands ownership to the caller; we must free via
  // cc_string_free so the heap it was malloc'd on matches.
  S.cc_string_free(raw as unknown as number)
  return str
}

function lastError(): string {
  const e = S.cc_last_error()
  return e ? e.toString() : ''
}

// ── Public surface ────────────────────────────────────────────

/** Configuration for opening a hub. Relative paths are resolved by the OS. */
export interface HubConfig {
  /** Write-ahead-log directory. Created if missing. */
  walPath: string
  /** Materialised-views directory. Created if missing. */
  viewsPath: string
}

/** Narrow JSON type — matches what the backend emits/accepts. */
export type Json =
  | null
  | boolean
  | number
  | string
  | Json[]
  | { [k: string]: Json }

/** Error raised when a native call fails before a JSON response is produced. */
export class ClickClackError extends Error {
  constructor(message: string) {
    super(message)
    this.name = 'ClickClackError'
  }
}

export class ClickClackHub {
  #handle: number | null

  private constructor(handle: number) {
    this.#handle = handle
  }

  /** Open a hub rooted at the given paths. Only one hub per process. */
  static open(config: HubConfig): ClickClackHub {
    const cfg = JSON.stringify({
      wal_path:   config.walPath,
      views_path: config.viewsPath,
    })
    const h = S.cc_hub_open(ptr(toCString(cfg))) as unknown as number
    if (!h) {
      throw new ClickClackError(`cc_hub_open failed: ${lastError() || 'unknown error'}`)
    }
    return new ClickClackHub(h)
  }

  /** Release all native resources. Safe to call twice. */
  close(): void {
    if (this.#handle !== null) {
      S.cc_hub_close(this.#handle as unknown as number)
      this.#handle = null
    }
  }

  /** Dispatch an MCP tool. Returns the parsed JSON response. */
  call<T extends Json = Json>(
    tool: string,
    args: Record<string, Json> = {},
    agentId = 'bun-client',
  ): T {
    this.#assertOpen()
    const retPtr = S.cc_hub_dispatch(
      this.#handle as unknown as number,
      ptr(toCString(tool)),
      ptr(toCString(JSON.stringify(args))),
      ptr(toCString(agentId)),
    ) as unknown as CString | null
    const raw = readAndFree(retPtr)
    if (!raw) throw new ClickClackError(`cc_hub_dispatch returned null: ${lastError()}`)
    return JSON.parse(raw) as T
  }

  /** List every registered MCP tool name. */
  listTools(): { name: string }[] {
    this.#assertOpen()
    const retPtr = S.cc_hub_list_tools(this.#handle as unknown as number) as unknown as CString | null
    const raw = readAndFree(retPtr)
    if (!raw) throw new ClickClackError(`cc_hub_list_tools failed: ${lastError()}`)
    return JSON.parse(raw) as { name: string }[]
  }

  /** Version string baked into the native library. */
  static nativeVersion(): string {
    const v = S.cc_version()
    return v ? v.toString() : ''
  }

  /**
   * Feed one JSON-RPC 2.0 frame to the hub and return the serialized
   * response (or an empty string for notifications). This is the
   * primitive behind the `click-clack-mcp` stdio CLI.
   */
  jsonrpc(line: string): string {
    this.#assertOpen()
    const retPtr = S.cc_hub_jsonrpc(
      this.#handle as unknown as number,
      ptr(toCString(line)),
    ) as unknown as CString | null
    return readAndFree(retPtr)
  }

  // ── Sugar: the most common tools as first-class methods ─────

  postClack(click: {
    verb: string
    taskId?: string
    subject?: string
    flags?: number
    parent?: number
    payload?: Record<string, Json>
    as?: string
  }): { epoch: number; task_id: string; task_id_generated?: boolean } {
    const { as, taskId, payload, ...rest } = click
    return this.call('cc.post_clack', {
      ...rest,
      task_id: taskId,
      payload: payload ?? {},
      ...(as ? { as_agent: as } : {}),
    } as Record<string, Json>)
  }

  claimTask(taskId: string, as?: string) {
    return this.call('cc.claim_task', { task_id: taskId, ...(as ? { as_agent: as } : {}) })
  }

  reportProgress(taskId: string, pct: number, summary = '', as?: string) {
    return this.call('cc.report_progress', {
      task_id: taskId, pct, summary,
      ...(as ? { as_agent: as } : {}),
    })
  }

  completeTask(taskId: string, summary = '', as?: string) {
    return this.call('cc.complete_task', {
      task_id: taskId, summary,
      ...(as ? { as_agent: as } : {}),
    })
  }

  queryTask(taskId: string) {
    return this.call('cc.query_task', { task_id: taskId })
  }

  queryTimeline(opts: { sinceEpoch?: number; limit?: number; verbs?: string[] } = {}) {
    return this.call('cc.query_timeline', {
      since_epoch: opts.sinceEpoch ?? 0,
      limit: opts.limit ?? 100,
      ...(opts.verbs ? { verbs: opts.verbs } : {}),
    } as Record<string, Json>)
  }

  /** Block until a matching clack arrives, or timeout. Great for agent loops. */
  wait(opts: {
    taskId?: string
    agentId?: string
    verbs?: string[]
    sinceEpoch?: number
    timeoutMs?: number
    limit?: number
  } = {}): { clacks: Json[]; timed_out: boolean } {
    return this.call('cc.wait', {
      ...(opts.taskId  ? { task_id:  opts.taskId }  : {}),
      ...(opts.agentId ? { agent_id: opts.agentId } : {}),
      ...(opts.verbs   ? { verbs:    opts.verbs }   : {}),
      since_epoch: opts.sinceEpoch ?? 0,
      timeout_ms:  opts.timeoutMs  ?? 30000,
      limit:       opts.limit      ?? 64,
    } as Record<string, Json>)
  }

  #assertOpen(): void {
    if (this.#handle === null) {
      throw new ClickClackError('hub is closed')
    }
  }
}

export const nativeLibraryPath = LIB_PATH

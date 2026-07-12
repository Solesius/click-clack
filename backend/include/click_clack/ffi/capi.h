// ──────────────────────────────────────────────────────────────
// click-clack / ffi / capi.h
// Stable C ABI for embedding the click-clack hub from any
// language with C FFI (Bun, Node, Python, Ruby, Rust, Go, ...).
//
// All strings are NUL-terminated UTF-8. Strings returned by the
// library are heap-allocated by the library and MUST be freed
// with cc_string_free.
//
// Threading: a single cc_hub_t owns a celer store. The underlying
// celer runtime is a process-wide singleton, so at most ONE
// cc_hub_t can be open at a time per process. Attempting to open
// a second hub before closing the first returns NULL and sets
// cc_last_error.
// ──────────────────────────────────────────────────────────────
#ifndef CLICK_CLACK_CAPI_H
#define CLICK_CLACK_CAPI_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

typedef struct cc_hub cc_hub_t;

/*
 * Open a hub.
 *
 * `config_json` is a JSON object with string fields:
 *   {
 *     "wal_path":   "/tmp/cc/wal",
 *     "views_path": "/tmp/cc/views"
 *   }
 *
 * Returns a non-NULL handle on success, or NULL on failure.
 * On failure, cc_last_error() returns a human-readable message.
 */
cc_hub_t* cc_hub_open(const char* config_json);

/*
 * Close a hub and release all resources. Safe to call with NULL.
 * After return, the celer global store is released and another
 * hub may be opened.
 */
void cc_hub_close(cc_hub_t* hub);

/*
 * Dispatch an MCP tool call.
 *
 * Returns a heap-allocated JSON string that the caller MUST free
 * with cc_string_free. On dispatch failure the returned JSON is
 * an object with an "error" field.
 *
 * Returns NULL only if `hub` is NULL or out-of-memory; check
 * cc_last_error() in that case.
 */
char* cc_hub_dispatch(cc_hub_t*   hub,
                      const char* tool_name,
                      const char* args_json,
                      const char* agent_id);

/*
 * Return a JSON array describing every registered tool.
 * Caller must free with cc_string_free.
 */
char* cc_hub_list_tools(cc_hub_t* hub);

/*
 * Handle one line of JSON-RPC 2.0 (Model Context Protocol over stdio).
 *
 * `line_json` is exactly one JSON-RPC envelope: request, notification,
 * or response. The hub dispatches the envelope and returns:
 *   - A heap JSON string containing the response envelope (for requests).
 *   - An empty heap string "" for notifications or responses (no reply).
 *
 * Session state (initialize handshake, caller agent id) is retained
 * across calls on the same hub handle, making this a complete stdio
 * MCP server when paired with a simple stdin-to-stdout loop.
 *
 * Caller must free the returned string with cc_string_free.
 */
char* cc_hub_jsonrpc(cc_hub_t* hub, const char* line_json);

/*
 * Free a string previously returned by this library.
 * Safe to call with NULL.
 */
void cc_string_free(char* s);

/*
 * Return the last error message recorded on the calling thread,
 * or an empty string if none.
 *
 * WARNING: the returned pointer aliases a thread-local std::string and
 * is invalidated by the NEXT cc_* call on the same thread. Prefer
 * cc_error_copy() for anything beyond an immediate synchronous read.
 */
const char* cc_last_error(void);

/*
 * Copy the current thread-local error into the caller's buffer.
 *
 * The message is NUL-terminated if `dst` has at least one byte of
 * capacity, and truncated to `cap - 1` bytes if it would not fit.
 *
 * Returns the full source length in bytes (excluding NUL), so callers
 * can detect truncation by comparing to `cap`. Returns 0 with `dst[0] = 0`
 * when no error is set or when `dst == NULL`.
 *
 * This API is safe across async FFI boundaries; the pointer returned by
 * cc_last_error() is NOT.
 */
size_t cc_error_copy(char* dst, size_t cap);

/*
 * Return the library version, e.g. "0.1.0".
 */
const char* cc_version(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* CLICK_CLACK_CAPI_H */

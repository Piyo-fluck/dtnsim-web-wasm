/* dtnsim_api_js.h
 * Minimal, WASM-friendly interface for JS to read node positions (zero-copy).
 *
 * Usage:
 *  - Call dtnsim_init(n) to create simulator with n agents (0 = default).
 *  - Call dtnsim_step(delta) each frame to advance simulation.
 *  - Read positions via dtnsim_get_node_positions() (returns pointer to metadata in WASM memory).
 *  - positions_ptr inside the metadata is a byte offset into the WASM linear memory and is used for zero-copy typed arrays on the JS side.
 *
 * Safety notes:
 *  - JS must check returned pointers and bounds before creating typed arrays.
 *  - Version is bumped when the contiguous buffer changes; use it to detect invalidation.
 */

#ifndef DTNSIM_API_JS_H
#define DTNSIM_API_JS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Packed metadata (16 bytes) describing the positions buffer */
typedef struct NodePositionsBuffer {
    uint32_t positions_ptr; /* byte offset into linear memory (0 => unavailable) */
    uint32_t count;         /* number of nodes */
    uint32_t stride;        /* bytes per entry (e.g., 12 for 3x float32) */
    uint32_t version;       /* incremented when buffer contents are refreshed */
} NodePositionsBuffer;

/* Optional: interleaved per-node buffer (id + position + other fields) */
typedef struct NodeBuffer {
    uint32_t nodes_ptr; /* byte offset to first node record (0 => unavailable) */
    uint32_t count;
    uint32_t stride;    /* bytes per record (e.g., 24) */
    uint32_t version;
} NodeBuffer;

/* Initialization / lifetime */
int dtnsim_init(uint32_t n_agents);    /* 0 => use default agent count. Returns 0 on success, negative on error */
void dtnsim_shutdown(void);            /* cleanup */

/* Time stepping */
int dtnsim_step(float delta_seconds);  /* Advance simulation by delta_seconds. Returns 0 on success. */
int dtnsim_resize(uint32_t new_n_agents); /* Optional: reconfigure number of agents. Returns 0 on success. */

/* Accessors (return pointer *within* module memory) */
const NodePositionsBuffer* dtnsim_get_node_positions(void); /* returns pointer to metadata struct in WASM heap (or NULL) */
int dtnsim_snapshot_positions(void); /* force refresh/pack of the contiguous positions buffer; returns 0 on success */

const NodeBuffer* dtnsim_get_node_buffer(void); /* optional interleaved node records */

/* Notes:
 *  - All pointers/offsets are native WASM linear memory offsets (byte addresses).
 *  - The module owns memory; JS must not attempt to free/modify allocation. JS creates typed-array views (Float32Array) over Module.HEAPU8.buffer using positions_ptr.
 */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* DTNSIM_API_JS_H */
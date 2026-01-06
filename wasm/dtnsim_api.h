#ifndef DTNSIM_API_H
#define DTNSIM_API_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * dtnsim_api.h
 * Stable, header-only ABI for dtnsim WASM integration.
 * - Plain C types only (C99/C11 compatible)
 * - Only includes <stdint.h>
 * - All pointers exposed to JS are represented as 32-bit byte offsets
 *   into the WebAssembly linear memory (wasm32). Do NOT assume host
 *   pointer size beyond wasm32.
 *
 * Ownership, alignment, lifetime rules:
 * - WASM (the dtnsim implementation) owns all buffers and memory.
 * - JS must only read memory via typed array views over the wasm linear
 *   memory (Float32Array, Uint32Array) and must NOT free WASM memory.
 * - Buffers are preallocated by dtnsim_init() (or dtnsim_resize()).
 * - No per-frame malloc/free across the JS/WASM boundary.
 * - All structs are 4-byte aligned and sizes are multiples of 4.
 */

#include <stdint.h>

/* API version */
#define DTNSIM_API_VERSION 1u

/* Positions stride is fixed: 3 float32 values (x,y,z) = 12 bytes */
#define DTNSIM_POSITIONS_STRIDE_BYTES 12u

/*
 * Roles:
 * - NodePositionsBuffer is the canonical, fast path for rendering. It
 *   exposes a contiguous float32 XYZ buffer optimized for direct mapping
 *   into WebGL vertex buffers (zero-copy).
 * - NodeBuffer is a secondary metadata descriptor for optional
 *   interleaved per-node payloads; it is intended for debugging and
 *   future extensions and is NOT required for the fast rendering path.
 */

/*
 * NodePositionsBuffer
 * - positions_ptr: byte offset into wasm memory to float32 positions
 *   laid out exactly as x0,y0,z0, x1,y1,z1, ... (count * 3 floats).
 *   If positions_ptr == 0, the positions buffer is not present.
 * - ids_ptr: byte offset into wasm memory to uint32 ids[count] (optional).
 *   If ids_ptr == 0, the ids buffer is not present.
 * - count: number of nodes
 * - positions_stride: MUST be exactly DTNSIM_POSITIONS_STRIDE_BYTES (12)
 *   (future per-node attributes must be exposed via separate buffers,
 *   not by changing this layout).
 * - version: monotonic incrementing counter; MUST increment when and
 *   only when the underlying buffer content changes. JS clients may use
 *   this to skip GPU uploads when the version hasn't changed.
 */
typedef struct {
    uint32_t positions_ptr;    /* byte offset -> float32 positions, 0 == not present */
    uint32_t ids_ptr;          /* byte offset -> uint32 ids[], 0 == not present */
    uint32_t count;            /* number of nodes */
    uint32_t positions_stride; /* MUST be DTNSIM_POSITIONS_STRIDE_BYTES (12) */
    uint32_t version;          /* inc on content change; JS may skip uploads */
    uint32_t reserved;         /* reserved for future use / alignment */
} NodePositionsBuffer;

_Static_assert(sizeof(NodePositionsBuffer) % 4 == 0, "NodePositionsBuffer must be 4-byte aligned");

/*
 * Node (suggested interleaved payload for debugging / extensions)
 * This struct is only a suggestion for implementations that provide an
 * interleaved Node buffer. Consumers must rely on NodeBuffer metadata
 * to locate the Node array; they must NOT assume NodeBuffer is always
 * present.
 */
typedef struct {
    uint32_t id;
    float    x;
    float    y;
    float    z;
    uint32_t state;
    uint32_t reserved; /* pad to multiple of 4 bytes */
} Node;

_Static_assert(sizeof(Node) % 4 == 0, "Node must be 4-byte aligned");

/*
 * NodeBuffer
 * - Metadata describing an interleaved per-node payload (e.g. Node[]).
 * - This is a metadata-only struct: it does NOT copy node payloads.
 * - nodes_ptr == 0 means the interleaved node buffer is not present.
 * - stride is bytes per node element (typically sizeof(Node) if present).
 * - version: same semantics as in NodePositionsBuffer: increment only when
 *   the underlying payload changes, allowing JS to avoid redundant work.
 */
typedef struct {
    uint32_t nodes_ptr; /* byte offset -> Node nodes[]; 0 == not present */
    uint32_t count;
    uint32_t stride;    /* bytes per node element */
    uint32_t version;   /* inc on content change only */
} NodeBuffer;

_Static_assert(sizeof(NodeBuffer) == 16, "NodeBuffer metadata must be 16 bytes");

/*
 * World bounds (optional helper for camera framing)
 */
typedef struct {
    float    minx, miny, minz;
    float    maxx, maxy, maxz;
    uint32_t version;   /* inc when bounds change */
    uint32_t reserved;
} WorldBounds;

_Static_assert(sizeof(WorldBounds) % 4 == 0, "WorldBounds must be 4-byte aligned");

/*
 * Function prototypes — plain C ABI.
 * Return codes: 0 = success, non-zero = error (implementation-defined)
 *
 * Notes on ownership and presence:
 * - Returned pointers point to data owned by WASM and must NOT be freed
 *   by the host. The pointers themselves point into wasm linear memory.
 * - The returned struct pointers are stable and non-NULL; their internal
 *   ptr fields (positions_ptr, ids_ptr, nodes_ptr) may be 0 to indicate
 *   a buffer is not present.
 */

/* Initialize simulation and preallocate buffers for up to max_nodes. */
int  dtnsim_init(uint32_t max_nodes);

/* Advance simulation by dt seconds (dt in seconds). */
void dtnsim_step(double dt);

/* Retrieve pointer to the internal NodePositionsBuffer (owned by WASM). */
const NodePositionsBuffer* dtnsim_get_node_positions(void);

/* Retrieve pointer to an interleaved NodeBuffer (metadata only; owned by WASM). */
const NodeBuffer* dtnsim_get_node_buffer(void);

/* Enable an optional interleaved Node buffer. stride is bytes per node element
 * (typically sizeof(Node)). Returns 0 on success, non-zero on error. */
int dtnsim_enable_node_buffer(uint32_t stride);

/* Disable the optional interleaved Node buffer (frees memory if present). */
int dtnsim_disable_node_buffer(void);

/* Retrieve world bounds (owned by WASM). */
const WorldBounds* dtnsim_get_bounds(void);

/* Explicitly grow/shrink internal capacity (reallocates inside WASM). */
int dtnsim_resize(uint32_t new_max_nodes);

/* Clean up and free internal WASM resources. */
void dtnsim_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* DTNSIM_API_H */

// Minimal WASM bindings for dtnsim (stubs only).
// Implements the frozen ABI declared in wasm/dtnsim_api.h
// No simulation logic here — just placeholders sufficient to compile.

#include "dtnsim_api.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <float.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Zero-copy / memory ownership / versioning notes
 * ------------------------------------------------
 * - The module allocates contiguous buffers in the WASM heap (malloc/realloc).
 * - Metadata structs (NodePositionsBuffer / NodeBuffer) expose byte offsets
 *   into the WebAssembly linear memory (wasm32). JS should treat these
 *   offsets as byte indices into Module.HEAPU8.buffer.
 * - Zero-copy recommendation (JS): create typed-array views directly over
 *   the wasm ArrayBuffer, e.g.:
 *     const floats = new Float32Array(Module.HEAPU8.buffer, positions_ptr, count * 3);
 * - Always validate bounds before creating a view:
 *     positions_ptr + count * stride <= Module.HEAPU8.byteLength
 * - Memory ownership: WASM owns all buffers. JS must NOT free them and
 *   should avoid writing into them unless explicitly permitted.
 * - Versioning: `NodePositionsBuffer.version` and `NodeBuffer.version`
 *   are incremented when contents change (e.g., after dtnsim_step or
 *   dtnsim_resize). JS clients should use `version` to detect updates and
 *   avoid redundant GPU uploads.
 * - Node layout: the interleaved Node payload follows the `Node` struct in
 *   `dtnsim_api.h`. Writes here use memcpy at fixed offsets (id @ 0, x/y/z
 *   at 4/8/12) in little-endian layout; consumers must read using the same
 *   definition and assume little-endian encoding on wasm32.
 * - When exposing pointers to JS, always convert host pointers to 32-bit
 *   byte offsets via ptr_to_offset().
 */

/* Internal module-state (static, owned by WASM)
 * - Buffers are owned by the module and their byte offsets are exposed
 *   through the public NodePositionsBuffer / NodeBuffer structs.
 */
static float *g_positions = NULL;    /* size: capacity * 3 floats */
static uint32_t *g_ids = NULL;       /* size: capacity */
static uint32_t g_capacity = 0u;     /* allocated capacity (number of nodes) */

/* Minimal internal Agent representation used by this bindings sketch.
 * This is intentionally lightweight and only exists inside the WASM
 * module; it does NOT alter any external Scheduler/Agent classes.
 */
typedef struct Agent { float x, y, z; float vx, vy, vz; uint32_t id; } Agent;
static Agent *g_agents = NULL;       /* array length == g_agent_count */
static uint32_t g_agent_count = 0u;  /* number of active agents (<= g_capacity) */

/* Optional interleaved node buffer (metadata-only exposed via NodeBuffer).
 * g_nodes is a raw byte buffer sized (capacity * g_nodes_stride). */
static uint8_t *g_nodes = NULL;
static uint32_t g_nodes_stride = 0u;

/* Public metadata structs (stable addresses returned to callers) */
static NodePositionsBuffer g_positions_buf = {0};
static NodeBuffer g_node_buf = {0};
static WorldBounds g_bounds = {0};

/* Helper: convert host pointer to 32-bit wasm linear memory offset
 * On wasm32 this is a direct cast. If compiling elsewhere, ensure
 * pointers fit in 32 bits for the target (this is intended for emcc).
 */
static inline uint32_t ptr_to_offset(const void *p) {
    return p ? (uint32_t)(uintptr_t)p : 0u;
}

int dtnsim_init(uint32_t max_nodes) {
    /* Accept 0 as "use a reasonable default" to simplify host code. */
    if (max_nodes == 0) max_nodes = 100; /* default agent count */

    /* Allocate positions (3 floats per node) and ids (uint32 per node).
     * Use malloc to allocate memory inside the WASM heap. The returned
     * pointers are exposed as byte offsets for zero-copy access from JS.
     */
    size_t pos_elems = (size_t)max_nodes * 3u;
    float *pos = (float*)malloc(pos_elems * sizeof(float));
    if (!pos) return -2;
    uint32_t *ids = (uint32_t*)malloc((size_t)max_nodes * sizeof(uint32_t));
    if (!ids) { free(pos); return -3; }

    /* Allocate and initialize internal Agent array used by this sketch. */
    Agent *agents = (Agent*)malloc((size_t)max_nodes * sizeof(Agent));
    if (!agents) { free(pos); free(ids); return -4; }

    /* zero-initialize to sensible defaults */
    memset(pos, 0, pos_elems * sizeof(float));
    memset(ids, 0, (size_t)max_nodes * sizeof(uint32_t));
    memset(agents, 0, (size_t)max_nodes * sizeof(Agent));

    /* Free any existing buffers (shouldn't normally happen) */
    if (g_positions) free(g_positions);
    if (g_ids) free(g_ids);
    if (g_agents) free(g_agents);

    g_positions = pos;
    g_ids = ids;
    g_agents = agents;
    g_capacity = max_nodes;
    g_agent_count = max_nodes;

    /* Initialize agents with simple deterministic layout and small velocity
     * so that dtnsim_step() will update their positions and JS can observe
     * changes via the version counter.
     */
    const float spacing = 10.0f;
    const uint32_t cols = (uint32_t)ceilf(sqrtf((float)g_agent_count));
    for (uint32_t i = 0; i < g_agent_count; ++i) {
        uint32_t row = i / cols;
        uint32_t col = i % cols;
        g_agents[i].x = (float)col * spacing;
        g_agents[i].y = (float)row * spacing;
        g_agents[i].z = 0.0f;
        // Random walk: random direction and speed
        float theta = ((float)rand() / (float)RAND_MAX) * 2.0f * (float)M_PI;
        float v = 1.0f; // constant speed, can be randomized if needed
        g_agents[i].vx = v * cosf(theta);
        g_agents[i].vy = v * sinf(theta);
        g_agents[i].vz = 0.0f;
        g_agents[i].id = i + 1;

        /* Also populate the contiguous positions/ids buffers for initial state */
        pos[i * 3 + 0] = g_agents[i].x;
        pos[i * 3 + 1] = g_agents[i].y;
        pos[i * 3 + 2] = g_agents[i].z;
        ids[i] = g_agents[i].id;
    }

    g_positions_buf.positions_ptr = ptr_to_offset(g_positions);
    g_positions_buf.ids_ptr = ptr_to_offset(g_ids);
    g_positions_buf.count = g_agent_count;
    g_positions_buf.positions_stride = DTNSIM_POSITIONS_STRIDE_BYTES;
    g_positions_buf.version = 1u; /* initial version */
    g_positions_buf.reserved = 0u;

    /* NodeBuffer not present by default (secondary) */
    g_node_buf.nodes_ptr = 0u;
    g_node_buf.count = 0u;
    g_node_buf.stride = 0u;
    g_node_buf.version = 0u;

    /* World bounds default (simple bounding box of initial layout) */
    g_bounds.minx = 0.0f;
    g_bounds.miny = 0.0f;
    g_bounds.minz = 0.0f;
    g_bounds.maxx = (cols + 1) * spacing;
    g_bounds.maxy = (cols + 1) * spacing;
    g_bounds.maxz = 0.0f;
    g_bounds.version = 1u;
    g_bounds.reserved = 0u;

    return 0;
}

/* Enable an optional interleaved node buffer. stride is bytes per node element
 * (typically sizeof(Node)). Returns 0 on success, non-zero on error. */
int dtnsim_enable_node_buffer(uint32_t stride) {
    if (stride == 0u) return -1; /* invalid stride */

    if (g_nodes) {
        /* already enabled; check stride compatibility */
        if (g_nodes_stride == stride) return 0;
        /* incompatible stride: require disabling first */
        return -2;
    }

    if (g_capacity == 0u) {
        /* nothing to allocate yet; just set stride and mark present with 0 count */
        g_nodes_stride = stride;
        g_node_buf.nodes_ptr = 0u;
        g_node_buf.count = 0u;
        g_node_buf.stride = g_nodes_stride;
        ++g_node_buf.version;
        return 0;
    }

    size_t bytes = (size_t)g_capacity * (size_t)stride;
    uint8_t *nodes = (uint8_t*)malloc(bytes);
    if (!nodes) return -3; /* allocation failed */
    memset(nodes, 0, bytes);

    g_nodes = nodes;
    g_nodes_stride = stride;

    /* Expose nodes_ptr as a 32-bit byte offset into wasm linear memory
     * (use ptr_to_offset so the pointer is safe for wasm32 consumers).
     * JS should use this offset to create typed views, e.g.:
     *   new Uint8Array(Module.HEAPU8.buffer, nodes_ptr, count * stride);
     */
    g_node_buf.nodes_ptr = ptr_to_offset(g_nodes);
    g_node_buf.count = g_capacity;
    g_node_buf.stride = g_nodes_stride;
    ++g_node_buf.version;

    return 0;
}

/* Disable the optional interleaved node buffer (free if present). */
int dtnsim_disable_node_buffer(void) {
    if (!g_nodes && g_nodes_stride == 0u) return 0; /* already disabled */

    if (g_nodes) { free(g_nodes); g_nodes = NULL; }
    g_nodes_stride = 0u;

    g_node_buf.nodes_ptr = 0u;
    g_node_buf.count = 0u;
    g_node_buf.stride = 0u;
    ++g_node_buf.version;

    return 0;
}

void dtnsim_step(double dt) {
    /* Advance internal minimal agents and copy their positions into
     * the contiguous positions buffer so JS can perform zero-copy reads.
     * Memory ownership: WASM owns the buffers; JS must not free them.
     * Versioning: increment g_positions_buf.version when we update
     * the buffer so JS can detect changes and skip unnecessary GPU uploads.
     */
    if (g_agent_count == 0) return;

    /* Update agent positions with a simple velocity model and copy
     * into the contiguous positions & ids buffers (3 floats per node).
     */
    float minx = FLT_MAX, miny = FLT_MAX, maxx = -FLT_MAX, maxy = -FLT_MAX;
    for (uint32_t i = 0; i < g_agent_count; ++i) {
        Agent *a = &g_agents[i];
        // Random walk: update direction and speed each step
        float theta = ((float)rand() / (float)RAND_MAX) * 2.0f * (float)M_PI;
        float v = 1.0f; // constant speed, can be randomized if needed
        a->vx = v * cosf(theta);
        a->vy = v * sinf(theta);
        // Euler integration
        a->x += a->vx * (float)dt;
        a->y += a->vy * (float)dt;
        a->z += a->vz * (float)dt;
        // wrap or clamp to keep values reasonable for demo
        if (a->x < -1000.0f) a->x = 1000.0f; else if (a->x > 1000.0f) a->x = -1000.0f;
        if (a->y < -1000.0f) a->y = 1000.0f; else if (a->y > 1000.0f) a->y = -1000.0f;

        /* copy to contiguous positions buffer */
        g_positions[i * 3 + 0] = a->x;
        g_positions[i * 3 + 1] = a->y;
        g_positions[i * 3 + 2] = a->z;
        g_ids[i] = a->id;

        /* optional: update interleaved NodeBuffer if enabled
         * - We write individual fields using memcpy into the raw node record
         *   to ensure the on-disk (in-memory) layout matches `Node` in the
         *   public header (`dtnsim_api.h`). Offsets used here are explicit
         *   and assume little-endian wasm32 memory layout: id @ 0, x @ 4,
         *   y @ 8, z @ 12. If additional node fields are required, extend
         *   the Node struct in the header and update writes accordingly.
         * - Using memcpy keeps the write behavior portable and avoids
         *   potential alignment/packing surprises in different compilers.
         */
        if (g_nodes && g_nodes_stride >= sizeof(Node)) {
            uint8_t *dst = g_nodes + (size_t)i * (size_t)g_nodes_stride;
            /* write Node { id, x, y, z, state=0 } in little-endian layout */
            memcpy(dst + 0, &a->id, sizeof(uint32_t));
            memcpy(dst + 4, &a->x, sizeof(float));
            memcpy(dst + 8, &a->y, sizeof(float));
            memcpy(dst + 12, &a->z, sizeof(float));
        }

        /* bounds tracking */
        if (a->x < minx) minx = a->x; if (a->y < miny) miny = a->y;
        if (a->x > maxx) maxx = a->x; if (a->y > maxy) maxy = a->y;
    }

    /* publish metadata updates */
    g_positions_buf.count = g_agent_count;
    ++g_positions_buf.version; /* signal update */

    if (g_nodes) {
        g_node_buf.count = g_agent_count;
        ++g_node_buf.version;
    }

    /* update bounds
     * Note: keep z extents as zero for this simple sketch
     */
    g_bounds.minx = (minx == FLT_MAX) ? 0.0f : minx;
    g_bounds.miny = (miny == FLT_MAX) ? 0.0f : miny;
    g_bounds.minz = 0.0f;
    g_bounds.maxx = (maxx == -FLT_MAX) ? 0.0f : maxx;
    g_bounds.maxy = (maxy == -FLT_MAX) ? 0.0f : maxy;
    g_bounds.maxz = 0.0f;
    ++g_bounds.version;
}

const NodePositionsBuffer* dtnsim_get_node_positions(void) {
    return &g_positions_buf;
}

const NodeBuffer* dtnsim_get_node_buffer(void) {
    return &g_node_buf;
}

const WorldBounds* dtnsim_get_bounds(void) {
    return &g_bounds;
}

int dtnsim_resize(uint32_t new_max_nodes) {
    if (new_max_nodes == g_capacity) return 0; /* unchanged */

    if (new_max_nodes == 0u) {
        /* Free and clear */
        if (g_positions) { free(g_positions); g_positions = NULL; }
        if (g_ids) { free(g_ids); g_ids = NULL; }
        /* If nodes buffer present, free it too */
        if (g_nodes) { free(g_nodes); g_nodes = NULL; }
        if (g_agents) { free(g_agents); g_agents = NULL; }
        g_nodes_stride = 0u;
        g_capacity = 0u;
        g_agent_count = 0u;
        g_positions_buf.positions_ptr = 0u;
        g_positions_buf.ids_ptr = 0u;
        g_positions_buf.count = 0u;
        ++g_positions_buf.version;

        g_node_buf.nodes_ptr = 0u;
        g_node_buf.count = 0u;
        g_node_buf.stride = 0u;
        ++g_node_buf.version;

        return 0;
    }

    /* Keep old capacity for zeroing logic in case we expand. */
    uint32_t old_capacity = g_capacity;

    /* Reallocate to new size */
    size_t new_pos_elems = (size_t)new_max_nodes * 3u;
    float *new_pos = (float*)realloc(g_positions, new_pos_elems * sizeof(float));
    if (!new_pos) return -1; /* realloc failed */

    uint32_t *new_ids = (uint32_t*)realloc(g_ids, (size_t)new_max_nodes * sizeof(uint32_t));
    if (!new_ids) {
        /* roll back positions if ids realloc failed */
        /* Note: gcc realloc semantics leave original pointer intact on failure */
        return -2;
    }

    /* Reallocate agents array to match capacity */
    Agent *new_agents = (Agent*)realloc(g_agents, (size_t)new_max_nodes * sizeof(Agent));
    if (!new_agents) {
        /* roll back if agent realloc failed */
        return -3;
    }

    /* If expanded, zero the new regions */
    if (new_max_nodes > old_capacity) {
        size_t old_pos_elems = (size_t)old_capacity * 3u;
        size_t old_ids_elems = (size_t)old_capacity;
        memset(new_pos + old_pos_elems, 0, (new_pos_elems - old_pos_elems) * sizeof(float));
        memset(new_ids + old_ids_elems, 0, (new_max_nodes - old_ids_elems) * sizeof(uint32_t));
        memset(new_agents + old_capacity, 0, (new_max_nodes - old_capacity) * sizeof(Agent));

        /* initialize new agents with deterministic defaults */
        const float spacing = 10.0f;
        const uint32_t cols = (uint32_t)ceilf(sqrtf((float)new_max_nodes));
        for (uint32_t i = old_capacity; i < new_max_nodes; ++i) {
            uint32_t row = i / cols;
            uint32_t col = i % cols;
            new_agents[i].x = (float)col * spacing;
            new_agents[i].y = (float)row * spacing;
            new_agents[i].z = 0.0f;
            new_agents[i].vx = 0.5f + (float)(i % 5) * 0.1f;
            new_agents[i].vy = 0.2f + (float)(i % 7) * 0.05f;
            new_agents[i].vz = 0.0f;
            new_agents[i].id = i + 1;
        }
    }

    g_positions = new_pos;
    g_ids = new_ids;
    g_agents = new_agents;
    g_capacity = new_max_nodes;
    g_agent_count = new_max_nodes; /* keep agents count == capacity in this sketch */

    g_positions_buf.positions_ptr = ptr_to_offset(g_positions);
    g_positions_buf.ids_ptr = ptr_to_offset(g_ids);
    g_positions_buf.count = g_agent_count;
    ++g_positions_buf.version;

    /* If an interleaved node buffer is enabled, resize/realloc it too. */
    if (g_nodes) {
        size_t bytes = (size_t)g_capacity * (size_t)g_nodes_stride;
        uint8_t *new_nodes = (uint8_t*)realloc(g_nodes, bytes);
        if (!new_nodes) {
            /* keep original buffers intact on failure */
            return -4;
        }
        /* If expanded, zero the new region */
        if (new_max_nodes > old_capacity) {
            size_t old_bytes = (size_t)old_capacity * (size_t)g_nodes_stride;
            memset(new_nodes + old_bytes, 0, bytes - old_bytes);
        }
        g_nodes = new_nodes;
        g_node_buf.nodes_ptr = ptr_to_offset(g_nodes);
        g_node_buf.count = g_capacity;
        ++g_node_buf.version;
    }

    return 0;
}

void dtnsim_shutdown(void) {
    if (g_positions) { free(g_positions); g_positions = NULL; }
    if (g_ids) { free(g_ids); g_ids = NULL; }
    if (g_nodes) { free(g_nodes); g_nodes = NULL; }
    if (g_agents) { free(g_agents); g_agents = NULL; }
    g_nodes_stride = 0u;
    g_capacity = 0u;
    g_agent_count = 0u;

    g_positions_buf.positions_ptr = 0u;
    g_positions_buf.ids_ptr = 0u;
    g_positions_buf.count = 0u;
    ++g_positions_buf.version;

    g_node_buf.nodes_ptr = 0u;
    g_node_buf.count = 0u;
    g_node_buf.stride = 0u;
    ++g_node_buf.version;

    g_bounds.version++;
}

#ifdef __cplusplus
} // extern "C"
#endif

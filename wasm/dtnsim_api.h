/* Clean single-guard header begins here */
#ifndef DTNSIM_API_H
#define DTNSIM_API_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t delivered;
    uint32_t tx;
    uint32_t rx;
    uint32_t duplicates;
} RoutingStats;

typedef struct {
    uint32_t src;
    uint32_t dst;
    uint32_t seq;
    uint32_t ttl;
    uint32_t hops;
} Message;

typedef struct {
    uint32_t positions_ptr;
    uint32_t ids_ptr;
    uint32_t count;
    uint32_t positions_stride;
    uint32_t version;
    uint32_t reserved;
} NodePositionsBuffer;

_Static_assert(sizeof(NodePositionsBuffer) % 4 == 0, "NodePositionsBuffer must be 4-byte aligned");

void dtnsim_init(uint32_t agent_count, const char* routing_name);
void dtnsim_step(double dt);
void dtnsim_reset();
const RoutingStats* dtnsim_get_stats();
const NodePositionsBuffer* dtnsim_get_node_positions();
const NodePositionsBuffer* dtnsim_get_agent_positions();
const Message* dtnsim_get_message_list(uint32_t* out_count);
// Per-agent delivery state for visualization: one byte per agent (0 = never received initial message, 1 = has received)
const uint8_t* dtnsim_get_agent_delivered_flags();

#ifdef __cplusplus
}
#endif

#endif /* DTNSIM_API_H */

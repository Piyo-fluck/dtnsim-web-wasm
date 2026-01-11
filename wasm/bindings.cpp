// --- Includes and Structs ---
#include "dtnsim_api.h"
#include <vector>
#include <string>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>

// Internal C++ graph and agent structures (use C ABI types from header)
struct GraphNode {
    float x, y, z;
    std::vector<uint32_t> neighbors; // indices of neighboring graph nodes
};

struct Agent {
    uint32_t id;
    uint32_t current_node; // index into graph nodes
    uint32_t target_node;  // next node to walk toward
    float progress;        // 0.0 - 1.0 along edge current_node -> target_node
    float x, y, z;         // current interpolated position in space
    std::vector<Message> messages; // messages currently held by this agent
    bool has_initial = false;      // has this agent ever received the initial message?
};

// --- DTN Simulation State ---
namespace {
    std::vector<GraphNode> g_nodes; // static graph nodes
    std::vector<Agent> g_agents;    // moving agents walking on the graph
    std::vector<float> g_node_positions;  // [x0, y0, z0, ...] static node positions for rendering
    std::vector<float> g_agent_positions; // [x0, y0, z0, ...] dynamic agent positions for rendering
    std::vector<Message> g_messages; // global message list (one entry per active message)
    std::vector<uint8_t> g_agent_delivered; // 0/1 per agent: ever received initial message
    RoutingStats g_stats;
    uint32_t g_node_count = 0;
    uint32_t g_agent_count = 0;
    uint32_t g_seq_counter = 0;
    // 0: CarryOnly, 1: Epidemic
    int g_routing_mode = 0;

    // Spatial grid parameters
    constexpr float COMM_RANGE = 80.0f; // reduced to ~0.4x of previous
    constexpr float GRID_CELL_SIZE = COMM_RANGE; // cell size == comm range
    constexpr float AGENT_SPEED = 150.0f; // units per second (spatial speed)

    struct GridCellKey {
        int gx, gy, gz;
        bool operator==(const GridCellKey &o) const { return gx==o.gx && gy==o.gy && gz==o.gz; }
    };

    struct GridCellKeyHash {
        std::size_t operator()(const GridCellKey &k) const noexcept {
            // simple integer hash
            std::size_t h = 1469598103934665603ull;
            h ^= (std::size_t)k.gx + 0x9e3779b97f4a7c15ull + (h<<6) + (h>>2);
            h ^= (std::size_t)k.gy + 0x9e3779b97f4a7c15ull + (h<<6) + (h>>2);
            h ^= (std::size_t)k.gz + 0x9e3779b97f4a7c15ull + (h<<6) + (h>>2);
            return h;
        }
    };

    // Encounter pair within one step
    struct Encounter {
        uint32_t a_idx;
        uint32_t b_idx;
    };

    // Utility: compute grid key
    inline GridCellKey cell_for(const Agent &a) {
        return {
            static_cast<int>(a.x / GRID_CELL_SIZE),
            static_cast<int>(a.y / GRID_CELL_SIZE),
            static_cast<int>(a.z / GRID_CELL_SIZE)
        };
    }
}

// --- API Internals ---

extern "C" {

// --- API required stubs for WASM export ---
void dtnsim_reset() {
    g_nodes.clear();
    g_agents.clear();
    g_node_positions.clear();
    g_agent_positions.clear();
    g_messages.clear();
    g_agent_delivered.clear();
    g_node_count = 0;
    g_agent_count = 0;
    g_seq_counter = 0;
    memset(&g_stats, 0, sizeof(g_stats));
    g_routing_mode = 0;
}


// Use the NodePositionsBuffer typedef from dtnsim_api.h
static NodePositionsBuffer g_node_positions_buf = {0, 0, 0, 12, 1, 0};
static NodePositionsBuffer g_agent_positions_buf = {0, 0, 0, 12, 1, 0};

const NodePositionsBuffer* dtnsim_get_node_positions() {
    // Fill metadata for JS
    g_node_positions_buf.positions_ptr = (uint32_t)(reinterpret_cast<uintptr_t>(g_node_positions.data()));
    g_node_positions_buf.ids_ptr = 0; // Not implemented
    g_node_positions_buf.count = (uint32_t)g_node_count;
    g_node_positions_buf.positions_stride = 12; // 3 floats (x,y,z) * 4 bytes
    static uint32_t version = 1;
    g_node_positions_buf.version = version++;
    g_node_positions_buf.reserved = 0;
    return &g_node_positions_buf;
}

const NodePositionsBuffer* dtnsim_get_agent_positions() {
    g_agent_positions_buf.positions_ptr = (uint32_t)(reinterpret_cast<uintptr_t>(g_agent_positions.data()));
    g_agent_positions_buf.ids_ptr = 0;
    g_agent_positions_buf.count = (uint32_t)g_agent_count;
    g_agent_positions_buf.positions_stride = 12;
    static uint32_t version = 1;
    g_agent_positions_buf.version = version++;
    g_agent_positions_buf.reserved = 0;
    return &g_agent_positions_buf;
}

const RoutingStats* dtnsim_get_stats() {
    return &g_stats;
}

const Message* dtnsim_get_message_list(uint32_t* out_count) {
    if (out_count) *out_count = (uint32_t)g_messages.size();
    return g_messages.data();
}

void dtnsim_init(uint32_t agent_count, const char* routing_name) {
    dtnsim_reset();
    // For now, use the same count for graph nodes and agents, but keep
    // them conceptually separate.
    g_node_count = agent_count;
    g_agent_count = agent_count;

    g_nodes.clear();
    g_nodes.reserve(g_node_count);
    g_node_positions.clear();
    g_node_positions.reserve(g_node_count * 3);

    // Place graph nodes randomly in a 3D box (scaled up to ~1500x1500x1500 to lengthen edges)
    for (uint32_t i = 0; i < g_node_count; ++i) {
        GraphNode n;
        n.x = static_cast<float>(rand()) / static_cast<float>(RAND_MAX) * 1500.0f;
        n.y = static_cast<float>(rand()) / static_cast<float>(RAND_MAX) * 1500.0f;
        n.z = static_cast<float>(rand()) / static_cast<float>(RAND_MAX) * 1500.0f;
        g_nodes.push_back(n);
        g_node_positions.push_back(n.x);
        g_node_positions.push_back(n.y);
        g_node_positions.push_back(n.z);
    }

    // Build explicit adjacency (k-nearest neighbors) on the static graph
    if (g_node_count > 1) {
        const uint32_t K = 3; // neighbors per node
        for (uint32_t i = 0; i < g_node_count; ++i) {
            struct DistIdx { float d2; uint32_t j; };
            std::vector<DistIdx> dists;
            dists.reserve(g_node_count - 1);
            const GraphNode &ni = g_nodes[i];
            for (uint32_t j = 0; j < g_node_count; ++j) {
                if (j == i) continue;
                const GraphNode &nj = g_nodes[j];
                float dx = ni.x - nj.x;
                float dy = ni.y - nj.y;
                float dz = ni.z - nj.z;
                float d2 = dx*dx + dy*dy + dz*dz;
                dists.push_back({d2, j});
            }
            std::sort(dists.begin(), dists.end(), [](const DistIdx &a, const DistIdx &b){ return a.d2 < b.d2; });
            const uint32_t limit = std::min<uint32_t>(K, (uint32_t)dists.size());
            for (uint32_t k = 0; k < limit; ++k) {
                uint32_t j = dists[k].j;
                // add undirected edge i <-> j (avoid obvious duplicates)
                if (std::find(g_nodes[i].neighbors.begin(), g_nodes[i].neighbors.end(), j) == g_nodes[i].neighbors.end()) {
                    g_nodes[i].neighbors.push_back(j);
                }
                if (std::find(g_nodes[j].neighbors.begin(), g_nodes[j].neighbors.end(), i) == g_nodes[j].neighbors.end()) {
                    g_nodes[j].neighbors.push_back(i);
                }
            }
        }
    }

    // Initialize agents on random graph nodes
    g_agents.clear();
    g_agents.reserve(g_agent_count);
    g_agent_positions.clear();
    g_agent_positions.reserve(g_agent_count * 3);
    g_agent_delivered.clear();
    g_agent_delivered.resize(g_agent_count, 0);

    for (uint32_t i = 0; i < g_agent_count; ++i) {
        Agent a;
        a.id = i + 1;
        a.current_node = (g_node_count > 0) ? (rand() % g_node_count) : 0;
        const GraphNode &start = g_nodes[a.current_node];
        if (!g_nodes[a.current_node].neighbors.empty()) {
            a.target_node = g_nodes[a.current_node].neighbors[rand() % g_nodes[a.current_node].neighbors.size()];
        } else {
            a.target_node = a.current_node;
        }
        a.progress = 0.0f;
        a.x = start.x;
        a.y = start.y;
        a.z = start.z;
        a.has_initial = false;
        g_agents.push_back(a);
        g_agent_positions.push_back(a.x);
        g_agent_positions.push_back(a.y);
        g_agent_positions.push_back(a.z);
    }
    // Select routing strategy by name
    // Only "carryonly" and "epidemic" supported for now
    // Store as int for fast check in step (0: CarryOnly, 1: Epidemic)
    if (routing_name && strcmp(routing_name, "epidemic") == 0) {
        g_routing_mode = 1;
    } else {
        g_routing_mode = 0;
    }
    // Inject a single message (TTL effectively infinite; ttl field is unused)
    if (agent_count >= 2) {
        uint32_t src = rand() % agent_count;
        uint32_t dst = (src + 1 + rand() % (agent_count - 1)) % agent_count;
        Message m;
        m.src = g_agents[src].id;
        m.dst = g_agents[dst].id;
        m.seq = ++g_seq_counter;
        m.ttl = 0; // 0 means "no expiry" in current logic
        m.hops = 0;
        g_agents[src].messages.push_back(m);
        g_messages.push_back(m);
        // Initial carrier has already "received" the initial message
        g_agents[src].has_initial = true;
        if (src < g_agent_delivered.size()) {
            g_agent_delivered[src] = 1;
        }
    }
    // Reset stats
    memset(&g_stats, 0, sizeof(g_stats));
    // delivered now means: number of distinct agents that have ever received the initial message
    if (agent_count >= 2) {
        g_stats.delivered = 1; // initial carrier
    }
}

// Expose per-agent delivered flags (0 = never received initial message, 1 = has received)
const uint8_t* dtnsim_get_agent_delivered_flags() {
    if (g_agent_delivered.empty()) return nullptr;
    return g_agent_delivered.data();
}

void dtnsim_step(double dt) {
    const uint32_t agent_count = g_agent_count;
    if (agent_count == 0) return;

    const float fdt = static_cast<float>(dt);

    // 1. Agent mobility update (random walk on graph edges)
    for (uint32_t i = 0; i < agent_count; ++i) {
        Agent &a = g_agents[i];
        if (g_node_count == 0) continue;
        const GraphNode &src = g_nodes[a.current_node];
        const GraphNode &dst = g_nodes[a.target_node];
        float dx = dst.x - src.x;
        float dy = dst.y - src.y;
        float dz = dst.z - src.z;
        float len = std::sqrt(dx*dx + dy*dy + dz*dz);

        if (len < 1e-3f) {
            a.progress = 1.0f;
        } else {
            float delta = (AGENT_SPEED * fdt) / len;
            a.progress += delta;
            if (a.progress > 1.0f) a.progress = 1.0f;
        }

        float t = a.progress;
        a.x = src.x + dx * t;
        a.y = src.y + dy * t;
        a.z = src.z + dz * t;

        // Write back to agent position buffer
        const size_t base = static_cast<size_t>(i) * 3;
        if (base + 2 < g_agent_positions.size()) {
            g_agent_positions[base + 0] = a.x;
            g_agent_positions[base + 1] = a.y;
            g_agent_positions[base + 2] = a.z;
        }

        if (a.progress >= 1.0f) {
            a.current_node = a.target_node;
            const GraphNode &cur = g_nodes[a.current_node];
            if (!cur.neighbors.empty()) {
                a.target_node = cur.neighbors[rand() % cur.neighbors.size()];
                a.progress = 0.0f;
            }
        }
    }

    // 2. Neighbor / encounter detection using a 3D uniform grid (on agent positions)
    std::unordered_map<GridCellKey, std::vector<uint32_t>, GridCellKeyHash> grid;
    grid.reserve(agent_count * 2);
    for (uint32_t i = 0; i < agent_count; ++i) {
        const Agent &a = g_agents[i];
        GridCellKey key = cell_for(a);
        grid[key].push_back(i);
    }

    std::vector<Encounter> encounters;
    encounters.reserve(agent_count * 4);

    const float comm_range2 = COMM_RANGE * COMM_RANGE;

    for (uint32_t i = 0; i < agent_count; ++i) {
        const Agent &ai = g_agents[i];
        GridCellKey ci = cell_for(ai);
        for (int dx = -1; dx <= 1; ++dx) {
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dz = -1; dz <= 1; ++dz) {
                    GridCellKey ck{ci.gx + dx, ci.gy + dy, ci.gz + dz};
                    auto it = grid.find(ck);
                    if (it == grid.end()) continue;
                    const std::vector<uint32_t> &indices = it->second;
                    for (uint32_t idx : indices) {
                        if (idx <= i) continue; // ensure each pair at most once per step
                        const Agent &aj = g_agents[idx];
                        const float dxp = ai.x - aj.x;
                        const float dyp = ai.y - aj.y;
                        const float dzp = ai.z - aj.z;
                        const float dist2 = dxp*dxp + dyp*dyp + dzp*dzp;
                        if (dist2 <= comm_range2) {
                            encounters.push_back({ i, idx });
                        }
                    }
                }
            }
        }
    }

    // 3. Routing and message forwarding
    // We must obey:
    //  - each message may be transferred at most once per encounter
    //  - a newly received message cannot be forwarded again within the same step

    // Track which (agent, message) pairs received a message in this step
    std::unordered_set<uint64_t> received_this_step;
    received_this_step.reserve(1024);

    auto make_key = [](uint32_t agent_idx, uint32_t msg_idx) -> uint64_t {
        return (static_cast<uint64_t>(agent_idx) << 32) | static_cast<uint64_t>(msg_idx);
    };

    // Helper: find message index in global g_messages by (src,dst,seq)
    auto find_global_msg_index = [](const Message &m) -> int {
        for (size_t i = 0; i < g_messages.size(); ++i) {
            const Message &gm = g_messages[i];
            if (gm.src == m.src && gm.dst == m.dst && gm.seq == m.seq) return static_cast<int>(i);
        }
        return -1;
    };

    // Helper: mark that an agent has received the initial message (seq == 1) at least once
    auto mark_initial_received = [&](uint32_t agent_idx) {
        if (agent_idx >= g_agents.size()) return;
        Agent &ag = g_agents[agent_idx];
        if (!ag.has_initial) {
            ag.has_initial = true;
            if (agent_idx < g_agent_delivered.size()) {
                g_agent_delivered[agent_idx] = 1;
            }
            g_stats.delivered++; // count distinct agents that have ever held the initial message
        }
    };

    for (const Encounter &enc : encounters) {
        Agent &a = g_agents[enc.a_idx];
        Agent &b = g_agents[enc.b_idx];

        if (g_routing_mode == 0) {
            // CarryOnly
            // An agent forwards a message only if it encounters the destination directly.
            // Forwarding to intermediates is not allowed.
            // Each successful delivery: tx++, rx++, delivered++, message removed from system.

            // From a -> b
            for (const Message &m : a.messages) {
                if (b.id != m.dst) continue;
                // destination reached
                // Check duplicates: if b already holds m, count duplicate and skip
                bool b_has = false;
                for (const Message &bm : b.messages) {
                    if (bm.src==m.src && bm.dst==m.dst && bm.seq==m.seq) { b_has = true; break; }
                }
                if (b_has) {
                    continue;
                }
                g_stats.tx++;
                g_stats.rx++;
                // Conceptual delivery: destination receives the message once
                if (m.seq == 1) {
                    mark_initial_received(enc.b_idx);
                }

                // Remove from all agents and global list after loop (delivery/removal handled below)
            }

            // From b -> a (symmetric case)
            for (const Message &m : b.messages) {
                if (a.id != m.dst) continue;
                bool a_has = false;
                for (const Message &am : a.messages) {
                    if (am.src==m.src && am.dst==m.dst && am.seq==m.seq) { a_has = true; break; }
                }
                if (a_has) {
                    continue;
                }
                g_stats.tx++;
                g_stats.rx++;
                if (m.seq == 1) {
                    mark_initial_received(enc.a_idx);
                }
            }
        } else {
            // Epidemic routing
            // During an encounter:
            //  - each side forwards all messages it holds and the neighbor does not hold
            //  - each message at most once per encounter
            //  - messages received in this step cannot be forwarded again in this step

            // Build fast membership sets for b and a for this encounter
            auto has_msg = [](const std::vector<Message> &vec, const Message &m) {
                for (const Message &x : vec) {
                    if (x.src==m.src && x.dst==m.dst && x.seq==m.seq) return true;
                }
                return false;
            };

            // a -> b
            for (size_t mi = 0; mi < a.messages.size(); ++mi) {
                const Message &m = a.messages[mi];
                int gidx = find_global_msg_index(m);
                if (gidx < 0) continue;
                uint64_t key = make_key(enc.a_idx, static_cast<uint32_t>(gidx));
                if (received_this_step.find(key) != received_this_step.end()) continue; // newly received earlier this step

                if (has_msg(b.messages, m)) {
                    continue;
                }

                // Transfer
                b.messages.push_back(m);
                g_stats.tx++;
                g_stats.rx++;

                // Track spread of the initial message (seq == 1)
                if (m.seq == 1) {
                    mark_initial_received(enc.b_idx);
                }

                // Once per encounter: don't transfer same message again in this encounter from a->b
                // (loop naturally ensures that)

                // mark as received this step so b cannot forward it again this step
                received_this_step.insert(make_key(enc.b_idx, static_cast<uint32_t>(gidx)));
            }

            // b -> a
            for (size_t mi = 0; mi < b.messages.size(); ++mi) {
                const Message &m = b.messages[mi];
                int gidx = find_global_msg_index(m);
                if (gidx < 0) continue;
                uint64_t key = make_key(enc.b_idx, static_cast<uint32_t>(gidx));
                if (received_this_step.find(key) != received_this_step.end()) continue;

                if (has_msg(a.messages, m)) {
                    continue;
                }

                a.messages.push_back(m);
                g_stats.tx++;
                g_stats.rx++;
                if (m.seq == 1) {
                    mark_initial_received(enc.a_idx);
                }
                received_this_step.insert(make_key(enc.a_idx, static_cast<uint32_t>(gidx)));
            }
        }
    }

    // 4. TTL handling (disabled for infinite TTL) & 5. Delivery check and message removal
    // We maintain g_messages as the set of all active (non-delivered) messages.
    // Agents hold references (by value). With infinite TTL we:
    //  - do NOT decrement ttl or drop by expiry
    //  - only remove messages that reached destination from all agents and global list

    // First, identify which global messages are delivered or expired
    std::vector<bool> remove_global(g_messages.size(), false);

    for (size_t gi = 0; gi < g_messages.size(); ++gi) {
        Message &gm = g_messages[gi];

        // Destination handling: if any agent holding gm has id == dst, treat as delivered
        bool delivered = false;
        for (const Agent &a : g_agents) {
            if (a.id != gm.dst) continue;
            for (const Message &m : a.messages) {
                if (m.src==gm.src && m.dst==gm.dst && m.seq==gm.seq) {
                    delivered = true;
                    break;
                }
            }
            if (delivered) break;
        }

        if (delivered) {
            remove_global[gi] = true;
            // stats.delivered already incremented when destination first received the message
        }
    }

    // Remove from global list
    std::vector<Message> new_global;
    new_global.reserve(g_messages.size());
    for (size_t gi = 0; gi < g_messages.size(); ++gi) {
        if (!remove_global[gi]) new_global.push_back(g_messages[gi]);
    }
    g_messages.swap(new_global);

    // Remove from agents' buffers
    for (Agent &a : g_agents) {
        std::vector<Message> kept;
        kept.reserve(a.messages.size());
        for (const Message &m : a.messages) {
            bool alive = false;
            for (const Message &gm : g_messages) {
                if (gm.src==m.src && gm.dst==m.dst && gm.seq==m.seq) {
                    alive = true;
                    break;
                }
            }
            if (alive) kept.push_back(m);
        }
        a.messages.swap(kept);
    }

    // 6. Statistics update
    // All stat counters (tx, rx, duplicates, delivered) are maintained inline above.

#ifndef NDEBUG
    // Lightweight consistency check (debug-only):
    //  - Every global message must be held by at least one agent
    //  - Every per-agent message must exist in g_messages
    for (const Message &gm : g_messages) {
        bool found = false;
        for (const Agent &a : g_agents) {
            for (const Message &m : a.messages) {
                if (m.src==gm.src && m.dst==gm.dst && m.seq==gm.seq) {
                    found = true;
                    break;
                }
            }
            if (found) break;
        }
        if (!found) {
            // In debug builds, abort early if invariants are broken.
            abort();
        }
    }

    for (const Agent &a : g_agents) {
        for (const Message &m : a.messages) {
            bool found = false;
            for (const Message &gm : g_messages) {
                if (gm.src==m.src && gm.dst==m.dst && gm.seq==m.seq) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                abort();
            }
        }
    }
#endif
}

#ifdef __cplusplus
} // extern "C"
#endif

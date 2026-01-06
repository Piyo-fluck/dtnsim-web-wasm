## dtnsim WASM ABI (frozen)

### Design principles
- Zero-copy JS <-> WASM
- Stable pointers, versioned buffers
- 4-byte alignment only
- No implicit allocation on step()

### Buffers
#### NodePositionsBuffer (canonical)
- stride: exactly 12 bytes (XYZ float32)
- ownership: WASM
- ptr==0 means not present

#### NodeBuffer (secondary metadata)
- optional
- ptr==0 means not present

Helper functions (bindings):
- `dtnsim_enable_node_buffer(stride)` — allocate an interleaved Node buffer with the specified stride (bytes per node). Returns 0 on success.
- `dtnsim_disable_node_buffer()` — free the optional interleaved node buffer (if present).

# GOALS.md

## Final Goal
A browser-based 3D DTN simulation using dtnsim core logic compiled to WebAssembly.

## Milestones

### Phase 1: Build
- Compile dtnsim into a WASM module using emcc.
- Confirm execution without graphics.

### Phase 2: Binding
- Expose simulation state via C API.
- Call from JavaScript.

### Phase 3: Visualization
- Render nodes as spheres in WebGL.
- Render links as lines.
- Animate movement over time.

### Phase 4: Interaction
- Camera control (orbit).
- Play / pause simulation.

Each phase must be independently runnable.

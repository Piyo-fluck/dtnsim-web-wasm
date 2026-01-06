## High-level architecture

C++ (dtnsim core)
  ↓ stable C ABI
WASM (emscripten)
  ↓ TypedArray mapping
JS
  ↓ bufferSubData
WebGL

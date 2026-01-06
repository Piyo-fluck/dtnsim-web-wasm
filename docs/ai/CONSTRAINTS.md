# CONSTRAINTS.md

## Emscripten
- Use -s WASM=1
- Use -s MODULARIZE=1
- Use -s EXPORTED_FUNCTIONS
- No pthreads
- No dynamic file IO

## Graphics
- WebGL only (no WebGPU).
- No external JS libraries.

## Performance
- Avoid per-frame malloc/free in WASM.
- State polling preferred over callbacks.

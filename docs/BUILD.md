## Build (Windows)

- Use emsdk (NOT msys2 emscripten)
- Use PowerShell for wasm build
- Use emcmake

```powershell
.\emsdk_env.ps1
emcmake cmake -S wasm -B wasm/build
cmake --build wasm/build

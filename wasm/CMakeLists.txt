cmake_minimum_required(VERSION 3.13)
project(dtnsim_wasm C CXX)

# Use C standard compatible with our header
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Create an executable module that emcc will turn into JS+WASM
add_executable(dtnsim bindings.cpp)

# Ensure output goes into the build directory
set_target_properties(dtnsim PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

# Linker flags for emscripten. Keep minimal and explicit.
# - MODULARIZE=1 produces a JS factory function; we also export the ABI functions
# - ALLOW_MEMORY_GROWTH is handy during development
set(COMMON_EMFLAGS "-s WASM=1 -s MODULARIZE=1 -s EXPORT_NAME='createDTNSIMModule' -s ALLOW_MEMORY_GROWTH=1 -s EXPORT_ES6=0 -O2")
set(EXPORTED_FUNCS "['_dtnsim_init','_dtnsim_step','_dtnsim_get_node_positions','_dtnsim_get_node_buffer','_dtnsim_enable_node_buffer','_dtnsim_disable_node_buffer','_dtnsim_get_bounds','_dtnsim_resize','_dtnsim_shutdown']")
set(EXPORTED_RUNTIME_METHODS "['HEAPU8','HEAPF32']")
set_target_properties(dtnsim PROPERTIES LINK_FLAGS "${COMMON_EMFLAGS} -s EXPORTED_FUNCTIONS=${EXPORTED_FUNCS} -s EXPORTED_RUNTIME_METHODS=${EXPORTED_RUNTIME_METHODS} -o dtnsim.js")

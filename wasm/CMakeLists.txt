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
# Export all DTNSIM API functions used by the web UI
set(EXPORTED_FUNCS "['_dtnsim_init','_dtnsim_step','_dtnsim_get_node_positions','_dtnsim_get_agent_positions','_dtnsim_get_stats','_dtnsim_get_message_list','_dtnsim_reset','_dtnsim_get_agent_delivered_flags']")
# Export runtime helpers needed for UTF-8 string conversion and memory access
set(EXPORTED_RUNTIME_METHODS "['HEAPU8','HEAPF32','lengthBytesUTF8','stringToUTF8','allocateUTF8OnStack','stackSave','stackRestore']")
set_target_properties(dtnsim PROPERTIES LINK_FLAGS "${COMMON_EMFLAGS} -s EXPORTED_FUNCTIONS=${EXPORTED_FUNCS} -s EXPORTED_RUNTIME_METHODS=${EXPORTED_RUNTIME_METHODS} -o dtnsim.js")
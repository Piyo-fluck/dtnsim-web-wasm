const assert = require('assert');
const path = require('path');

const createDTNSIMModule = require(path.join(__dirname, 'dtnsim.js'));

(async () => {
  try {
    const Module = await createDTNSIMModule();
    assert(Module && Module.HEAPU8 && Module.HEAPF32, 'WASM heaps not exposed');

    const AGENT_COUNT = 32;
    const initRc = Module._dtnsim_init(AGENT_COUNT);
    assert.strictEqual(initRc, 0, `dtnsim_init returned ${initRc}`);

    const metaPtr = Module._dtnsim_get_node_positions();
    assert(metaPtr !== 0, 'NodePositionsBuffer pointer is null');

    const metaView = new DataView(Module.HEAPU8.buffer, metaPtr, 24);
    const positionsPtr = metaView.getUint32(0, true);
    const idsPtr = metaView.getUint32(4, true);
    const count = metaView.getUint32(8, true);
    const stride = metaView.getUint32(12, true);
    const version0 = metaView.getUint32(16, true);

    assert.strictEqual(count, AGENT_COUNT, 'Unexpected agent count');
    assert.strictEqual(stride, 12, 'Unexpected positions stride');
    assert(positionsPtr !== 0, 'Positions buffer pointer is null');
    assert(idsPtr !== 0, 'IDs buffer pointer is null');

    const floats = new Float32Array(Module.HEAPF32.buffer, positionsPtr, count * 3);
    const snapshot = new Float32Array(floats); // copy initial positions

    Module._dtnsim_step(0.016);
    const version1 = metaView.getUint32(16, true);
    assert.notStrictEqual(version1, version0, 'Version did not increment after step');

    // Verify positions changed for at least one agent
    let moved = false;
    for (let i = 0; i < snapshot.length; ++i) {
      if (floats[i] !== snapshot[i]) {
        moved = true;
        break;
      }
    }
    assert(moved, 'No agent moved after step');

    Module._dtnsim_shutdown();
    console.log('✅ Random walk harness passed');
  } catch (err) {
    console.error('❌ Random walk harness failed:', err);
    process.exitCode = 1;
  }
})();

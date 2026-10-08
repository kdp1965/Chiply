// Runs the benchmark off the page's thread (PLAN.md 13).
importScripts('chiply_bench.js');
let modulePromise = null;
function engine() {
    if (!modulePromise)
        modulePromise = createChiplyBench();
    return modulePromise;
}
onmessage = async (e) => {
    const { json, seconds, cycles, switches } = e.data;
    try {
        const t0 = performance.now();
        const m = await engine();
        const load = performance.now() - t0;
        const bench = m.cwrap('chiply_bench', 'number', ['string', 'number', 'number', 'string']);
        const t1 = performance.now();
        const ptr = bench(json, seconds, cycles, switches || '');
        const total = performance.now() - t1;
        const text = m.UTF8ToString(ptr);
        m._chiply_free(ptr);
        postMessage({ ok: true, result: JSON.parse(text), engineLoadMs: load, totalMs: total });
    } catch (err) {
        postMessage({ ok: false, error: String(err && err.message || err) });
    }
};

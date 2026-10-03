# Reference fixtures

Files used by the plan (`../PLAN.md`) and, later, by the test suite. They are
not part of Chiply's source and keep their own origins and licenses.

| File | Origin | Notes |
|---|---|---|
| `wokwi_414123795172381697.diagram.json` | Ken Pettit's Wokwi project <https://wokwi.com/projects/414123795172381697> | 1024 parts, 2042 wires; the round-trip and performance target |
| `tt_um_wokwi_414123795172381697.v` | Wokwi's Verilog export of the same project (`https://wokwi.com/api/projects/414123795172381697/verilog`) | golden netlist for the equivalence test |
| `tt_template_354858054593504257.diagram.json` | Tiny Tapeout's official Wokwi template <https://wokwi.com/projects/354858054593504257> | the "New Tiny Tapeout project" starting point |
| `cells.v` | Tiny Tapeout (`ttsky-wokwi-template`, Apache-2.0) | the Verilog cell library Wokwi designs are mapped to |
| `wokwi_414123795172381697.rendered_wires.json` | Wire polylines as wokwi.com renders the reference design (captured with headless Chrome) | ground truth for the wire-routing test |

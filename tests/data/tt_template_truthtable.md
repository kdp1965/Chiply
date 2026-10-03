# Tiny Tapeout template: uo_out = {ui_in[7:4], ~ui_in[3:0]}

| ui_in     | uo_out    | comment          |
|-----------|-----------|------------------|
| 0000 0000 | 0000 1111 | all low          |
| 1010 0101 | 1010 1010 |                  |
| ---- ---t | ---- ---1 | toggle bit 0 off |
| t--- ---- | 0--- ---- | toggle bit 7 off |
| 8'11111111| 8'11110000| prefix form      |
| ---- 0000 | xxxx 1111 | don't care       |
| ---- ---c | ---- ---1 | a clock pulse on bit 0 returns to 0 |
| ---- ---- |           | no check         |

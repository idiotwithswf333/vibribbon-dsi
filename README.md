# Vib-Ribbon DSi port

SD card layout (exactly this path): `sd:/VIBRI/vibribbon.cue`, `sd:/VIBRI/vibribbon.bin`, plus your PS1 BIOS `.bin` (512 KB) in the same folder (needed for buttons).
Run `vibribbon.nds` in DSi mode. Top screen = game, bottom = Vibri logo. No cue/bin at that path: top screen says so.
Buttons: d-pad, A=Cross, B=Circle, X=Square, Y=Triangle, L/R=L1/R1, Start, Select.
No sound yet. Optional `sd:/VIBRI/dsi.ini` with `cpi=N` (default 2) trades game CPU speed for emulation speed.
Never compiled against real libnds; the first CI run may need a small fix.

# Independent Z80 CPU emulator

Unmodified `chips/z80.h` from [floooh/chips](https://github.com/floooh/chips),
commit `9e88298ce56319953ac7a43213a1120359f7a3a6`.
Copyright Andre Weissflog; zlib/libpng license, included in the header and LICENSE.

Used only by the host execution harness. It is neither linked into the cartridge
nor a dependency of the Vexel resolver. The surrounding MSX/NEO model is local
code in `tools/neo8_runner.cpp`; it is not a full MSX emulator.

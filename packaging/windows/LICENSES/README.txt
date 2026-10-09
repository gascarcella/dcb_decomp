The licences of what this package contains.

  dcb_decomp.txt          the decompilation of Digimon Digital Card Battle by juandav (the game's C, compiled into
                          dcb.exe) (https://github.com/ReGame-Labs/dcb_decomp)
  psxstack.txt            psxstack: the PC port's runtime, its Psy-Q shim and the launcher
                          (https://github.com/gascarcella/psxstack); the port's own files are the fork's
                          (https://github.com/gascarcella/dcb_decomp)
  SDL3.txt                SDL 3 (zlib licence), linked statically into both programs (dcb.exe and
                          dcb-launcher.exe)
  imgui.txt               Dear ImGui (MIT), compiled into the launcher
  ProggyForever.txt       the launcher's font (MIT), embedded in Dear ImGui
  ProggyClean.txt         Dear ImGui's other embedded font (MIT)
  llvm.txt                the LLVM project (Apache 2.0 with the LLVM exception): the parts of its runtime linked
                          statically into both programs by llvm-mingw (compiler-rt's builtins, libunwind; libc++ in
                          the launcher) (https://github.com/mstorsjo/llvm-mingw)
  mingw-w64-runtime.txt   the mingw-w64 runtime (its own notices), linked statically into both programs
                          (https://www.mingw-w64.org/)
  winpthreads.txt         mingw-w64's winpthreads (MIT and BSD), linked statically into both programs

The Windows C runtime itself (the UCRT) is part of Windows and is not included.
No game data is included: the game is read from your own disc image of Digimon Digital Card Battle (PS1, USA).

The licences of what this AppImage contains.

  dcb_decomp.txt        the decompilation of Digimon Digital Card Battle by juandav (the game's C, compiled into
                        usr/bin/dcb) (https://github.com/ReGame-Labs/dcb_decomp)
  psxstack.txt          psxstack: the PC port's runtime, its Psy-Q shim and the launcher
                        (https://github.com/gascarcella/psxstack); the port's own files are the fork's
                        (https://github.com/gascarcella/dcb_decomp)
  SDL3.txt              SDL 3 (zlib licence), linked statically into both programs (usr/bin/dcb and
                        usr/bin/dcb-launcher)
  imgui.txt             Dear ImGui (MIT), compiled into the launcher
  ProggyForever.txt     the launcher's font (MIT), embedded in Dear ImGui
  ProggyClean.txt       Dear ImGui's other embedded font (MIT)
  appimage-runtime.txt  the AppImage runtime (MIT) at the start of the .AppImage file, with the libraries linked
                        into it (musl, libfuse 3 under the LGPL 2.1, squashfuse, zstd, zlib)
                        (https://github.com/AppImage/type2-runtime)

No game data is included: the game is read from your own disc image of Digimon Digital Card Battle (PS1, USA).

Digimon Digital Card Battle, PC port, @VERSION@ for Windows (x86_64)
=====================================================================

Digimon Digital Card Battle (PlayStation, USA) rebuilt as a native program from juandav's decompilation of its code
(https://github.com/ReGame-Labs/dcb_decomp), on psxstack (https://github.com/gascarcella/psxstack).
The port: https://github.com/gascarcella/dcb_decomp

What you need
-------------
* Windows 10 or newer, 64-bit.
* Your own disc image of Digimon Digital Card Battle (PlayStation, USA, SLUS-01328), as a .cue with its .bin (or the
  .bin alone; one MODE2/2352 track). No game data is included: the launcher checks the image's SHA-1 and only the USA
  disc is accepted.

Starting
--------
1. Unzip this folder anywhere (its files must stay together: the launcher finds the game beside itself).
2. Run dcb-launcher.exe. The programs are not signed, so Windows may show a SmartScreen warning the first time:
   choose "More info", then "Run anyway".
3. On the Disc screen, pick your disc image (the file dialog, a drag-and-drop or a typed path).
4. Press Play. The launcher hides while the game runs and comes back when it ends.

Settings, Controls and Mods are the launcher's other screens. Keyboard defaults: arrows the D-pad, X cross (confirm),
C circle, Z square, S triangle, Enter START, Backspace SELECT, Q/E L1/R1, 1/3 L2/R2; P pauses, F11 fullscreen. A
gamepad works as on a PlayStation (south button cross, east circle; the left stick is the D-pad too). Everything can be
rebound on the Controls screen. Fast-forward (hold Tab) is a mod, off until you switch it on in the Mods screen: the
way through the two-minute opening movie, which cannot be skipped.

Where things are kept
---------------------
%APPDATA%\dcb\  (usually C:\Users\<you>\AppData\Roaming\dcb\):
  settings.json           the launcher's settings
  card1.mcd, card2.mcd    the memory cards
  logs\last-run.log       the last game run's output (and last-run.1.log, the one before)
  crashes\                crash reports (crash-<date>.txt) and minidumps (crash-<date>.dmp)
A portable.txt file beside dcb-launcher.exe keeps all of this in the launcher's own folder instead.

State of the port
-----------------
A work in progress: the game boots, plays its opening and its menus, registers a new player and starts the first duel;
the rest of the game is not tested yet, the sound has not been compared with the PlayStation's, and memory cards have
not been exchanged with an emulator. This Windows build was tested on Linux through Wine, not yet on real Windows.

Reporting a problem
-------------------
If the game crashes, the launcher's Play screen shows the exit status and the last lines: press "Copy" and paste the
text into a new issue at https://github.com/gascarcella/dcb_decomp/issues, and attach the matching crash-<date>.dmp
from the crashes folder if there is one. Your disc image and your saves are never part of a report.

LICENSES\README.txt lists the licences of what this package contains.

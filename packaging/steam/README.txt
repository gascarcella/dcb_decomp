PLACEHOLDER Steam artwork (game.json launcher.steam_art; psxstack docs/LAUNCHER.md "Steam").

The launcher's Settings > Steam > Add to Steam writes these beside the game's non-Steam shortcut; they are embedded in
the launcher at build time. They are placeholders: the port's own icon (packaging/appimage/dcb.svg, drawn for this
fork, nothing from the game) and the title in plain type, at Steam's sizes, the capsules and the hero labelled
"placeholder art". Proper art replaces them file for file (any PNG is taken; nothing from the disc).

  capsule.png       600x900    the library's portrait capsule
  wide_capsule.png  920x430    the wide capsule
  hero.png          3840x1240  the game page's banner
  logo.png          1280x720   drawn over the hero, transparent
  icon.png          256x256    the shortcut's icon (dcb.svg rendered)

Regenerate: packaging/steam/make_placeholders.py (Inkscape renders the SVG sources it writes here).

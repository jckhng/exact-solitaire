# Exact Solitaire

A practical Kindle-friendly Windows Solitaire-style Klondike app inspired by
GNOME Aisleriot, using GTK2/Cairo and the same Mesquite/KUAL packaging approach
as Kindle GlChess.

This v0.1.0 port intentionally takes the practical path: it targets familiar
Windows Solitaire/Klondike play for Kindle instead of trying to embed the full
GNOME Aisleriot Guile/Scheme game collection.

## Screenshot

![Exact Solitaire screenshot](exact-solitaire.png)

## Features

- Touch-friendly Windows Solitaire-style Klondike board.
- Stock, waste, tableau, foundations, draw, undo, new game, and quit.
- Draw 1 / Draw 3 mode toggle. Draw 3 is the default classic Klondike mode;
  Draw 1 remains available as an easier/casual option.
- Card theme selector: Simplified large-label cards, Modern SVG Cards 2.0, and
  Original Bonded cards from GNOME Aisleriot.
- KUAL extension package with bundled ARM GTK2/Cairo runtime libraries.

## Install

Use the prebuilt extension package:

```text
release/exact-solitaire-extension.zip
```

Unzip it at the Kindle USB-storage root so it creates:

```text
/mnt/us/extensions/exact-solitaire
/mnt/us/documents/shortcut_exactsolitaire.sh
```

Launch from KUAL:

```text
KUAL -> Exact Solitaire -> Exact Solitaire
```

## Build

```bash
./docker_rebuild.sh
```

If ARM containers are not enabled:

```bash
docker run --privileged --rm tonistiigi/binfmt --install arm
```

## License And Provenance

This is not an official GNOME Aisleriot release and not an official
GnomeGames4Kindle release. GNOME Aisleriot attribution and license notes are in
[docs/PROVENANCE.md](docs/PROVENANCE.md).

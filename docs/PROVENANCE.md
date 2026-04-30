# Provenance And Licensing

Kindle Aisleriot is an unofficial Kindle-focused solitaire adaptation inspired
by GNOME Aisleriot.

- Upstream project: <https://gitlab.gnome.org/GNOME/aisleriot>
- Upstream license basis: GPL-family GNOME application licensing. Distribution
  metadata for GNOME Aisleriot commonly includes GPL-3.0-or-later,
  LGPL-3.0-or-later, and documentation licensing.
- Documentation license basis: GNOME documentation is commonly GFDL; the GFDL
  text is included as `licenses/COPYING-DOCS` for completeness.

## What Comes From GNOME Aisleriot

- Solitaire/Aisleriot design lineage and project inspiration.
- Small visual assets copied into `assets/`.
- GPL-family license basis.

## What Is Kindle-Specific

- Focused Klondike-style rules engine in `solitaire_engine.c`.
- GTK2/Cairo Kindle interface in `main.c`.
- KUAL extension scripts and Mesquite window title.
- Docker ARM build and release packaging scripts.

## Scope Note

This is not a full port of the original Aisleriot Guile/Scheme game collection.
It is a practical first Kindle solitaire implementation that keeps lineage and
packaging clear while avoiding the large Guile runtime dependency.

## License Notes

GPL, LGPL, and documentation license texts are included in `licenses/`. Runtime
libraries bundled into binary release packages keep their own upstream licenses
and are listed inside the generated package under `LICENSES/`.

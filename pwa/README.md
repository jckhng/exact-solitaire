# Exact Solitaire PWA

An installable browser Solitaire game with muted card colors on a green felt background.

## Features

- Klondike solitaire (Draw 1 or Draw 3).
- FreeCell solitaire.
- Click-to-select and click-to-move card interaction.
- Auto-complete: moves cards to foundations as far as possible.
- Undo moves.
- Save/Load a manual restore point.
- Works offline after first load.
- Installable via Chrome "Add to Home Screen."

## Building

```bash
npm install
npm run typecheck
npm run build
```

## Rules

**Klondike:** Build four foundation piles from Ace to King by suit. Tableau
columns are built in alternating colors, descending rank. Click the stock to
draw cards to the waste. Move the top waste card or tableau sequences.

**FreeCell:** All cards are dealt face-up. Use the four free cells as temporary
storage. Build foundations Ace to King by suit.

## Attribution

Rules engine ported from the Exact Solitaire native Kindle implementation
(derived from GNOME Aisleriot / gnome-games). Part of the Exact Games /
GnomeGames4Kindle project.

## License

GPL-3.0-or-later. See THIRD_PARTY.md for dependency details.

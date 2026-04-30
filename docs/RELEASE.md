# Release Notes

## Current Release

Artifact:

```text
release/kindle-aisleriot-extension.zip
```

Verify:

```bash
cd release
sha256sum -c SHA256SUMS
```

Current checksum:

```text
efa03b1f78ede622e422f029fedc0ff6858283a21c1819a4b391958df3f99214  kindle-aisleriot-extension.zip
```

Known constraints:

- First practical port; targets Windows Solitaire-style Klondike, not the full
  GNOME Aisleriot game collection.
- Draw 3 is the default classic Klondike mode. It displays up to three waste
  cards, but only the top/rightmost waste card is movable.
- Requires a jailbroken Kindle with KUAL.

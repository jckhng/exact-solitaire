# Building

Build the ARM Kindle binary and extension package with:

```bash
./docker_rebuild.sh
```

The script starts the persistent `exact-solitaire-armhf-builder` container,
builds `exact-solitaire`, runs `smoke-test`, and writes:

```text
dist/exact-solitaire-extension.zip
```

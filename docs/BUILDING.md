# Building

Build the ARM Kindle binary and extension package with:

```bash
./docker_rebuild.sh
```

The script starts the persistent `kindle-aisleriot-armhf-builder` container,
builds `kindle-aisleriot`, runs `smoke-test`, and writes:

```text
dist/kindle-aisleriot-extension.zip
```

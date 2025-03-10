# Yet Another Wine Launcher

## Building

`./autogen.sh && ./configure` to set up the build files, then `make && make install`.

This will output a static `yawl` binary in the `./dist/bin` directory, with the default `--prefix`.

Also, the static link libraries (curl, zlib) are included, but can be rebuilt with `make clean && ./configure`. No guarantees.

## Running

`yawl winecfg.exe`

Current useful environment variables:
- `YAWL_VERBS`: Semicolon-separated list of verbs to control yawl behavior:
  - `verify`: Verify the runtime before running
  - `reinstall`: Force reinstallation of the runtime
  - `help`: Display help and exit
  - `exec=PATH`: Set the executable to run in the container (default: `/usr/bin/wine`)

  Examples:
  - `YAWL_VERBS="reinstall;verify" yawl winecfg`
  - `YAWL_VERBS="exec=/opt/wine/bin/wine64" yawl explorer.exe`
  - `YAWL_VERBS="exec=/opt/firefox/firefox" yawl`

- Other environment variables are passed through as usual.

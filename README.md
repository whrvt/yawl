# Yet Another Wine Launcher

## Building

`./autogen.sh && ./configure` to set up the build files, then `make && make install`.

This will output a static `yawl` binary in the `./dist/bin` directory, with the default `--prefix`.

Also, the static link libraries (curl, zlib) are included, but can be rebuilt with `make clean && ./configure`. No guarantees.

## Running

`WINE_PATH=/path/to/compatbile/wine yawl winecfg.exe`

Current useful environment variables:
- `WINE_PATH`: Path to the top-level folder that contains `bin/wine`, `lib/wine`, etc. It's `/usr` by default.
- Other environment variables are passed through as usual.

TBD

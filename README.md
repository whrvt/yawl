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
  - `make_wrapper=NAME`: Create a configuration file and symlink for easy reuse
  - `config=NAME`: Use a specific named configuration

  Examples:
  - `YAWL_VERBS="reinstall;verify" yawl winecfg`
  - `YAWL_VERBS="exec=/opt/wine/bin/wine64" yawl explorer.exe`
  - `YAWL_VERBS="exec=/opt/firefox/firefox" yawl`

- Other environment variables are passed through as usual.

## Using Wrappers

yawl can create named wrappers that simplify running wine with specific configurations. This is especially useful if you have multiple Wine installations or need different configurations for different applications.

### Creating a Wrapper

```
YAWL_VERBS="make_wrapper=gaming;exec=/opt/wine-staging/bin/wine64;verify" yawl
```

This command:
1. Creates a configuration file at `~/.local/share/yawl/configs/gaming.cfg`
2. Stores the current options (`exec=` path and `verify` flag) in that file
3. Creates a symlink in the same directory as yawl (e.g., `yawl-gaming` → `yawl`)

### Using a Wrapper

Once created, you can run the wrapper directly:

```
yawl-gaming winecfg.exe
```

This will automatically load the configuration from `gaming.cfg` and run with those settings.

You can also manually specify a configuration:

```
YAWL_VERBS="config=gaming" yawl winecfg.exe
```

Note that the base name for the executable/symlink doesn't matter. It doesn't have to start with `yawl`.

### Practical Examples

1. Set up different Wine versions:
   ```
   YAWL_VERBS="make_wrapper=stable;exec=/usr/bin/wine" yawl
   YAWL_VERBS="make_wrapper=staging;exec=/opt/wine-staging/bin/wine64" yawl
   YAWL_VERBS="make_wrapper=proton;exec=/opt/proton/bin/wine" yawl
   ```

2. Create a launcher for a specific game:
   ```
   # Place this script in ~/bin/gta5
   #!/bin/sh
   exec yawl-gaming "C:\\Program Files\\Grand Theft Auto V\\GTA5.exe"
   ```

3. Set up a wrapper with special environment:
   ```
   WINEDEBUG=fixme-all DXVK_HUD=1 YAWL_VERBS="make_wrapper=debug;exec=/usr/bin/wine" yawl
   ```
   Then you can run with these debug settings: `yawl-debug GTA5.exe`

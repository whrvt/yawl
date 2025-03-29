# Yet Another Wine Launcher

## Building

`./autogen.sh && ./configure` to set up the build files/dependencies, then `make && make install`.

This will output a static `yawl` binary in the `./dist/bin` directory, with the default `--prefix`.

## Running

`yawl winecfg.exe`

Current useful environment variables:

- `YAWL_VERBS`: Semicolon-separated list of verbs to control yawl behavior:

  - `version`: Just print the version of yawl and exit
  - `verify`: Verify the runtime before running
  - `reinstall`: Force reinstallation of the runtime
  - `help`: Display help and exit
  - `check`: Check for updates to yawl (without downloading/installing)
  - `update`: Check for, download, and install available updates
  - `exec=PATH`: Set the executable to run in the container (default: `/usr/bin/wine`)
  - `wineserver=PATH`: Set the wineserver executable path when creating a wrapper
  - `make_wrapper=NAME`: Create a configuration file and symlink for easy reuse
  - `config=NAME`: Use a specific named configuration (can be the full path or lone config name with/without .cfg)
    Configs are loaded from the default install/configs directory, if specified by symlink or without a full path.
  - `enter=PID`: Run an executable in the same container as `PID` (like CheatEngine or a debugger)

  Examples:

  - `YAWL_VERBS="reinstall;verify" yawl winecfg`
  - `YAWL_VERBS="exec=/opt/wine/bin/wine64" yawl explorer.exe`
  - `YAWL_VERBS="exec=/opt/firefox/firefox" yawl`
  - `YAWL_VERBS="enter=$(pgrep game.exe)" yawl cheatengine.exe`

- `YAWL_INSTALL_DIR`: Override the default installation directory of `$XDG_DATA_HOME/yawl` or `$HOME/.local/share/yawl`

  - Do note that this setting is "volatile", it's not stored anywhere. It must be passed on each subsequent invocation to use the same install directory.

  Example:

  - `YAWL_INSTALL_DIR="$HOME/programs/winelauncher" YAWL_VERBS="reinstall" yawl`

- `YAWL_LOG_LEVEL`: Control the verbosity of the logging output. Valid values are:

  - `none`: Turn off all logging
  - `error`: Show only critical errors that prevent proper operation
  - `warning`: Show warnings and errors (default)
  - `info`: Show normal operational information and all of the above
  - `debug`: Show detailed debugging information and all of the above

- `YAWL_LOG_FILE`: Specify a custom path for the log file. By default, logs are written to:

  - Terminal output (only when running interactively)
  - `$YAWL_INSTALL_DIR/yawl.log`

- Other environment variables are passed through as usual.

## Using Wrappers

yawl can create named wrappers that simplify running wine with specific configurations. This is especially useful if you have multiple Wine installations or need different configurations for different applications.

### Creating a Wrapper

```
YAWL_VERBS="make_wrapper=gaming;exec=/opt/wine-staging/bin/wine64" yawl
```

This command:

1. Creates a configuration file at `~/.local/share/yawl/configs/gaming.cfg`
2. Stores the current options (only `exec=` for now) in that file
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

### Winetricks Integration

Many Wine tools (including winetricks) expect to be able to find a wineserver binary related to the Wine binary you're using. When creating a wrapper, you can specify a wineserver path to automatically create a corresponding wineserver wrapper.

```
YAWL_VERBS="make_wrapper=osu;exec=/opt/wine-osu/bin/wine;wineserver=/opt/wine-osu/bin/wineserver" yawl
```

This command:

1. Creates the standard wrapper (`yawl-osu`) pointing to your Wine binary
2. Also creates a wineserver wrapper (`yawl-osuserver`) pointing to your wineserver binary

This allows winetricks to use the `yawl-osuserver` wrapper as the wineserver binary, as it checks `${WINE}server` before falling back to `$PATH`. This enables seamless integration with winetricks:

```
WINEPREFIX=~/.wine WINE=yawl-osu winetricks d3dx9
```

### Practical Examples

1. Set up different Wine versions:

   ```
   YAWL_VERBS="make_wrapper=stable;exec=/usr/bin/wine;wineserver=/usr/bin/wineserver" yawl
   YAWL_VERBS="make_wrapper=staging;exec=/opt/wine-staging/bin/wine64;wineserver=/opt/wine-staging/bin/wineserver" yawl
   YAWL_VERBS="make_wrapper=proton;exec=/opt/proton/bin/wine;wineserver=/opt/proton/bin/wineserver" yawl
   ```

2. Create a launcher for a specific game:

   ```
   # Place this script in ~/bin/gta5
   #!/bin/sh
   exec yawl-gaming "C:\\Program Files\\Grand Theft Auto V\\GTA5.exe"
   ```

3. (SOON) Set up a wrapper with special a environment:

   ```
   WINEDEBUG=fixme-all DXVK_HUD=1 YAWL_VERBS="make_wrapper=debug;exec=/usr/bin/wine;wineserver=/usr/bin/wineserver" yawl
   ```

   Then you can run with these debug settings: `yawl-debug GTA5.exe`

4. Using with winetricks to install components for a game:

   ```
   # First create your wrapper with wineserver
   YAWL_VERBS="make_wrapper=mygame;exec=/opt/wine-ge/bin/wine64;wineserver=/opt/wine-ge/bin/wineserver" yawl

   # Use winetricks with your wrapper
   WINEPREFIX=~/.local/share/wineprefixes/mygame WINE=yawl-mygame winetricks dxvk vcrun2019

   # Run your game with the same wrapper
   WINEPREFIX=~/.local/share/wineprefixes/mygame yawl-mygame "C:\\Games\\MyGame\\game.exe"
   ```

# Bin & Installation Layout Architecture (bin/ & bin/libs/)

## Executive Summary
This document specifies the revised directory structure and installation layout for `SwordigoDesktop`. Compiled binaries and libraries are removed from the repository root directory and organized into dedicated, standardized directories (`bin/`, `bin/libs/`, `res/`, `config/`).

---

## 1. Traditional vs Revised Directory Layout

### Legacy Directory Structure (Current Root Pollution):
```
SwordigoDesktop/
├── Makefile
├── swordigo_boot        (80 MB binary in root)
├── ruby                 (33 MB binary in root)
├── libsre.so            (690 KB binary in root)
├── libswordigo.so       (7.2 MB game binary in root)
├── imgui.ini            (config file in root)
├── run_swordigo.sh
└── run_openswordigo.sh
```

### Revised Modularized Directory Structure (`bin/` and `bin/libs/`):
```
SwordigoDesktop/
├── bin/                 # All executable targets
│   ├── swordigo_boot    # Main desktop engine launcher
│   ├── ruby             # Standalone asset viewer / previewer
│   └── libs/            # Shared ELF libraries subfolder inside bin/
│       ├── libswd_core.so
│       ├── libswd_gui.so
│       ├── libswd_formats.so
│       ├── libswd_filerift.so
│       ├── libswd_gfx.so
│       ├── libswd_emu.so
│       ├── libopensw_ui.so
│       ├── libdynarmic.so
│       ├── libmcl.so
│       ├── libfmt.so
│       ├── libZydis.so
│       ├── libZycore.so
│       └── libswd_ffmpeg.so
├── config/              # Default configuration files
│   ├── imgui.ini
│   ├── controls.ini
│   └── mod_config.toml
├── res/                 # Shaders, game assets, and resources
│   ├── shaders/
│   └── icons/
├── Makefile             # Clean build system
├── run_swordigo.sh      # Updated wrapper script pointing to bin/
└── run_openswordigo.sh  # Updated launcher wrapper script
```

---

## 2. Updated Shell Launcher Integration

### `run_swordigo.sh`
```bash
#!/bin/bash
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export LD_LIBRARY_PATH="$SCRIPT_DIR/bin/libs:$LD_LIBRARY_PATH"
exec "$SCRIPT_DIR/bin/swordigo_boot" "$@"
```

### `run_openswordigo.sh`
```bash
#!/bin/bash
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export LD_LIBRARY_PATH="$SCRIPT_DIR/bin/libs:$LD_LIBRARY_PATH"
exec "$SCRIPT_DIR/bin/ruby" "$@"
```

---

## 3. Build Output Target Mapping & RPATH

In `Makefile`:
```makefile
BIN_DIR  := bin
LIB_DIR  := bin/libs
LDFLAGS  += -Wl,-rpath,'$$ORIGIN/libs:$$ORIGIN'

all: $(LIB_DIR)/libswd_core.so \
     $(LIB_DIR)/libswd_gui.so \
     $(LIB_DIR)/libswd_formats.so \
     $(LIB_DIR)/libswd_filerift.so \
     $(LIB_DIR)/libswd_gfx.so \
     $(LIB_DIR)/libswd_emu.so \
     $(LIB_DIR)/libopensw_ui.so \
     $(BIN_DIR)/swordigo_boot \
     $(BIN_DIR)/ruby
```

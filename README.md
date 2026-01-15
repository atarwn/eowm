> [!CAUTION]
> I don't want to support this project anymore.
> 
> This project helped me learn a lot about X11 and low-level window management,
> but maintaining an event-driven, global-state-heavy codebase in C is not something
> I want to continue doing.
> 
> There will be no further development or support.


<img src="assets/logo.svg" alt="eowm" align="right" width="150"/>

`┏┓┏┓┓┏┏┏┳┓`  
`┗ ┗┛┗┻┛┛┗┗`
============

**eowm** (eet owter winvow manader) is a lightweight, dynamic tiling window manager for X11. While it started as a clone of catwm, version 2.0 introduces a unique column-based tiling engine designed for flexibility and efficiency.

Key Features
------------
* **Dynamic Column Layout:** Organize windows into multiple columns.
* **Multi-Monitor Support:** Seamlessly handle multiple screens via XRandR.
* **EWMH Compliance:** Works with modern bars and docks (supports struts).
* **High Performance:** Uses a hash table for O(1) client lookups.

Keybinds
--------

**Mod** - Mod1Mask (Alt). You can change this to Mod4Mask (Win) in `config.h`.

| Keybind | Action |
|---------|--------|
| `Mod + j/k` | Focus next/previous window in column |
| `Mod + h/l` | Focus left/right column |
| `Mod + Shift + h/l` | Move focused window to left/right column |
| `Mod + Ctrl + h/l` | Focus next/previous monitor |
| `Mod + Ctrl + Shift + h/l` | Move focused window to next/previous monitor |
| `Mod + f` | Toggle fullscreen |
| `Mod + q` | Kill focused window |
| `Mod + Shift + c` | Quit eowm |
| `Mod + Return` | Spawn Alacritty |
| `Mod + p` | Spawn dmenu_run |
| `Mod + 1-9` | Switch workspaces |
| `Mod + Shift + 1-9` | Move window to workspace |
| `Mouse hover` | Focus window (Sloppy focus) |

Layout: Dynamic Columns
-----------------------

Unlike the traditional Master/Stack layout, **eowm** treats the screen as a series of columns.

```
 ______ ______ ______
|      |      |      |
|      |      |______|
|______|      |      |
|      |      |______|
|      |      |      |
|______|______|______|
```

* Each workspace supports up to 16 columns.
* Windows within a column share height equally.
* Empty columns are automatically compacted to reclaim space.
* Fullscreen mode hides all other windows for total focus.

Installation
------------

```bash
# Edit config.h to suit your needs
make
sudo make install

```

## Naming

The evolution of the name:

* **catwm** - The origin
* **kittywm** - Too silly
* **meowm** - Too many M's
* **eowm** - Just right. "eet owter winvow manader" (v2.0) 

## Thanks to

* [catwm](https://github.com/pyknite/catwm) - Original inspiration
* [dwm](https://git.suckless.org/dwm) - For the best C patterns and X11 logic
* [tinywm](https://github.com/mackstann/tinywm) - Helping understand the basics

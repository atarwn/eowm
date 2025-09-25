eowm - eet owter winvow manader

eowm
----

This is a simple window manager, ispired by catwm and ratpoison.  
Might be used for learning purporses

Keybinds
-------


 * Mod + j/k -> next/prev window
 * Mod + h/l -> inc/dec master
 * Mod + Return (Enter) -> spawn alacritty
 * Mod + p -> spawn dmenu\_run
 * Mod + f -> fullscreen
 * Mod + Space -> toggle master
 * Mod + q -> kill window
 * Mod + c -> quit
 * Mouse hover -> focus

Layout
------

```
 ____ ______________
|    |              |
|____|              |
|    |    Master    |
|____|              |
|    |              |
|____|______________|
```

No borders, no UI, new window pushes master to the top of the stack

Why?!
-----

i can do whatever i want

Known bugs
----------
 * Exiting the console using Ctrl+D results in `(explicit kill or server shutdown).ourve id in failed request: 0xc00003 ...`

Thanks to
---------

 * 2bwm
 * catwm
 * tinywm
 * dwm
 * sxwm

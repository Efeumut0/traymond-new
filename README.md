![Traymond](https://github.com/fcFn/fcFn.github.io/blob/master/images/logos/traymond_logo.png) Traymond
=======

A reworked version of the original [Traymond](https://github.com/fcFn/traymond) by [fcFn](https://github.com/fcFn).

A very simple app for minimizing any window to tray as an icon. Runs in the background.

The original project was built around a lightweight and practical idea: hide windows in the system tray instead of leaving them open on the taskbar. This version keeps that idea, while improving reliability and fixing several issues found in older builds.

It includes fixes for hidden windows not reopening properly, missing or broken tray icons, invalid restore entries, crash-recovery problems, and startup or elevated-run tray visibility issues.

Installing
------------

No installation required, just run `Traymond.exe`.

Controls
--------

+ __Win key + Shift + Z__: Minimize the currently focused window to tray.

+ __Click or double-click on an icon__: Bring back the corresponding hidden window.

+ __Tray icon menu__ accessible by right-clicking the Traymond tray icon:

  + __Restore all windows__: Restore all previously hidden windows.

  + __Exit__: Exit Traymond and restore all previously hidden windows.

Building
--------

### Nmake

`> nmake`

### Microsoft Visual Studio

Import and build using the included project files.

Contributing
------------

Contributions, fixes, and improvements are welcome.
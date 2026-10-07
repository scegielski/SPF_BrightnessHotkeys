# SPF Brightness Hotkeys

Live VR brightness adjustment for American Truck Simulator through SPF hotkeys, with configurable increments and reset. Version 1.0.1.

## Controls and settings

Bind **Brighter**, **Dimmer**, **Reset brightness**, and **Open brightness settings** in SPF's Keybind Settings. All shortcuts start unassigned. The settings window lets you configure the brightness step, reset target, and starting value.

This is an independent SPF plugin; Console Command Hotkeys is not required. Install the built DLL in `spfPlugins/SPF_BrightnessHotkeys/` with ATS closed, and enable the plugin in SPF. Keep your existing config folder when updating.

Brightness is tracked from commands sent by the plugin, rather than live game readback. Activation reads the saved ATS configuration without changing brightness. If you change brightness through the game's graphics menu or console, reload the saved value or apply a known starting value in the plugin window. The plugin limits values to -2 through 3.

## Build and test

Use Windows with Visual Studio C++ Build Tools and CMake:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

The DLL is staged in `build/package/SPF_BrightnessHotkeys/`.

## Migration

`MigrateSettings.py` migrates brightness settings and bindings from the earlier combined Console Command Hotkeys plugin, backs up affected configuration files, and preserves existing destination choices. Run it with ATS closed, passing the SPF plugins directory as its argument. Use the split Console Command Hotkeys build to avoid duplicate brightness controls.

## Attribution

Extracted and modified from the brightness implementation in the modified Track & Truck Devs Console Command Hotkeys plugin. The original Apache 2.0 license is retained in LICENSE. SPF API headers are included for compilation.

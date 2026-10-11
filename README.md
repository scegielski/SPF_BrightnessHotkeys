# SPF Brightness Hotkeys

Live VR brightness adjustment for American Truck Simulator through SPF hotkeys, with configurable increments and reset. Version 1.0.26.

## Controls and settings

Bind **Brighter**, **Dimmer**, **Reset brightness**, and **Open brightness settings** in SPF's Keybind Settings. All shortcuts start unassigned. The settings window lets you configure the brightness step, reset target, and starting value.

This is an independent SPF plugin; Console Command Hotkeys is not required. Install the built DLL in `spfPlugins/SPF_BrightnessHotkeys/` with ATS closed, and enable the plugin in SPF. Keep your existing config folder when updating.

Brightness is tracked from commands sent by the plugin, rather than live game readback. Activation reads the saved ATS configuration without changing brightness. If you change brightness through the game's graphics menu or console, reload the saved value or apply a known starting value in the plugin window. The plugin limits values to -2 through 3.

## Custom eye adaptation

The brightness window also provides a replacement adaptation shader with hard minimum and maximum exposure limits, independent darkening and brightening speed multipliers, and separate exposure multipliers for bright and dark scenes. Valid edits apply and save immediately; enabled presets reload when the vehicle and renderer are ready. **Restore original adaptation** restores the stock shader and disables automatic application.

Speed multipliers range from 0.001 to 100; higher values respond faster. They are not durations in seconds. Scene multipliers adjust the target before smoothing and hard limits: below one darkens, above one brightens. The panel includes exposure-history readback and paired adapted-exposure/target readings from the custom shader.

Native shader replacement and exposure controls have been tested in ATS VR. The latest paired GPU capture passes software DirectX tests but still needs in-game confirmation; until a valid sample arrives, its readouts show a waiting message. Native renderer hooks are signature-gated to the inspected ATS 1.61.3.1s executable and may require updates for other game versions. The plugin restores its replacement shader on unload. No archive override mod is required.

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

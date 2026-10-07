    Brightness::State brightness;
    int64_t brightnessReset = 0;
    double editStep = 0.1, editStart = 0.0, editReset = 0.0;
    std::string brightnessSource = "Starting value required";
    std::string brightnessError;

    void SaveBrightness() {
        if (!g_ctx.coreAPI || !g_ctx.coreAPI->config || !g_ctx.configHandle) return;
        auto* cfg = g_ctx.coreAPI->config;
        cfg->Cfg_SetFloat(g_ctx.configHandle, "settings.brightness.step", static_cast<double>(brightness.step) / Brightness::Scale);
        cfg->Cfg_SetFloat(g_ctx.configHandle, "settings.brightness.reset", static_cast<double>(brightnessReset) / Brightness::Scale);
        cfg->Cfg_SetFloat(g_ctx.configHandle, "settings.brightness.current", static_cast<double>(brightness.value) / Brightness::Scale);
        cfg->Cfg_SetBool(g_ctx.configHandle, "settings.brightness.initialized", brightness.ready);
        cfg->Cfg_Save(g_ctx.configHandle);
    }

    bool ReadSavedBrightness(int64_t& value) {
        if (!g_ctx.coreAPI || !g_ctx.coreAPI->environment) return false;
        auto* env = g_ctx.coreAPI->environment;
        auto* handle = env->Env_GetContext(PLUGIN_NAME);
        if (!handle) return false;
        char path[4096] = {};
        if (env->Env_GetSCSUserDir(handle, path, sizeof(path)) <= 0) return false;
        std::ifstream file(std::filesystem::path(path) / "config.cfg");
        std::string line;
        while (std::getline(file, line)) if (Brightness::Assignment(line, value)) return true;
        return false;
    }

    void LoadBrightness() {
        if (!g_ctx.coreAPI || !g_ctx.coreAPI->config || !g_ctx.configHandle) return;
        auto* cfg = g_ctx.coreAPI->config;
        brightness = {};
        Brightness::Units(cfg->Cfg_GetFloat(g_ctx.configHandle, "settings.brightness.step", 0.1), brightness.step, true);
        brightnessReset = 0;
        Brightness::Units(cfg->Cfg_GetFloat(g_ctx.configHandle, "settings.brightness.reset", 0.0), brightnessReset);
        brightness.ready = ReadSavedBrightness(brightness.value);
        brightnessSource = "ATS saved config (not live readback)";
        if (!brightness.ready) {
            brightness.ready = cfg->Cfg_GetBool(g_ctx.configHandle, "settings.brightness.initialized", false) &&
                Brightness::Units(cfg->Cfg_GetFloat(g_ctx.configHandle, "settings.brightness.current", 0.0), brightness.value);
            brightnessSource = brightness.ready ? "Last plugin value (not live readback)" : "Starting value required";
        }
        editStep = static_cast<double>(brightness.step) / Brightness::Scale;
        editStart = static_cast<double>(brightness.value) / Brightness::Scale;
        editReset = static_cast<double>(brightnessReset) / Brightness::Scale;
        // Add only our settings; never rewrite the user's commands or keybinds.
        SaveBrightness();
    }

    void NotifyBrightness(const std::string& text) {
        if (!g_ctx.uiAPI) return;
        SPF_Notification_Params p = {SPF_NOTIFICATION_INFO, text.c_str(), SPF_NOTIF_MODE_TOP, -1.0f};
        g_ctx.uiAPI->UI_ShowNotification(&p);
    }

    void ApplyBrightness(int64_t value) {
        if (!g_ctx.gameConsoleAPI) return;
        const auto command = "uset r_sdr_display_gray_offset \"" + Brightness::Decimal(value) + "\"";
        g_ctx.gameConsoleAPI->GCon_ExecuteCommand(command.c_str());
        brightness.value = value; brightness.ready = true;
        editStart = static_cast<double>(value) / Brightness::Scale;
        brightnessSource = "Last command sent by this plugin (not live readback)";
        SaveBrightness();
        if (g_ctx.showNotifications) NotifyBrightness("Brightness: " + Brightness::Decimal(value));
    }

    void OnBrighter(const char*, void*) {
        if (!brightness.ready) { NotifyBrightness("Set a starting brightness in the plugin manager first."); return; }
        ApplyBrightness(brightness.Next(1));
    }
    void OnDimmer(const char*, void*) {
        if (!brightness.ready) { NotifyBrightness("Set a starting brightness in the plugin manager first."); return; }
        ApplyBrightness(brightness.Next(-1));
    }
    void OnResetBrightness(const char*, void*) { ApplyBrightness(brightnessReset); }

    void RenderBrightness(SPF_UI_API* ui) {
        ui->UI_Text("VR brightness");
        ui->UI_TextWrapped("Assign Brighter, Dimmer and Reset Brightness in SPF Key Binds. Range: -2 to 3 (plugin guardrails). Precision: 0.000001.");
        const auto tracked = brightness.ready ? "Tracked value: " + Brightness::Decimal(brightness.value) : "Tracked value: unset";
        ui->UI_Text(tracked.c_str()); ui->UI_TextWrapped(brightnessSource.c_str());
        ui->UI_TextWrapped("Separate graphics-slider or console changes can desynchronize tracking. Set and apply a known value below to resynchronize. Saved config is read on activation only.");
        ui->UI_InputDouble("Brightness step", &editStep, 0, 0, "%.6f", SPF_INPUT_TEXT_FLAG_NONE);
        ui->UI_InputDouble("Reset target", &editReset, 0, 0, "%.6f", SPF_INPUT_TEXT_FLAG_NONE);
        if (ui->UI_Button("Save brightness settings", 0, 0)) {
            int64_t step, reset;
            if (!Brightness::Units(editStep, step, true) || !Brightness::Units(editReset, reset))
                brightnessError = "Step must be finite, 0.000001 to 5; reset must be finite, -2 to 3.";
            else { brightness.step = step; brightnessReset = reset; SaveBrightness(); brightnessError.clear(); }
        }
        ui->UI_InputDouble("Starting / current value", &editStart, 0, 0, "%.6f", SPF_INPUT_TEXT_FLAG_NONE);
        if (ui->UI_Button("Set and apply brightness", 0, 0)) {
            int64_t value;
            if (!Brightness::Units(editStart, value)) brightnessError = "Starting value must be finite, -2 to 3.";
            else { ApplyBrightness(value); brightnessError.clear(); }
        }
        if (ui->UI_Button("Reload tracked value from saved ATS config", 0, 0)) {
            int64_t value;
            if (!ReadSavedBrightness(value)) brightnessError = "No valid saved brightness found; set and apply a starting value.";
            else {
                brightness.value = value; brightness.ready = true;
                editStart = static_cast<double>(value) / Brightness::Scale;
                brightnessSource = "ATS saved config (not live readback)"; SaveBrightness(); brightnessError.clear();
            }
        }
        if (!brightnessError.empty()) ui->UI_TextWrapped(brightnessError.c_str());
        ui->UI_Separator();
    }

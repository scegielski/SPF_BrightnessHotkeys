#include "BrightnessPlugin.cpp"
#include <iostream>
#include <map>
#include <vector>
#include <limits>
#include <stdexcept>

using namespace SPF_BrightnessHotkeys;
std::map<std::string, double> numbers;
std::map<std::string, bool> flags;
std::vector<std::string> sent;
void Check(bool result, const char* name) { if (!result) throw std::runtime_error(name); }
double GetFloat(SPF_Config_Handle*, const char* key, double fallback) { return numbers.count(key) ? numbers[key] : fallback; }
bool GetBool(SPF_Config_Handle*, const char* key, bool fallback) { return flags.count(key) ? flags[key] : fallback; }
void SetFloat(SPF_Config_Handle*, const char* key, double value) { numbers[key] = value; }
void SetBool(SPF_Config_Handle*, const char* key, bool value) { flags[key] = value; }
void Save(SPF_Config_Handle*) {}
void Execute(const char* command) { sent.push_back(command); }
SPF_Environment_Handle* EnvContext(const char*) { return reinterpret_cast<SPF_Environment_Handle*>(1); }
int UserDir(SPF_Environment_Handle*, char* out, int size) { strcpy_s(out, size, "BrightnessTestData"); return 18; }
int main() {
    std::filesystem::remove("BrightnessTestData/config.cfg");
    int64_t units = 42;
    Check(!Brightness::Units(std::numeric_limits<double>::infinity(), units, true), "infinite step");
    Check(!Brightness::Units(std::numeric_limits<double>::quiet_NaN(), units, true), "NaN step");
    Check(!Brightness::Units(0, units, true) && !Brightness::Units(-0.1, units, true), "nonpositive step");
    Check(!Brightness::Units(0.0000001, units, true) && !Brightness::Units(6, units, true), "step bounds");
    Check(!Brightness::Units(-2.1, units) && !Brightness::Units(3.1, units), "value bounds");
    Check(Brightness::Assignment("uset r_sdr_display_gray_offset \"1\"", units) && units == 1000000, "high assignment");
    Check(Brightness::Assignment("uset r_sdr_display_gray_offset \"-0.5\"", units) && units == -500000, "low assignment");
    Check(Brightness::Assignment("r_sdr_display_gray_offset 1.08", units) && units == 1080000, "saved config parsing");
    Check(!Brightness::Assignment("uset r_sdr_display_gray_offset \"1\"; quit", units), "compound command rejected");
    Check(!Brightness::Assignment("uset r_sdr_display_gray_offset \"1", units), "unclosed quote");
    SPF_Config_API config = {}; config.Cfg_GetFloat = GetFloat; config.Cfg_GetBool = GetBool;
    config.Cfg_SetFloat = SetFloat; config.Cfg_SetBool = SetBool; config.Cfg_Save = Save;
    SPF_Core_API core = {}; core.config = &config;
    SPF_GameConsole_API console = {}; console.GCon_ExecuteCommand = Execute;
    g_ctx.coreAPI = &core; g_ctx.configHandle = reinterpret_cast<SPF_Config_Handle*>(1);
    g_ctx.gameConsoleAPI = &console; g_ctx.showNotifications = false;
    LoadBrightness();
    Check(!brightness.ready && brightness.step == 100000, "old config migration requires explicit start without saved file");
    OnBrighter(nullptr, nullptr); Check(sent.empty(), "no arbitrary starting jump");
    ApplyBrightness(1000000);
    Check(brightness.value == 1000000 && brightness.ready, "existing high synchronizes");
    for (int i=0; i<10; ++i) OnBrighter(nullptr, nullptr);
    Check(brightness.value == 2000000 && sent.back() == "uset r_sdr_display_gray_offset \"2.000000\"", "ten precise 0.1 increments");
    for (int i=0; i<10; ++i) OnDimmer(nullptr, nullptr);
    Check(brightness.value == 1000000, "reversible decimal increments");
    ApplyBrightness(-500000); OnDimmer(nullptr, nullptr);
    Check(brightness.value == -600000, "existing low and decrement");
    brightness.step = 250000; OnBrighter(nullptr, nullptr);
    Check(brightness.value == -350000, "configurable step");
    brightnessReset = 1080000; OnResetBrightness(nullptr, nullptr);
    Check(brightness.value == 1080000 && numbers["settings.brightness.reset"] == 1.08, "configurable reset persists and synchronizes");
    brightness.value = Brightness::Max; OnBrighter(nullptr, nullptr); Check(brightness.value == Brightness::Max, "upper clamp");
    brightness.value = Brightness::Min; OnDimmer(nullptr, nullptr); Check(brightness.value == Brightness::Min, "lower clamp");
    SaveBrightness(); LoadBrightness();
    Check(brightness.ready && brightness.value == Brightness::Min && brightness.step == 250000 && brightnessReset == 1080000, "persistence restoration");
    SPF_Environment_API environment = {}; environment.Env_GetContext = EnvContext; environment.Env_GetSCSUserDir = UserDir;
    core.environment = &environment;
    std::filesystem::create_directories("BrightnessTestData");
    { std::ofstream file("BrightnessTestData/config.cfg"); file << "uset r_sdr_display_gray_offset \"1.08\"\n"; }
    const auto previousCount = sent.size();
    LoadBrightness();
    Check(brightness.ready && brightness.value == 1080000 && sent.size() == previousCount, "saved ATS config initializes without applying command");
    std::cout << "PASS: independent brightness precision, bounds, reset, persistence and saved config initialization\n";
}

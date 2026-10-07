#include <SPF_Plugin.h>
#include <SPF_Manifest_API.h>
#include <SPF_Config_API.h>
#include <SPF_KeyBinds_API.h>
#include <SPF_UI_API.h>
#include <SPF_GameConsole_API.h>
#include <SPF_Environment_API.h>
#include "Brightness.hpp"
#include <filesystem>
#include <fstream>

namespace SPF_BrightnessHotkeys {
constexpr const char* PLUGIN_NAME="SPF_BrightnessHotkeys";
struct Context {
 const SPF_Core_API* coreAPI=nullptr;
 SPF_Config_Handle* configHandle=nullptr;
 SPF_KeyBinds_Handle* keys=nullptr;
 SPF_GameConsole_API* gameConsoleAPI=nullptr;
 SPF_UI_API* uiAPI=nullptr;
 SPF_Window_Handle* window=nullptr;
 bool showNotifications=true;
} g_ctx;
#include "BrightnessIntegration.inc"
void ToggleWindow() {
 if(g_ctx.uiAPI && g_ctx.window) g_ctx.uiAPI->UI_SetVisibility(g_ctx.window,!g_ctx.uiAPI->UI_IsVisible(g_ctx.window));
}
void Render(SPF_UI_API* ui,void*) { RenderBrightness(ui); }
void BuildManifest(SPF_Manifest_Builder_Handle* h,const SPF_Manifest_Builder_API* api) {
 api->Info_SetName(h,PLUGIN_NAME);api->Info_SetVersion(h,"1.0.1");api->Info_SetMinFrameworkVersion(h,"1.2.5");
 api->Info_SetAuthor(h,"SPF Brightness Hotkeys contributors");
 api->Info_SetDescriptionLiteral(h,"Independent brightness adjustment with configurable steps, reset and SPF bindings.");
 api->Policy_SetAllowUserConfig(h,true);api->Policy_AddConfigurableSystem(h,"settings");api->Policy_AddConfigurableSystem(h,"ui");
 api->Policy_AddRequiredHook(h,"GameConsole");
 api->Settings_SetJson(h,R"json({"brightness":{"step":0.1,"reset":0.0,"initialized":false},"options":{"show_notifications":true}})json");
 api->Meta_AddCustomSetting(h,"brightness",nullptr,nullptr,nullptr,nullptr,true);
 api->Meta_AddCustomSetting(h,"options.show_notifications","Brightness notifications","Show the value after adjustment.","checkbox",nullptr,false);
 api->Defaults_AddWindow(h,"MainWindow",false,true,500,200,650,450,false,false);
 api->Meta_AddWindow(h,"MainWindow","Brightness settings",nullptr);
 api->Meta_AddKeybind(h,"MainWindow","toggle","Open brightness settings","Show or hide brightness settings.");
}
void OnLoad(const SPF_Load_API* api) { if(api && api->config) g_ctx.configHandle=api->config->Cfg_GetContext(PLUGIN_NAME); }
void OnActivated(const SPF_Core_API* api) {
 if(!api)return;
 g_ctx.coreAPI=api;g_ctx.uiAPI=api->ui;g_ctx.gameConsoleAPI=api->console;
 LoadBrightness();
 if(api->keybinds) {
  g_ctx.keys=api->keybinds->Kbind_GetContext(PLUGIN_NAME);
  api->keybinds->Kbind_RegisterActionMetadata(g_ctx.keys,"brightness_brighter","Brighter","Increase brightness by configured step",OnBrighter,nullptr);
  api->keybinds->Kbind_RegisterActionMetadata(g_ctx.keys,"brightness_dimmer","Dimmer","Decrease brightness by configured step",OnDimmer,nullptr);
  api->keybinds->Kbind_RegisterActionMetadata(g_ctx.keys,"brightness_reset","Reset brightness","Apply configured reset target",OnResetBrightness,nullptr);
  api->keybinds->Kbind_RegisterActionMetadata(g_ctx.keys,"MainWindow.toggle","Open brightness settings","Show or hide brightness settings",nullptr,nullptr);
  api->keybinds->Kbind_Register(g_ctx.keys,"MainWindow.toggle",ToggleWindow);
 }
}
void OnRegisterUI(SPF_UI_API* api) {
 if(!api)return;g_ctx.uiAPI=api;
 api->UI_RegisterDrawCallback(PLUGIN_NAME,"MainWindow",Render,nullptr);
 g_ctx.window=api->UI_GetWindowHandle(PLUGIN_NAME,"MainWindow");
}
void OnUpdate() {
 if(g_ctx.coreAPI && g_ctx.coreAPI->config && g_ctx.configHandle)
  g_ctx.showNotifications=g_ctx.coreAPI->config->Cfg_GetBool(g_ctx.configHandle,"settings.options.show_notifications",true);
}
void OnUnload() { SaveBrightness();g_ctx={}; }
}
extern "C" {
SPF_PLUGIN_EXPORT bool SPF_GetManifestAPI(SPF_Manifest_API* api) { if(!api)return false;api->BuildManifest=SPF_BrightnessHotkeys::BuildManifest;return true; }
SPF_PLUGIN_EXPORT bool SPF_GetPlugin(SPF_Plugin_Exports* api) {
 if(!api)return false;
 api->OnLoad=SPF_BrightnessHotkeys::OnLoad;api->OnActivated=SPF_BrightnessHotkeys::OnActivated;
 api->OnRegisterUI=SPF_BrightnessHotkeys::OnRegisterUI;api->OnUpdate=SPF_BrightnessHotkeys::OnUpdate;api->OnUnload=SPF_BrightnessHotkeys::OnUnload;return true;
}
}

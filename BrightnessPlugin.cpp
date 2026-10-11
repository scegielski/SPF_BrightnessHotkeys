#include <SPF_Plugin.h>
#include <SPF_Manifest_API.h>
#include <SPF_Config_API.h>
#include <SPF_KeyBinds_API.h>
#include <SPF_UI_API.h>
#include <SPF_GameConsole_API.h>
#include <SPF_Environment_API.h>
#include <SPF_Climate_API.h>
#include <SPF_Logger_API.h>
#include <SPF_Hooks_API.h>
#define NOMINMAX
#include <Windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <iomanip>
#include <locale>
#include "CustomAdaptationSource.hpp"
#include "ProbeShader.hpp"
#include <mutex>
#include <atomic>
#include <cstring>
#include <chrono>
#include <sstream>
#include <cmath>
#include <limits>
#include <vector>
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
 SPF_Logger_Handle* logger=nullptr;
 std::chrono::steady_clock::time_point lastDiagnostic{};
 std::string diagnostic="Waiting for a loaded lighting profile.";
} g_ctx;
#include "RendererObserver.inc"
#include "ShaderObserver.inc"
void RestoreAdaptationTest();
#include "ShaderProbe.inc"
#include "ExposurePairObserver.inc"
struct GrayBackup { SPF_Climate_ProfileRef profile; uint64_t variation; float value; };
std::vector<GrayBackup> grayBackups;
struct SpeedBackup { SPF_Climate_ProfileRef profile; uint64_t variation; float value; SPF_CL_Profile_SetFloatByIndex_t setter; };
std::vector<SpeedBackup> speedBackups;
const char* activeTest="None";
std::chrono::steady_clock::time_point testEnds;
void RestoreAdaptationTest() {
 auto* cl=g_ctx.coreAPI ? g_ctx.coreAPI->climate : nullptr;
 if(cl && cl->SetTargetGrayByIndex) for(const auto& b:grayBackups) cl->SetTargetGrayByIndex(b.profile,b.variation,b.value);
 grayBackups.clear();
 for(const auto& b:speedBackups) b.setter(b.profile,b.variation,b.value);
 speedBackups.clear();activeTest="None";
}
void StartSpeedTest(float speed) {
 auto* cl=g_ctx.coreAPI ? g_ctx.coreAPI->climate : nullptr;
 if(!grayBackups.empty() || !speedBackups.empty() || !cl || !cl->CL_GetActiveSunProfileIndex || cl->CL_GetActiveSunProfileIndex()<0 ||
    !cl->CL_ActiveProfile || !cl->CL_NextProfile || !cl->CL_GetSunProfileCount ||
    !cl->GetDarkAdaptationSpeedCount || !cl->GetDarkAdaptationSpeedByIndex || !cl->SetDarkAdaptationSpeedByIndex ||
    !cl->GetBrightAdaptationSpeedCount || !cl->GetBrightAdaptationSpeedByIndex || !cl->SetBrightAdaptationSpeedByIndex) return;
 const SPF_Climate_ProfileRef profiles[]={cl->CL_ActiveProfile(),cl->CL_NextProfile()};
 std::vector<SpeedBackup> saved;
 for(int i=0;i<2;++i) {
  const auto p=profiles[i];
  if(i && p.index==profiles[0].index && p.isBad==profiles[0].isBad) continue;
  const auto count=cl->CL_GetSunProfileCount(p.isBad);
  if(count<=0 || p.index>=static_cast<uint64_t>(count)) return;
  auto capture=[&](SPF_CL_Profile_GetCount_t getCount,SPF_CL_Profile_GetFloatByIndex_t getter,SPF_CL_Profile_SetFloatByIndex_t setter) {
   const auto n=getCount(p);if(!n || n>64)return false;
   for(uint64_t v=0;v<n;++v) {const float value=getter(p,v);if(!std::isfinite(value) || value<0 || value>100)return false;saved.push_back({p,v,value,setter});}
   return true;
  };
  if(!capture(cl->GetDarkAdaptationSpeedCount,cl->GetDarkAdaptationSpeedByIndex,cl->SetDarkAdaptationSpeedByIndex) ||
     !capture(cl->GetBrightAdaptationSpeedCount,cl->GetBrightAdaptationSpeedByIndex,cl->SetBrightAdaptationSpeedByIndex))return;
 }
 speedBackups=std::move(saved);
 for(const auto& b:speedBackups)b.setter(b.profile,b.variation,speed);
 activeTest=speed<1 ? "Slow adaptation speed test" : "Fast adaptation speed test";
 testEnds=std::chrono::steady_clock::now()+std::chrono::seconds(45);
}
void StartAdaptationTest() {
 auto* cl=g_ctx.coreAPI ? g_ctx.coreAPI->climate : nullptr;
 if(!grayBackups.empty() || !speedBackups.empty() || !cl || !cl->CL_GetActiveSunProfileIndex || cl->CL_GetActiveSunProfileIndex()<0 ||
    !cl->CL_ActiveProfile || !cl->CL_NextProfile || !cl->CL_GetSunProfileCount ||
    !cl->GetTargetGrayCount || !cl->GetTargetGrayByIndex || !cl->SetTargetGrayByIndex) return;
 const SPF_Climate_ProfileRef profiles[]={cl->CL_ActiveProfile(),cl->CL_NextProfile()};
 std::vector<GrayBackup> saved;
 for(int i=0;i<2;++i) {
  const auto p=profiles[i];
  if(i && p.index==profiles[0].index && p.isBad==profiles[0].isBad) continue;
  const auto count=cl->CL_GetSunProfileCount(p.isBad);
  if(count<=0 || p.index>=static_cast<uint64_t>(count)) return;
  const auto variations=cl->GetTargetGrayCount(p);
  if(!variations || variations>64) return;
  for(uint64_t v=0;v<variations;++v) {
   const float original=cl->GetTargetGrayByIndex(p,v);
   if(!std::isfinite(original) || original<=0 || original>1) return;
   saved.push_back({p,v,original});
  }
 }
 grayBackups=std::move(saved);
 activeTest="Target gray test";
 for(const auto& b:grayBackups) cl->SetTargetGrayByIndex(b.profile,b.variation,0.6f);
 testEnds=std::chrono::steady_clock::now()+std::chrono::seconds(30);
}
void StartOverrideTest() {
 auto* cl=g_ctx.coreAPI ? g_ctx.coreAPI->climate : nullptr;
 if(!grayBackups.empty() || !speedBackups.empty() || !cl || !cl->CL_GetActiveSunProfileIndex || cl->CL_GetActiveSunProfileIndex()<0 ||
    !cl->CL_ActiveProfile || !cl->CL_NextProfile || !cl->CL_GetSunProfileCount ||
    !cl->GetScaleOverrideCount || !cl->GetScaleOverrideByIndex || !cl->SetScaleOverrideByIndex) return;
 const SPF_Climate_ProfileRef profiles[]={cl->CL_ActiveProfile(),cl->CL_NextProfile()};
 std::vector<SpeedBackup> saved;
 for(int i=0;i<2;++i) {
  const auto p=profiles[i];
  if(i && p.index==profiles[0].index && p.isBad==profiles[0].isBad)continue;
  const auto count=cl->CL_GetSunProfileCount(p.isBad);
  if(count<=0 || p.index>=static_cast<uint64_t>(count))return;
  const auto variations=cl->GetScaleOverrideCount(p);
  if(!variations || variations>64)return;
  for(uint64_t v=0;v<variations;++v) {
   const float original=cl->GetScaleOverrideByIndex(p,v);
   if(!std::isfinite(original) || original<0 || original>10)return;
   saved.push_back({p,v,original,cl->SetScaleOverrideByIndex});
  }
 }
 speedBackups=std::move(saved);
 for(const auto& b:speedBackups)b.setter(b.profile,b.variation,0.0f);
 activeTest="Experimental zero exposure override test";
 testEnds=std::chrono::steady_clock::now()+std::chrono::seconds(45);
}
double editMinScale=0.005,editMaxScale=8.0,editDarkSpeed=0.02,editBrightSpeed=2.0,editBrightSceneMultiplier=1.0,editDarkSceneMultiplier=1.0;
std::string adaptationError;
std::string adaptationApplyStatus="Custom adaptation has not been applied this run.";
bool ValidAdaptationEdits() {return std::isfinite(editBrightSceneMultiplier) && editBrightSceneMultiplier>=0.001 && editBrightSceneMultiplier<=100 && std::isfinite(editDarkSceneMultiplier) && editDarkSceneMultiplier>=0.001 && editDarkSceneMultiplier<=100 && std::isfinite(editMinScale) && std::isfinite(editMaxScale) && std::isfinite(editDarkSpeed) && std::isfinite(editBrightSpeed) && editMinScale>=0.0001 && editMaxScale<=100 && editMaxScale>=editMinScale && editDarkSpeed>=0.001 && editDarkSpeed<=100 && editBrightSpeed>=0.001 && editBrightSpeed<=100;}
void StartExaggeratedTest(bool custom=false) {
 auto* cl=g_ctx.coreAPI?g_ctx.coreAPI->climate:nullptr;
 if(!grayBackups.empty() || !speedBackups.empty() || !cl || !cl->CL_GetActiveSunProfileIndex || cl->CL_GetActiveSunProfileIndex()<0 || !cl->CL_ActiveProfile || !cl->CL_NextProfile || !cl->CL_GetSunProfileCount)return;
 struct Field {SPF_CL_Profile_GetCount_t count;SPF_CL_Profile_GetFloatByIndex_t get;SPF_CL_Profile_SetFloatByIndex_t set;float target;};
 const Field fields[]={
  {cl->GetMinScaleCount,cl->GetMinScaleByIndex,cl->SetMinScaleByIndex,custom?static_cast<float>(editMinScale):0.005f},
  {cl->GetMaxScaleCount,cl->GetMaxScaleByIndex,cl->SetMaxScaleByIndex,custom?static_cast<float>(editMaxScale):8.0f},
  {cl->GetDarkAdaptationSpeedCount,cl->GetDarkAdaptationSpeedByIndex,cl->SetDarkAdaptationSpeedByIndex,custom?static_cast<float>(editDarkSpeed):0.02f},
  {cl->GetBrightAdaptationSpeedCount,cl->GetBrightAdaptationSpeedByIndex,cl->SetBrightAdaptationSpeedByIndex,custom?static_cast<float>(editBrightSpeed):2.0f}};
 for(const auto& f:fields)if(!f.count || !f.get || !f.set)return;
 const SPF_Climate_ProfileRef profiles[]={cl->CL_ActiveProfile(),cl->CL_NextProfile()};
 std::vector<SpeedBackup> saved;std::vector<float> targets;
 for(int i=0;i<2;++i){auto p=profiles[i];if(i && p.index==profiles[0].index && p.isBad==profiles[0].isBad)continue;
  const auto count=cl->CL_GetSunProfileCount(p.isBad);if(count<=0 || p.index>=static_cast<uint64_t>(count))return;
  for(const auto& f:fields){const auto n=f.count(p);if(!n || n>64)return;
   for(uint64_t v=0;v<n;++v){float original=f.get(p,v);if(!std::isfinite(original) || original<0 || original>10000)return;saved.push_back({p,v,original,f.set});targets.push_back(f.target);}
  }
 }
 speedBackups=std::move(saved);
 for(size_t i=0;i<speedBackups.size();++i){const auto& b=speedBackups[i];b.setter(b.profile,b.variation,targets[i]);}
 activeTest=custom?"Custom adaptation active until restored or plugin unload":"Experimental dark-cabin adaptation: same limits, reversed speeds";
 testEnds=custom?std::chrono::steady_clock::time_point::max():std::chrono::steady_clock::now()+std::chrono::seconds(60);
}
#include "BrightnessIntegration.inc"
void ReadAdaptation() {
 auto* api=g_ctx.coreAPI;
 auto* cl=api ? api->climate : nullptr;
 g_ctx.diagnostic="Climate getters unavailable.";
 if(cl && cl->CL_GetActiveSunProfileIndex && cl->CL_ActiveProfile && cl->CL_GetSunProfileCount &&
    cl->GetDarkAdaptationSpeed && cl->GetBrightAdaptationSpeed && cl->GetTargetGray &&
    cl->GetLowIntensityMinimum && cl->GetLowIntensityMaximum &&
    cl->GetBlendedDarkAdaptationSpeed && cl->GetBlendedBrightAdaptationSpeed &&
    cl->GetBlendedTargetGray && cl->GetBlendedLowIntensityMinimum && cl->GetBlendedLowIntensityMaximum) {
  if(cl->CL_GetActiveSunProfileIndex()<0) g_ctx.diagnostic="No active lighting profile; load a vehicle.";
  else {
   const auto profile=cl->CL_ActiveProfile();
   const auto count=cl->CL_GetSunProfileCount(profile.isBad);
   if(count<=0 || profile.index>=static_cast<uint64_t>(count)) g_ctx.diagnostic="Lighting profile unavailable.";
   else {
    std::ostringstream out;
    out<<"Eye adaptation configuration (read-only): test="<<activeTest<<"; profile="<<profile.index<<", bad_weather="<<profile.isBad;
    auto field=[&](const char* name, SPF_CL_Profile_GetFloat_t get, SPF_CL_GetBlendedFloat_t blend) {
     const float value=get(profile), blended=blend();
     out<<"; "<<name<<"=";
     if(std::isfinite(value)) out<<value; else out<<"invalid";
     out<<", blended=";
     if(std::isfinite(blended)) out<<blended; else out<<"invalid";
    };
    field("dark_speed",cl->GetDarkAdaptationSpeed,cl->GetBlendedDarkAdaptationSpeed);
    field("bright_speed",cl->GetBrightAdaptationSpeed,cl->GetBlendedBrightAdaptationSpeed);
    field("target_gray",cl->GetTargetGray,cl->GetBlendedTargetGray);
    field("low_min",cl->GetLowIntensityMinimum,cl->GetBlendedLowIntensityMinimum);
    field("low_max",cl->GetLowIntensityMaximum,cl->GetBlendedLowIntensityMaximum);
    if(cl->GetMinScale && cl->GetBlendedMinScale) field("min_scale",cl->GetMinScale,cl->GetBlendedMinScale);
    else out<<"; min_scale=unavailable";
    if(cl->GetMaxScale && cl->GetBlendedMaxScale) field("max_scale",cl->GetMaxScale,cl->GetBlendedMaxScale);
    else out<<"; max_scale=unavailable";
    if(cl->GetScaleOverride && cl->GetBlendedScaleOverride) field("scale_override",cl->GetScaleOverride,cl->GetBlendedScaleOverride);
    else out<<"; scale_override=unavailable";
    g_ctx.diagnostic=out.str();
   }
  }
 }
 if(api && api->logger && g_ctx.logger) api->logger->Log(g_ctx.logger,SPF_LOG_INFO,g_ctx.diagnostic.c_str());
}
#include "CustomAdaptation.inc"
bool customAdaptationEnabled=true,liveAdaptationPending=false;
void SaveAdaptationEnabled(){if(g_ctx.coreAPI && g_ctx.coreAPI->config && g_ctx.configHandle){auto* cfg=g_ctx.coreAPI->config;cfg->Cfg_SetBool(g_ctx.configHandle,"settings.adaptation.enabled",customAdaptationEnabled);cfg->Cfg_Save(g_ctx.configHandle);}}
void ApplyLiveAdaptation(){
 liveAdaptationPending=false;
 if(!ValidAdaptationEdits()){adaptationError="Use scales 0.0001 to 100 with minimum <= maximum, and speed/exposure multipliers 0.001 to 100.";adaptationApplyStatus="Invalid edit: previous settings remain active.";return;}
 auto* cl=g_ctx.coreAPI?g_ctx.coreAPI->climate:nullptr;
 if(!observedShaderDevice.load() || !cl || !cl->CL_GetActiveSunProfileIndex || cl->CL_GetActiveSunProfileIndex()<0){liveAdaptationPending=true;adaptationApplyStatus="Waiting for vehicle and renderer to apply settings.";return;}
  std::ostringstream attempted;attempted<<"Apply custom adaptation requested: minimum="<<editMinScale<<", maximum="<<editMaxScale<<", darkening="<<editDarkSpeed<<", brightening="<<editBrightSpeed<<", bright-scene multiplier="<<editBrightSceneMultiplier<<", dark-scene multiplier="<<editDarkSceneMultiplier;
  adaptationApplyStatus=attempted.str();
  if(g_ctx.coreAPI && g_ctx.coreAPI->logger && g_ctx.logger)g_ctx.coreAPI->logger->Log(g_ctx.logger,SPF_LOG_INFO,adaptationApplyStatus.c_str());
  if(!ValidAdaptationEdits())adaptationError="Use scales 0.0001 to 100 with minimum <= maximum, and speed/exposure multipliers 0.001 to 100.";
  else {StartCustomAdaptation();
   if(customShaderActive)adaptationError="";
   if(customShaderActive && g_ctx.coreAPI->config && g_ctx.configHandle){auto* cfg=g_ctx.coreAPI->config;
    cfg->Cfg_SetFloat(g_ctx.configHandle,"settings.adaptation.min_scale",editMinScale);cfg->Cfg_SetFloat(g_ctx.configHandle,"settings.adaptation.max_scale",editMaxScale);
    cfg->Cfg_SetFloat(g_ctx.configHandle,"settings.adaptation.dark_speed",editDarkSpeed);cfg->Cfg_SetFloat(g_ctx.configHandle,"settings.adaptation.bright_speed",editBrightSpeed);cfg->Cfg_SetFloat(g_ctx.configHandle,"settings.adaptation.bright_scene_multiplier",editBrightSceneMultiplier);cfg->Cfg_SetFloat(g_ctx.configHandle,"settings.adaptation.dark_scene_multiplier",editDarkSceneMultiplier);cfg->Cfg_Save(g_ctx.configHandle);
   }ReadAdaptation();
  }
  adaptationApplyStatus=adaptationError.empty()?shaderProbeStatus:("Apply failed: "+adaptationError);
  if(g_ctx.coreAPI && g_ctx.coreAPI->logger && g_ctx.logger)g_ctx.coreAPI->logger->Log(g_ctx.logger,SPF_LOG_INFO,adaptationApplyStatus.c_str());

 customAdaptationEnabled=customShaderActive;SaveAdaptationEnabled();
}
void ToggleWindow() {
 if(g_ctx.uiAPI && g_ctx.window) g_ctx.uiAPI->UI_SetVisibility(g_ctx.window,!g_ctx.uiAPI->UI_IsVisible(g_ctx.window));
}
void Render(SPF_UI_API* ui,void*) {
 RenderBrightness(ui);
 ui->UI_TextWrapped(adaptationApplyStatus.c_str());
 if(!adaptationError.empty())ui->UI_TextWrapped(adaptationError.c_str());
 ui->UI_Text("Live adaptation controls");
 if(ui->UI_Checkbox("Enable custom adaptation",&customAdaptationEnabled)){
  if(customAdaptationEnabled)ApplyLiveAdaptation();else {liveAdaptationPending=false;StopShaderProbe();RestoreAdaptationTest();adaptationApplyStatus="Stock adaptation active.";SaveAdaptationEnabled();}
 }
 bool changed=false;
 ui->UI_TextWrapped("Hard exposure limits. Higher speed multipliers adapt faster. Valid edits apply and save immediately. Saved settings apply on vehicle load; Restore returns to stock.");
 changed|=ui->UI_InputDouble("Bright-scene exposure multiplier",&editBrightSceneMultiplier,0,0,"%.4f",SPF_INPUT_TEXT_FLAG_NONE);
 changed|=ui->UI_InputDouble("Dark-scene exposure multiplier",&editDarkSceneMultiplier,0,0,"%.4f",SPF_INPUT_TEXT_FLAG_NONE);
 ui->UI_TextWrapped("Separate multipliers for bright and dark scenes blend across the exposure range before adaptation and hard limits. Below 1 darkens; above 1 brightens. Valid edits take effect immediately.");
 changed|=ui->UI_InputDouble("Minimum exposure scale",&editMinScale,0,0,"%.4f",SPF_INPUT_TEXT_FLAG_NONE);
 changed|=ui->UI_InputDouble("Maximum exposure scale",&editMaxScale,0,0,"%.4f",SPF_INPUT_TEXT_FLAG_NONE);
 changed|=ui->UI_InputDouble("Darkening speed multiplier",&editDarkSpeed,0,0,"%.4f",SPF_INPUT_TEXT_FLAG_NONE);
 changed|=ui->UI_InputDouble("Brightening speed multiplier",&editBrightSpeed,0,0,"%.4f",SPF_INPUT_TEXT_FLAG_NONE);
 if(changed){customAdaptationEnabled=true;ApplyLiveAdaptation();}
 if(ui->UI_Button("Restore original adaptation",0,0)){StopShaderProbe();RestoreAdaptationTest();ReadAdaptation();adaptationError.clear();adaptationApplyStatus="Stock adaptation restored.";customAdaptationEnabled=false;liveAdaptationPending=false;SaveAdaptationEnabled();}
 if(!adaptationError.empty())ui->UI_TextWrapped(adaptationError.c_str());

 ExposurePair pair;{std::lock_guard<std::mutex> lock(exposurePairMutex);pair=exposurePair;}
 std::ostringstream pairText;
 if(customShaderActive && pair.valid)pairText<<std::fixed<<std::setprecision(4)<<"Current exposure: "<<pair.exposure<<"\nCurrent target: "<<pair.target;
 else pairText<<"Current exposure / target: "<<(customShaderActive?"waiting for paired GPU readback":"enable custom adaptation to read the target");
 const auto pairDisplay=pairText.str();ui->UI_TextWrapped(pairDisplay.c_str());
 ExposureSample currentExposure;{std::lock_guard<std::mutex> lock(exposureMutex);currentExposure=exposureSample;}
 std::ostringstream exposureText;exposureText<<"Exposure history readback: ";
 if(currentExposure.valid)exposureText<<std::fixed<<std::setprecision(4)<<currentExposure.exposure;else exposureText<<"waiting for renderer";
 const auto exposureDisplay=exposureText.str();ui->UI_Text(exposureDisplay.c_str());

}
void BuildManifest(SPF_Manifest_Builder_Handle* h,const SPF_Manifest_Builder_API* api) {
 api->Info_SetName(h,PLUGIN_NAME);api->Info_SetVersion(h,"1.0.26");api->Info_SetMinFrameworkVersion(h,"1.2.5");
 api->Info_SetAuthor(h,"SPF Brightness Hotkeys contributors");
 api->Info_SetDescriptionLiteral(h,"Independent brightness adjustment with configurable steps, reset and SPF bindings.");
 api->Policy_SetAllowUserConfig(h,true);api->Policy_AddConfigurableSystem(h,"settings");api->Policy_AddConfigurableSystem(h,"ui");
 api->Policy_AddRequiredHook(h,"GameConsole");
 api->Settings_SetJson(h,R"json({"brightness":{"step":0.1,"reset":0.0,"initialized":false},"options":{"show_notifications":true,"adaptation_diagnostics":true}})json");
 api->Meta_AddCustomSetting(h,"options.adaptation_diagnostics","Read-only adaptation diagnostics","Log lighting configuration every five seconds; never changes adaptation.","checkbox",nullptr,false);
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
 if(api->logger) g_ctx.logger=api->logger->Log_GetContext(PLUGIN_NAME);
 LoadBrightness();
 StartRendererObserver();
 StartShaderObserver();
 StartExposurePairObserver();
 if(api->config && g_ctx.configHandle){auto* cfg=api->config;editMinScale=cfg->Cfg_GetFloat(g_ctx.configHandle,"settings.adaptation.min_scale",0.005);editMaxScale=cfg->Cfg_GetFloat(g_ctx.configHandle,"settings.adaptation.max_scale",8.0);editDarkSpeed=cfg->Cfg_GetFloat(g_ctx.configHandle,"settings.adaptation.dark_speed",0.02);editBrightSpeed=cfg->Cfg_GetFloat(g_ctx.configHandle,"settings.adaptation.bright_speed",2.0);const double priorMultiplier=cfg->Cfg_GetFloat(g_ctx.configHandle,"settings.adaptation.exposure_multiplier",1.0);editBrightSceneMultiplier=cfg->Cfg_GetFloat(g_ctx.configHandle,"settings.adaptation.bright_scene_multiplier",priorMultiplier);editDarkSceneMultiplier=cfg->Cfg_GetFloat(g_ctx.configHandle,"settings.adaptation.dark_scene_multiplier",priorMultiplier); }
 customAdaptationEnabled=api->config && g_ctx.configHandle?api->config->Cfg_GetBool(g_ctx.configHandle,"settings.adaptation.enabled",true):true;liveAdaptationPending=customAdaptationEnabled;
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
 FlushShaderRecords();InstallExecutingDrawObserver();
 if(liveAdaptationPending && customAdaptationEnabled)ApplyLiveAdaptation();
 if(probeOriginal && std::chrono::steady_clock::now()>=shaderProbeEnds)StopShaderProbe();
 if((!grayBackups.empty() || !speedBackups.empty()) && std::chrono::steady_clock::now()>=testEnds) {StopShaderProbe();RestoreAdaptationTest();ReadAdaptation();}
 if(g_ctx.coreAPI && g_ctx.coreAPI->config && g_ctx.configHandle)
  g_ctx.showNotifications=g_ctx.coreAPI->config->Cfg_GetBool(g_ctx.configHandle,"settings.options.show_notifications",true);
 if(g_ctx.coreAPI && g_ctx.coreAPI->config && g_ctx.configHandle &&
    g_ctx.coreAPI->config->Cfg_GetBool(g_ctx.configHandle,"settings.options.adaptation_diagnostics",true)) {
  const auto now=std::chrono::steady_clock::now();
  if(now-g_ctx.lastDiagnostic>=std::chrono::seconds(5)) { g_ctx.lastDiagnostic=now;ReadAdaptation();ReadRendererDiagnostic(true);ExposurePair loggedPair;{std::lock_guard<std::mutex> lock(exposurePairMutex);loggedPair=exposurePair;}if(g_ctx.coreAPI->logger && g_ctx.logger){std::ostringstream paired;paired<<"Paired GPU exposure: valid="<<loggedPair.valid<<" samples="<<loggedPair.samples<<" adapted="<<loggedPair.exposure<<" target="<<loggedPair.target<<" execution_draws="<<pairExecutingDraws.load()<<" draws="<<pairDrawCalls.load()<<" shader_matches="<<pairShaderMatches.load()<<" deferred_contexts="<<pairDeferredContexts.load();const auto message=paired.str();g_ctx.coreAPI->logger->Log(g_ctx.logger,SPF_LOG_INFO,message.c_str());}const auto shaderText=ShaderDiagnostic();if(g_ctx.coreAPI->logger && g_ctx.logger)g_ctx.coreAPI->logger->Log(g_ctx.logger,SPF_LOG_INFO,shaderText.c_str()); }
 }
}
void OnUnload() { StopExecutingDrawObserver();ReleaseExposurePairResources();StopShaderProbe();exposureObserving.store(false);RestoreAdaptationTest();SaveBrightness();g_ctx={}; }
}
extern "C" {
SPF_PLUGIN_EXPORT bool SPF_GetManifestAPI(SPF_Manifest_API* api) { if(!api)return false;api->BuildManifest=SPF_BrightnessHotkeys::BuildManifest;return true; }
SPF_PLUGIN_EXPORT bool SPF_GetPlugin(SPF_Plugin_Exports* api) {
 if(!api)return false;
 api->OnLoad=SPF_BrightnessHotkeys::OnLoad;api->OnActivated=SPF_BrightnessHotkeys::OnActivated;
 api->OnRegisterUI=SPF_BrightnessHotkeys::OnRegisterUI;api->OnUpdate=SPF_BrightnessHotkeys::OnUpdate;api->OnUnload=SPF_BrightnessHotkeys::OnUnload;return true;
}
}

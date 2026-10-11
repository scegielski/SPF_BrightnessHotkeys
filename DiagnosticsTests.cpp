#include "BrightnessPlugin.cpp"
#include <limits>
#include <stdexcept>
#include <source_location>
#include <cstdio>
namespace {
int shaderForwards=0;
bool ForwardShader(uintptr_t d,uint16_t id,uintptr_t object){if(d!=123 || id!=7 || object!=1)throw std::runtime_error("Changed shader arguments");++shaderForwards;return true;}
int forwards=0;
uintptr_t expectedRenderer=0;
uintptr_t ForwardExposure(uintptr_t r){++forwards; if(r!=expectedRenderer)throw std::runtime_error("Changed renderer argument");return 12345;}
int calls=0;
int32_t active=0;
int32_t Active(){return active;}
SPF_Climate_ProfileRef Profile(){return {0,false};}
int32_t Count(bool){return 1;}
float Value(SPF_Climate_ProfileRef){++calls;return 0.25f;}
float Blend(){++calls;return 0.5f;}
float Invalid(){++calls;return std::numeric_limits<float>::quiet_NaN();}
float gray[2]={0.25f,0.3f};
uint64_t Variations(SPF_Climate_ProfileRef){return 2;}
float ReadGray(SPF_Climate_ProfileRef,uint64_t v){return gray[v];}
void WriteGray(SPF_Climate_ProfileRef,uint64_t v,float value){gray[v]=value;}
float minScale[2]={0.6f,0.7f},maxScale[2]={1.2f,1.4f};
float ReadMin(SPF_Climate_ProfileRef,uint64_t v){return minScale[v];}
float ReadMax(SPF_Climate_ProfileRef,uint64_t v){return maxScale[v];}
void WriteMin(SPF_Climate_ProfileRef,uint64_t v,float x){minScale[v]=x;}
void WriteMax(SPF_Climate_ProfileRef,uint64_t v,float x){maxScale[v]=x;}
float dark[2]={0.3f,0.4f},bright[2]={0.7f,0.8f};
float ReadDark(SPF_Climate_ProfileRef,uint64_t v){return dark[v];}
float ReadBright(SPF_Climate_ProfileRef,uint64_t v){return bright[v];}
void WriteDark(SPF_Climate_ProfileRef,uint64_t v,float value){dark[v]=value;}
void WriteBright(SPF_Climate_ProfileRef,uint64_t v,float value){bright[v]=value;}
void Check(bool ok,const std::source_location where=std::source_location::current()){if(!ok){std::fprintf(stderr,"Check failed at %s:%u\n",where.file_name(),where.line());throw std::runtime_error("Diagnostic regression");}}
}
#include "ShaderGpuTests.inc"
int main(){
 using namespace SPF_BrightnessHotkeys;
 void* shaderSlot=reinterpret_cast<void*>(123);const auto slotAddress=reinterpret_cast<uintptr_t>(&shaderSlot);
 Check(!ExchangeShaderSlot(0,nullptr,nullptr));Check(!ExchangeShaderSlot(1,nullptr,nullptr));
 Check(!ExchangeShaderSlot(slotAddress,reinterpret_cast<void*>(456),reinterpret_cast<void*>(789)) && shaderSlot==reinterpret_cast<void*>(123));
 Check(ExchangeShaderSlot(slotAddress,reinterpret_cast<void*>(123),reinterpret_cast<void*>(789)));
 Check(ExchangeShaderSlot(slotAddress,reinterpret_cast<void*>(789),reinterpret_cast<void*>(123)) && shaderSlot==reinterpret_cast<void*>(123));
 Check(sizeof(exposureProbeBytecode)>32 && std::memcmp(exposureProbeBytecode,"DXBC",4)==0);

 // Live edits wait for the renderer; invalid edits never reach installation.
 ApplyLiveAdaptation();Check(liveAdaptationPending && !probeOriginal);
 editDarkSpeed=0;ApplyLiveAdaptation();Check(!liveAdaptationPending && !probeOriginal && !adaptationError.empty());
 editDarkSpeed=0.02;adaptationError.clear();
 // Exercise creation and restoration with a real software DirectX device.
 HMODULE d3dModule=LoadLibraryW(L"d3d11.dll");Check(d3dModule!=nullptr);
 const auto createDevice=reinterpret_cast<decltype(&D3D11CreateDevice)>(GetProcAddress(d3dModule,"D3D11CreateDevice"));Check(createDevice!=nullptr);
 ID3D11Device* testDevice=nullptr;ID3D11DeviceContext* testContext=nullptr;D3D_FEATURE_LEVEL level{};
 Check(SUCCEEDED(createDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&testDevice,&level,&testContext)));
 ID3D11PixelShader* stock=nullptr;Check(SUCCEEDED(testDevice->CreatePixelShader(exposureProbeBytecode,sizeof(exposureProbeBytecode),nullptr,&stock)));
 std::vector<unsigned char> registryDevice(0x363fec0,0),registryEntry(24,0);
 const uintptr_t registryAddress=reinterpret_cast<uintptr_t>(registryEntry.data());const uint64_t registryCount=1;
 const char* stockName="/effect/deferred/_shd/3f615831365abda45f953c77ebf20a44.sm5x.fso";
 std::memcpy(registryDevice.data()+0x363feb0,&registryAddress,sizeof(registryAddress));
 std::memcpy(registryDevice.data()+0x363feb8,&registryCount,sizeof(registryCount));
 std::memcpy(registryDevice.data()+0x1ca2c48,&stockName,sizeof(stockName));std::memcpy(registryEntry.data(),&stock,sizeof(stock));
 observedShaderDevice.store(reinterpret_cast<uintptr_t>(registryDevice.data()));shaderCreationHook=reinterpret_cast<SPF_Hook_Handle*>(1);
 testContext->PSSetShader(stock,nullptr,0);StartShaderProbe();Check(probeOriginal==stock && probeReplacement!=nullptr);
 ID3D11PixelShader* bound=nullptr;testContext->PSGetShader(&bound,nullptr,nullptr);Check(bound==probeReplacement);bound->Release();
 shaderProbeEnds=std::chrono::steady_clock::now();OnUpdate();Check(!probeOriginal && shaderProbeStatus.find("restored")!=std::string::npos);
 ID3D11PixelShader* registryRestored=nullptr;std::memcpy(&registryRestored,registryEntry.data(),sizeof(registryRestored));Check(registryRestored==stock);
 testContext->PSGetShader(&bound,nullptr,nullptr);Check(bound==stock);bound->Release();
 editMinScale=0.05;editMaxScale=4;editDarkSpeed=0.25;editBrightSpeed=0.25;
 const float slowDark=RunAdaptationGpu(testDevice,testContext,4),slowBright=RunAdaptationGpu(testDevice,testContext,0.05f);
 editDarkSpeed=4;editBrightSpeed=4;
 const float fastDark=RunAdaptationGpu(testDevice,testContext,4),fastBright=RunAdaptationGpu(testDevice,testContext,0.05f);
 Check(fastDark<slowDark && fastBright>slowBright && fastDark>=0.05f && fastBright<=4);
 editBrightSceneMultiplier=editDarkSceneMultiplier=0.5;
 const float scaledDark=RunAdaptationGpu(testDevice,testContext,4);
 Check(scaledDark<fastDark);
 editBrightSceneMultiplier=editDarkSceneMultiplier=2;
 Check(RunAdaptationGpu(testDevice,testContext,4)>fastDark);
 editBrightSceneMultiplier=editDarkSceneMultiplier=1;
 editMinScale=0.0001;editMaxScale=100;editDarkSpeed=editBrightSpeed=100;
 const float brightBaseline=RunAdaptationGpu(testDevice,testContext,1,20),darkBaseline=RunAdaptationGpu(testDevice,testContext,1,12);
 editBrightSceneMultiplier=0.5;editDarkSceneMultiplier=0.25;
 const float reducedBright=RunAdaptationGpu(testDevice,testContext,1,20),reducedDark=RunAdaptationGpu(testDevice,testContext,1,12);
 Check(reducedBright<brightBaseline && reducedDark<darkBaseline && reducedDark/darkBaseline<reducedBright/brightBaseline);
 editBrightSceneMultiplier=editDarkSceneMultiplier=1;
 editMinScale=editMaxScale=0.5;
 Check(std::abs(RunAdaptationGpu(testDevice,testContext,100)-0.5f)<0.00001f);
 Check(std::abs(RunAdaptationGpu(testDevice,testContext,0.00001f)-0.5f)<0.00001f);
 editMinScale=0.005;editMaxScale=8;editDarkSpeed=0.02;editBrightSpeed=2;
 testContext->ClearState();stock->Release();testContext->Release();testDevice->Release();FreeLibrary(d3dModule);
 observedShaderDevice.store(0);shaderCreationHook=nullptr;
 originalCreateShader=ForwardShader;Check(!MatchesAdaptationBytecode(0) && !MatchesAdaptationBytecode(1));Check(ObserveCreateShader(123,7,1) && shaderForwards==1 && adaptationShaderMatches.load()==0 && shaderCreationCalls.load()==1);
 std::vector<unsigned char> renderer(0x3ba30,0),resource(0x60,0);
 auto rendererAddress=reinterpret_cast<uintptr_t>(renderer.data());auto resourceAddress=reinterpret_cast<uintptr_t>(resource.data());
 std::memcpy(renderer.data()+0x3ba28,&resourceAddress,sizeof(resourceAddress));
 float exposure=0.42f;std::memcpy(resource.data()+0x58,&exposure,sizeof(exposure));resource[0x38]=1;resource[0x39]=1;
 const auto before=resource;expectedRenderer=rendererAddress;originalExposureUpdate=ForwardExposure;exposureObserving.store(true);
 Check(ObserveExposure(rendererAddress)==12345 && forwards==1);Check(resource==before);
 Check(exposureSample.valid && exposureSample.fixed==1 && exposureSample.skip==1 && exposureSample.exposure==exposure);
 Check(!CaptureExposure(0).valid && !CaptureExposure(1).valid);
 exposureObserving.store(false);Check(ObserveExposure(rendererAddress)==12345 && forwards==2 && exposureSample.calls==1);
 g_ctx={};ReadAdaptation();Check(g_ctx.diagnostic=="Climate getters unavailable.");
 SPF_Core_API core{};SPF_Climate_API cl{};core.climate=&cl;g_ctx.coreAPI=&core;
 cl.CL_GetActiveSunProfileIndex=Active;cl.CL_ActiveProfile=Profile;cl.CL_GetSunProfileCount=Count;
 cl.GetDarkAdaptationSpeed=cl.GetBrightAdaptationSpeed=cl.GetTargetGray=cl.GetLowIntensityMinimum=cl.GetLowIntensityMaximum=Value;
 cl.GetBlendedDarkAdaptationSpeed=cl.GetBlendedBrightAdaptationSpeed=cl.GetBlendedTargetGray=cl.GetBlendedLowIntensityMinimum=cl.GetBlendedLowIntensityMaximum=Blend;
 active=-1;ReadAdaptation();Check(calls==0);
 active=0;ReadAdaptation();Check(calls==10);Check(g_ctx.diagnostic.find("dark_speed=0.25, blended=0.5")!=std::string::npos);
 Check(g_ctx.diagnostic.find("scale_override=unavailable")!=std::string::npos);
 cl.GetMinScale=cl.GetMaxScale=cl.GetScaleOverride=Value;
 cl.GetBlendedMinScale=cl.GetBlendedMaxScale=cl.GetBlendedScaleOverride=Blend;
 calls=0;ReadAdaptation();Check(calls==16 && g_ctx.diagnostic.find("scale_override=0.25, blended=0.5")!=std::string::npos);
 // All setter pointers remain null: any accidental write fails this test.
 cl.GetBlendedTargetGray=Invalid;ReadAdaptation();Check(g_ctx.diagnostic.find("target_gray=0.25, blended=invalid")!=std::string::npos);
 cl.GetTargetGray=nullptr;calls=0;ReadAdaptation();Check(calls==0);
 cl.CL_NextProfile=Profile;cl.GetTargetGrayCount=Variations;cl.GetTargetGrayByIndex=ReadGray;cl.SetTargetGrayByIndex=WriteGray;
 StartAdaptationTest();Check(grayBackups.size()==2 && gray[0]==0.6f && gray[1]==0.6f);
 StartAdaptationTest();Check(grayBackups.size()==2);
 RestoreAdaptationTest();Check(grayBackups.empty() && gray[0]==0.25f && gray[1]==0.3f);
 gray[1]=std::numeric_limits<float>::quiet_NaN();StartAdaptationTest();Check(grayBackups.empty() && gray[0]==0.25f);
 cl.GetDarkAdaptationSpeedCount=cl.GetBrightAdaptationSpeedCount=Variations;
 cl.GetDarkAdaptationSpeedByIndex=ReadDark;cl.GetBrightAdaptationSpeedByIndex=ReadBright;
 cl.SetDarkAdaptationSpeedByIndex=WriteDark;cl.SetBrightAdaptationSpeedByIndex=WriteBright;
 StartSpeedTest(0.05f);Check(speedBackups.size()==4 && dark[0]==0.05f && bright[1]==0.05f && gray[0]==0.25f);
 StartSpeedTest(5.0f);Check(dark[0]==0.05f);
 RestoreAdaptationTest();Check(dark[0]==0.3f && dark[1]==0.4f && bright[0]==0.7f && bright[1]==0.8f);
 StartSpeedTest(5.0f);Check(bright[0]==5.0f);testEnds=std::chrono::steady_clock::now();OnUpdate();Check(speedBackups.empty() && bright[0]==0.7f);
 bright[1]=std::numeric_limits<float>::quiet_NaN();StartSpeedTest(5.0f);Check(speedBackups.empty() && dark[0]==0.3f);
 gray[1]=0.35f;cl.GetScaleOverrideCount=Variations;cl.GetScaleOverrideByIndex=ReadGray;cl.SetScaleOverrideByIndex=WriteGray;
 StartOverrideTest();Check(speedBackups.size()==2 && gray[0]==0 && gray[1]==0 && dark[0]==0.3f);
 StartOverrideTest();Check(speedBackups.size()==2);
 testEnds=std::chrono::steady_clock::now();OnUpdate();Check(speedBackups.empty() && gray[0]==0.25f && gray[1]==0.35f);
 gray[1]=std::numeric_limits<float>::quiet_NaN();StartOverrideTest();Check(speedBackups.empty() && gray[0]==0.25f);
 bright[1]=0.8f;cl.GetMinScaleCount=cl.GetMaxScaleCount=Variations;
 cl.GetMinScaleByIndex=ReadMin;cl.GetMaxScaleByIndex=ReadMax;cl.SetMinScaleByIndex=WriteMin;cl.SetMaxScaleByIndex=WriteMax;
 StartExaggeratedTest();Check(speedBackups.size()==8 && minScale[0]==0.005f && maxScale[1]==8 && dark[0]==0.02f && bright[1]==2.0f);
 StartExaggeratedTest();Check(speedBackups.size()==8);
 testEnds=std::chrono::steady_clock::now();OnUpdate();Check(speedBackups.empty() && minScale[0]==0.6f && maxScale[1]==1.4f && dark[0]==0.3f && bright[1]==0.8f);
 maxScale[1]=std::numeric_limits<float>::quiet_NaN();StartExaggeratedTest();Check(speedBackups.empty() && minScale[0]==0.6f && dark[0]==0.3f);
 maxScale[1]=1.4f;editMinScale=0.01;editMaxScale=4;editDarkSpeed=0.1;editBrightSpeed=1;
 Check(ValidAdaptationEdits());StartExaggeratedTest(true);Check(minScale[0]==0.01f && maxScale[0]==4 && dark[0]==0.1f && bright[0]==1);
 OnUpdate();Check(speedBackups.size()==8 && testEnds==std::chrono::steady_clock::time_point::max());
 RestoreAdaptationTest();Check(minScale[0]==0.6f && maxScale[0]==1.2f && dark[0]==0.3f && bright[0]==0.7f);
 editMinScale=5;Check(!ValidAdaptationEdits());editMinScale=0.01;editDarkSpeed=std::numeric_limits<double>::quiet_NaN();Check(!ValidAdaptationEdits());
 g_ctx={};
}

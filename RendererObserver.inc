// Read-only, build-gated observer of the native exposure readback path.
using ExposureUpdate = uintptr_t (*)(uintptr_t);
ExposureUpdate originalExposureUpdate=nullptr;
SPF_Hook_Handle* exposureHook=nullptr;
uintptr_t freezeObject=0;
using ReadFreeze = int (*)(uintptr_t);
ReadFreeze readFreeze=nullptr;
struct ExposureSample { uint64_t calls=0; bool valid=false; int freeze=-1; uint8_t fixed=0,skip=0; float exposure=-1; bool pending=false; };
std::mutex exposureMutex;
ExposureSample exposureSample;
std::atomic<bool> exposureObserving{false};
bool ExposureRead(uintptr_t address,void* out,size_t size) noexcept {
 if(!address || !out || size>4096)return false;
 __try {std::memcpy(out,reinterpret_cast<void*>(address),size);return true;}
 __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
template<class T> bool ExposureReadValue(uintptr_t a,T& v) noexcept {return ExposureRead(a,&v,sizeof(v));}
int ReadFreezeSafely() noexcept {
 if(!readFreeze || !freezeObject)return -1;
 __try {return readFreeze(freezeObject);}
 __except(EXCEPTION_EXECUTE_HANDLER){return -1;}
}
ExposureSample CaptureExposure(uintptr_t renderer) noexcept {
 ExposureSample s;uintptr_t resource=0,pending=0;
 if(!renderer || !ExposureReadValue(renderer+0x3ba28,resource) || !resource)return s;
 if(!ExposureReadValue(resource+0x38,s.fixed) || !ExposureReadValue(resource+0x39,s.skip) ||
    !ExposureReadValue(resource+0x50,pending) || !ExposureReadValue(resource+0x58,s.exposure))return s;
 s.valid=std::isfinite(s.exposure);s.pending=pending!=0;s.freeze=ReadFreezeSafely();return s;
}
uintptr_t ObserveExposure(uintptr_t renderer) {
 // Forward exactly once, preserving the return register, before reading the result.
 const auto result=originalExposureUpdate(renderer);
 if(exposureObserving.load(std::memory_order_relaxed)) {
  auto s=CaptureExposure(renderer);
  std::lock_guard<std::mutex> lock(exposureMutex);s.calls=exposureSample.calls+1;exposureSample=s;
 }
 return result;
}
std::string rendererStatus="Renderer observer not initialized.";
void StartRendererObserver() {
 auto* h=g_ctx.coreAPI?g_ctx.coreAPI->hooks:nullptr;
 const auto base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
 const char* pattern="40 55 53 56 57 48 8D 6C 24 C1 48 81 EC 98 00 00 00 48 8B B1 28 BA 03 00 48 8B F9 48 8B CE E8 FD 57 00 00";
 const char* freezePattern="48 8D 0D B6 55 98 01 49 89 9D 30 BA 03 00 E8 2A 05 D5 FE 85";
 if(!h || !h->Hook_Register || !h->Hook_FindPattern || !base ||
    h->Hook_FindPattern(pattern)!=base+0x1487700 || h->Hook_FindPattern(freezePattern)!=base+0x147cbc3) {
  rendererStatus="Renderer observer unavailable: executable signatures do not match the traced ATS build.";return;
 }
 const uint8_t getterBytes[]={0x40,0x57,0x48,0x83,0xec,0x20,0x48,0x83,0xb9,0x20,0x01,0x00,0x00,0x00,0x48,0x8b,0xf9,0x74,0x39};
 uint8_t actual[sizeof(getterBytes)]{};
 if(!ExposureRead(base+0x1cd100,actual,sizeof(actual)) || std::memcmp(actual,getterBytes,sizeof(actual))) {
  rendererStatus="Renderer observer unavailable: freeze getter signature mismatch.";return;
 }
 readFreeze=reinterpret_cast<ReadFreeze>(base+0x1cd100);freezeObject=base+0x2e02180;
 exposureHook=h->Hook_Register(PLUGIN_NAME,"ExposureReadbackObserver","Read-only renderer exposure observer",reinterpret_cast<void*>(ObserveExposure),reinterpret_cast<void**>(&originalExposureUpdate),pattern,true);
 rendererStatus=exposureHook?"Renderer observer registered; waiting for exposure calls.":"Renderer observer registration failed.";
 exposureObserving.store(exposureHook!=nullptr);
}
void ReadRendererDiagnostic(bool log) {
 ExposureSample s;{std::lock_guard<std::mutex> lock(exposureMutex);s=exposureSample;}
 std::ostringstream out;out<<rendererStatus;
 if(s.calls)out<<" calls="<<s.calls<<" valid="<<s.valid<<" freeze="<<s.freeze<<" fixed="<<unsigned(s.fixed)<<" skip_readback="<<unsigned(s.skip)<<" pending_readback="<<s.pending<<" exposure_history_readback="<<s.exposure;
 const auto value=out.str();
 if(log && g_ctx.coreAPI && g_ctx.coreAPI->logger && g_ctx.logger)g_ctx.coreAPI->logger->Log(g_ctx.logger,SPF_LOG_INFO,value.c_str());
 if(!log && g_ctx.uiAPI){g_ctx.uiAPI->UI_TextWrapped(value.c_str());g_ctx.uiAPI->UI_TextWrapped("GPU exposure-history readback (previous completed GPU result). Separate histogram target: unavailable. Final headset exposure: not yet independently verified. These are not two measured target/final values.");}
}

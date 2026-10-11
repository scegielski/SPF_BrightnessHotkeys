// Read the custom shader's adapted exposure (x) and adjusted target (w).
// Copy only while that exact shader is bound; never infer target from climate.
struct ExposurePair {bool valid=false;float exposure=0,target=0;uint64_t samples=0;};
std::mutex exposurePairMutex;
ExposurePair exposurePair;
std::atomic<bool> exposurePairObserving{false};
using NativeAdaptationPass=void (*)(uintptr_t);
NativeAdaptationPass originalAdaptationPass=nullptr;
SPF_Hook_Handle* exposurePairHook=nullptr;
ID3D11Texture2D* exposurePairStaging=nullptr;
bool exposurePairPending=false;
float HalfExposure(uint16_t value) {
 const int exponent=(value>>10)&31;const float sign=(value&0x8000)?-1.0f:1.0f;
 if(exponent==31)return std::numeric_limits<float>::quiet_NaN();
 return sign*(exponent?std::ldexp(1.0f+(value&1023)/1024.0f,exponent-15):std::ldexp(static_cast<float>(value&1023),-24));
}
void ReleaseExposurePairResources(){if(exposurePairStaging)exposurePairStaging->Release();exposurePairStaging=nullptr;exposurePairPending=false;}
std::atomic<uint64_t> pairDrawCalls{0},pairShaderMatches{0},pairDeferredContexts{0};
void CaptureExposurePair(ID3D11DeviceContext* supplied=nullptr) {
 if(!customShaderActive || !probeReplacement)return;
 ID3D11Device* device=nullptr;probeReplacement->GetDevice(&device);if(!device)return;
 ID3D11DeviceContext* context=nullptr;if(supplied){context=supplied;context->AddRef();}else device->GetImmediateContext(&context);
 if(!context){device->Release();return;}
 if(context->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE){++pairDeferredContexts;context->Release();device->Release();return;}
 ID3D11PixelShader* current=nullptr;context->PSGetShader(&current,nullptr,nullptr);
 const bool matches=current==probeReplacement;if(current)current->Release();
 if(!matches){context->Release();device->Release();return;}
 ++pairShaderMatches;
 ID3D11RenderTargetView* view=nullptr;context->OMGetRenderTargets(1,&view,nullptr);
 ID3D11Resource* resource=nullptr;if(view){view->GetResource(&resource);view->Release();}
 ID3D11Texture2D* texture=nullptr;if(resource){resource->QueryInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&texture));resource->Release();}
 if(!texture){context->Release();device->Release();return;}
 D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);
 const bool float32=desc.Format==DXGI_FORMAT_R32G32B32A32_FLOAT;
 const bool float16=desc.Format==DXGI_FORMAT_R16G16B16A16_FLOAT;
 if(desc.Width!=1 || desc.Height!=1 || desc.ArraySize!=1 || desc.MipLevels!=1 || desc.SampleDesc.Count!=1 || (!float32 && !float16)){texture->Release();context->Release();device->Release();return;}
 if(exposurePairStaging){D3D11_TEXTURE2D_DESC stagingDesc{};exposurePairStaging->GetDesc(&stagingDesc);if(stagingDesc.Format!=desc.Format)ReleaseExposurePairResources();}
 if(!exposurePairStaging){desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.MiscFlags=0;desc.MipLevels=desc.ArraySize=1;device->CreateTexture2D(&desc,nullptr,&exposurePairStaging);}
 if(exposurePairStaging && exposurePairPending){
  D3D11_MAPPED_SUBRESOURCE mapped{};
  if(SUCCEEDED(context->Map(exposurePairStaging,0,D3D11_MAP_READ,D3D11_MAP_FLAG_DO_NOT_WAIT,&mapped))){
   const auto* full=static_cast<const float*>(mapped.pData);const auto* half=static_cast<const uint16_t*>(mapped.pData);
   const float exposure=float32?full[0]:HalfExposure(half[0]);const float target=float32?full[3]:HalfExposure(half[3]);
   context->Unmap(exposurePairStaging,0);exposurePairPending=false;
   if(std::isfinite(exposure) && exposure>0 && std::isfinite(target) && target>0){std::lock_guard<std::mutex> lock(exposurePairMutex);exposurePair={true,exposure,target,exposurePair.samples+1};}
  }
 }
 if(exposurePairStaging && !exposurePairPending){context->CopyResource(exposurePairStaging,texture);exposurePairPending=true;}
 texture->Release();context->Release();device->Release();
}
// Observe execution of Draw, including commands submitted by the game's queue.
using NativeD3DDraw=void (STDMETHODCALLTYPE *)(ID3D11DeviceContext*,UINT,UINT);
NativeD3DDraw originalPairDraw=nullptr;
void** pairDrawSlot=nullptr;
std::atomic<uint64_t> pairExecutingDraws{0};
void STDMETHODCALLTYPE ObserveExecutingDraw(ID3D11DeviceContext* context,UINT count,UINT start){
 originalPairDraw(context,count,start);
 if(exposurePairObserving.load() && customShaderActive && count==3){++pairExecutingDraws;CaptureExposurePair(context);}
}
bool ExchangeDrawEntry(void** slot,void* expected,void* replacement){
 DWORD oldProtection=0;if(!VirtualProtect(slot,sizeof(void*),PAGE_READWRITE,&oldProtection))return false;
 const bool changed=InterlockedCompareExchangePointer(slot,replacement,expected)==expected;
 DWORD unused=0;VirtualProtect(slot,sizeof(void*),oldProtection,&unused);return changed;
}
void InstallExecutingDrawObserver(){
 if(pairDrawSlot || !probeReplacement || !exposurePairObserving.load())return;
 ID3D11Device* device=nullptr;probeReplacement->GetDevice(&device);if(!device)return;
 ID3D11DeviceContext* context=nullptr;device->GetImmediateContext(&context);device->Release();if(!context)return;
 auto** table=*reinterpret_cast<void***>(context);auto** slot=table+13;
 auto* previous=*slot;originalPairDraw=reinterpret_cast<NativeD3DDraw>(previous);
 if(ExchangeDrawEntry(slot,previous,reinterpret_cast<void*>(ObserveExecutingDraw)))pairDrawSlot=slot;
 context->Release();
}
void StopExecutingDrawObserver(){
 exposurePairObserving.store(false);
 if(pairDrawSlot)ExchangeDrawEntry(pairDrawSlot,reinterpret_cast<void*>(ObserveExecutingDraw),reinterpret_cast<void*>(originalPairDraw));
 pairDrawSlot=nullptr;
}
void StartExposurePairObserver(){exposurePairObserving.store(true);}

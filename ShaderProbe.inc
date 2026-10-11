// Temporary replacement of the identified registry object, never game files.
bool customShaderActive=false;
ID3D11PixelShader* probeOriginal=nullptr;
ID3D11PixelShader* probeReplacement=nullptr;
uintptr_t probeSlot=0;
std::chrono::steady_clock::time_point shaderProbeEnds{};
std::string shaderProbeStatus="Shader replacement probe inactive. Test fixes exposure at 0.5 for 30 seconds.";
bool ExchangeShaderSlot(uintptr_t slot,void* expected,void* replacement) noexcept {
 if(!slot || slot%sizeof(void*))return false;
 __try {return InterlockedCompareExchangePointer(reinterpret_cast<void* volatile*>(slot),replacement,expected)==expected;}
 __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
void ProbeLog() {
 if(g_ctx.coreAPI && g_ctx.coreAPI->logger && g_ctx.logger)g_ctx.coreAPI->logger->Log(g_ctx.logger,SPF_LOG_INFO,shaderProbeStatus.c_str());
}
void UpdateBoundShader(ID3D11PixelShader* from,ID3D11PixelShader* to) {
 ID3D11Device* device=nullptr;from->GetDevice(&device);if(!device)return;
 ID3D11DeviceContext* context=nullptr;device->GetImmediateContext(&context);device->Release();if(!context)return;
 ID3D11PixelShader* current=nullptr;ID3D11ClassInstance* instances[256]{};UINT count=256;
 context->PSGetShader(&current,instances,&count);
 if(current==from)context->PSSetShader(to,instances,count);
 if(current)current->Release();for(UINT i=0;i<count;++i)if(instances[i])instances[i]->Release();context->Release();
}
void StopShaderProbe() {
 if(!probeOriginal)return;
 const bool restored=ExchangeShaderSlot(probeSlot,probeReplacement,probeOriginal);
 UpdateBoundShader(probeReplacement,probeOriginal);
 if(restored)probeReplacement->Release(); // Remove temporary registry ownership.
 else probeOriginal->Release(); // Reserved original registry reference no longer has a slot.
 probeOriginal->Release();probeReplacement->Release();
 probeOriginal=nullptr;probeReplacement=nullptr;probeSlot=0;
 shaderProbeStatus=restored?"Shader replacement probe restored the stock shader.":"Shader replacement probe ended; registry changed, so no registry entry was overwritten.";
 if(customShaderActive){customShaderActive=false;RestoreAdaptationTest();}
 ProbeLog();
}
bool InstallShaderReplacement(const void* bytecode,size_t bytecodeSize,const char* description,bool timed) {
 if(probeOriginal || !shaderCreationHook)return false;
 const auto deviceObject=observedShaderDevice.load();uint64_t count=0;uintptr_t entries=0,slot=0;ID3D11PixelShader* original=nullptr;
 if(deviceObject && ExposureReadValue(deviceObject+0x363feb8,count) && count<=65536 && ExposureReadValue(deviceObject+0x363feb0,entries) && entries){
  for(uint32_t id=0;id<count && id<4096;++id){
   auto record=CaptureShaderRecord(deviceObject,static_cast<uint16_t>(id),0);
   if(std::strcmp(record.name,"/effect/deferred/_shd/3f615831365abda45f953c77ebf20a44.sm5x.fso"))continue;
   slot=entries+static_cast<uintptr_t>(id)*24;
   if(!ExposureReadValue(slot,original))original=nullptr;break;
  }
 }
 if(!original){shaderProbeStatus="Shader replacement unavailable: stock adaptation object not found.";ProbeLog();return false;}
 ID3D11Device* device=nullptr;original->GetDevice(&device);
 ID3D11PixelShader* replacement=nullptr;
 const HRESULT result=device?device->CreatePixelShader(bytecode,bytecodeSize,nullptr,&replacement):E_FAIL;
 if(device)device->Release();
 if(FAILED(result) || !replacement){shaderProbeStatus="Shader replacement failed: DirectX rejected the probe bytecode.";ProbeLog();return false;}
 original->AddRef();replacement->AddRef();
 if(!ExchangeShaderSlot(slot,original,replacement)){original->Release();replacement->Release();replacement->Release();shaderProbeStatus="Shader replacement cancelled: registry entry changed.";ProbeLog();return false;}
 probeOriginal=original;probeReplacement=replacement;probeSlot=slot;
 UpdateBoundShader(original,replacement);
 shaderProbeEnds=timed?std::chrono::steady_clock::now()+std::chrono::seconds(30):std::chrono::steady_clock::time_point::max();
 shaderProbeStatus=description;ProbeLog();return true;
}

void StartShaderProbe(){InstallShaderReplacement(exposureProbeBytecode,sizeof(exposureProbeBytecode),"Shader replacement active: fixed exposure 0.5; stock shader restores after 30 seconds.",true);}

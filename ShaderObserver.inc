using NativeCreateShader=bool (*)(uintptr_t,uint16_t,uintptr_t);
NativeCreateShader originalCreateShader=nullptr;
SPF_Hook_Handle* shaderCreationHook=nullptr;
std::atomic<uint64_t> shaderCreationCalls{0},adaptationShaderMatches{0},adaptationShaderSuccesses{0};
std::atomic<uintptr_t> observedShaderDevice{0};
bool shaderRegistryScanned=false;
std::string shaderRegistryStatus="Existing shader registry not scanned.";
bool MatchesAdaptationBytecode(uintptr_t object) noexcept {
 uint32_t header[7]{};
 if(!ExposureRead(object,header,sizeof(header)) || header[5]>4096 || header[5]<28 || header[6]!=11352)return false;
 const auto data=object+header[5];if(data<object)return false;
 uint64_t hash=14695981039346656037ULL;uint8_t block[256];
 for(size_t offset=0;offset<header[6];offset+=sizeof(block)) {
  const size_t count=(header[6]-offset)<sizeof(block)?header[6]-offset:sizeof(block);
  if(!ExposureRead(data+offset,block,count))return false;
  for(size_t i=0;i<count;++i)hash=(hash^block[i])*1099511628211ULL;
 }
 return hash==0xb69cd6401b4bcc70ULL;
}
struct ShaderCreationRecord {uint16_t id=0;uint32_t header[7]{};uint32_t payloadMagic=0;bool valid=false,success=false;char name[128]{};};
std::mutex shaderRecordsMutex;
std::vector<ShaderCreationRecord> shaderRecords;
ShaderCreationRecord CaptureShaderRecord(uintptr_t device,uint16_t id,uintptr_t object) noexcept {
 ShaderCreationRecord r;r.id=id;r.valid=ExposureRead(object,r.header,sizeof(r.header));
 if(r.valid && r.header[5]>=28 && r.header[5]<=4096)ExposureReadValue(object+r.header[5],r.payloadMagic);
 uintptr_t name=0;
 if(id!=0xffff && device && ExposureReadValue(device+0x1ca2c48+static_cast<uintptr_t>(id)*0x198,name) && name){
  for(size_t i=0;i<sizeof(r.name)-1;++i){if(!ExposureRead(name+i,&r.name[i],1) || !r.name[i])break;if(static_cast<unsigned char>(r.name[i])<32 || static_cast<unsigned char>(r.name[i])>126){r.name[i]=0;break;}}
 }
 return r;
}
void ScanExistingShaders() {
 if(shaderRegistryScanned || !g_ctx.coreAPI || !g_ctx.coreAPI->logger || !g_ctx.logger)return;
 const auto device=observedShaderDevice.load();if(!device)return;
 uint64_t count=0;uintptr_t entries=0;
 if(!ExposureReadValue(device+0x363feb8,count) || !ExposureReadValue(device+0x363feb0,entries) || count==0 || count>65536 || !entries)return;
 shaderRegistryScanned=true;unsigned matches=0;
 for(uint32_t id=0;id<count && id<4096;++id){
  auto record=CaptureShaderRecord(device,static_cast<uint16_t>(id),0);
  if(!std::strstr(record.name,"3f615831365abda45f953c77ebf20a44") && !std::strstr(record.name,"b02df8a2b53044b82bb8cf4494532eaa"))continue;
  uintptr_t shader=0;const bool readable=ExposureReadValue(entries+static_cast<uintptr_t>(id)*24,shader);++matches;
  std::ostringstream out;out<<"Existing adaptation shader: id="<<id<<" name="<<record.name<<" object_readable="<<readable<<" object_present="<<(readable && shader!=0);
  const auto message=out.str();g_ctx.coreAPI->logger->Log(g_ctx.logger,SPF_LOG_INFO,message.c_str());
 }
 std::ostringstream out;out<<"Existing shader registry: entries="<<count<<" adaptation_names="<<matches;shaderRegistryStatus=out.str();
 g_ctx.coreAPI->logger->Log(g_ctx.logger,SPF_LOG_INFO,shaderRegistryStatus.c_str());
}
void FlushShaderRecords() {
 ScanExistingShaders();
 std::vector<ShaderCreationRecord> pending;{std::lock_guard<std::mutex> lock(shaderRecordsMutex);pending.swap(shaderRecords);}
 if(!g_ctx.coreAPI || !g_ctx.coreAPI->logger || !g_ctx.logger)return;
 for(const auto& r:pending){std::ostringstream out;out<<"Shader creation record: id="<<r.id<<" name="<<r.name<<" success="<<r.success<<" valid_header="<<r.valid<<" header=";
  for(auto v:r.header)out<<std::hex<<v<<',';out<<" payload_magic="<<r.payloadMagic;
  const auto message=out.str();g_ctx.coreAPI->logger->Log(g_ctx.logger,SPF_LOG_INFO,message.c_str());
 }
}
bool ObserveCreateShader(uintptr_t device,uint16_t id,uintptr_t object) {
 if(device)observedShaderDevice.store(device);
 const auto sequence=shaderCreationCalls.fetch_add(1,std::memory_order_relaxed);
 ShaderCreationRecord record;if(sequence<1024)record=CaptureShaderRecord(device,id,object);
 const bool match=MatchesAdaptationBytecode(object);
 const bool result=originalCreateShader(device,id,object);
 if(sequence<1024){record.success=result;std::lock_guard<std::mutex> lock(shaderRecordsMutex);shaderRecords.push_back(record);}
 if(match){adaptationShaderMatches.fetch_add(1,std::memory_order_relaxed);if(result)adaptationShaderSuccesses.fetch_add(1,std::memory_order_relaxed);}
 return result;
}
std::string shaderObserverStatus="Shader creation observer not initialized.";
void StartShaderObserver() {
 observedShaderDevice.store(0);shaderRegistryScanned=false;shaderRegistryStatus="Existing shader registry not scanned.";
 auto* h=g_ctx.coreAPI?g_ctx.coreAPI->hooks:nullptr;
 const auto base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
 const char* pattern="40 56 41 57 48 83 EC 48 41 BA FF FF 00 00 48 8B F1 41 B9 FF FF FF FF 66 41 3B D2 75 05 41";
 if(!h || !h->Hook_FindPattern || !h->Hook_Register || h->Hook_FindPattern(pattern)!=base+0x299050){shaderObserverStatus="Shader creation observer unavailable: signature mismatch.";return;}
 shaderCreationHook=h->Hook_Register(PLUGIN_NAME,"AdaptationShaderCreationObserver","Read-only adaptation shader creation observer",reinterpret_cast<void*>(ObserveCreateShader),reinterpret_cast<void**>(&originalCreateShader),pattern,true);
 shaderObserverStatus=shaderCreationHook?"Shader creation observer registered (unchanged bytecode).":"Shader creation observer registration failed.";
}
std::string ShaderDiagnostic() {
 std::ostringstream out;out<<shaderRegistryStatus<<"; "<<shaderObserverStatus<<" creation_calls="<<shaderCreationCalls.load()<<" adaptation_matches="<<adaptationShaderMatches.load()<<" successful_matches="<<adaptationShaderSuccesses.load();return out.str();
}

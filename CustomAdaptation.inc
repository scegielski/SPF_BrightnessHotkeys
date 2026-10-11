bool CompileCustomAdaptation(ID3DBlob** bytecode) {
 *bytecode=nullptr;
 static const HMODULE module=LoadLibraryW(L"d3dcompiler_47.dll");
 if(!module){adaptationError="Shader compiler unavailable.";return false;}
 const auto compile=reinterpret_cast<decltype(&D3DCompile)>(GetProcAddress(module,"D3DCompile"));
 if(!compile){adaptationError="Shader compiler entry point unavailable.";return false;}
 auto number=[](double value){std::ostringstream out;out.imbue(std::locale::classic());out<<std::setprecision(17)<<value;return out.str();};
 const auto minimum=number(editMinScale),maximum=number(editMaxScale),dark=number(editDarkSpeed),bright=number(editBrightSpeed),brightScene=number(editBrightSceneMultiplier),darkScene=number(editDarkSceneMultiplier);
 const D3D_SHADER_MACRO macros[]={{"MIN_SCALE",minimum.c_str()},{"MAX_SCALE",maximum.c_str()},{"DARK_SPEED",dark.c_str()},{"BRIGHT_SPEED",bright.c_str()},{"BRIGHT_SCENE_MULTIPLIER",brightScene.c_str()},{"DARK_SCENE_MULTIPLIER",darkScene.c_str()},{nullptr,nullptr}};
 ID3DBlob* errors=nullptr;
 const HRESULT result=compile(customAdaptationSource,sizeof(customAdaptationSource)-1,"CustomAdaptation.hlsl",macros,nullptr,"main","ps_5_0",D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,bytecode,&errors);
 if(errors)errors->Release();
 if(FAILED(result)){if(*bytecode){(*bytecode)->Release();*bytecode=nullptr;}adaptationError="Custom adaptation shader failed to compile.";return false;}return true;
}
void StartCustomAdaptation() {
 if(!ValidAdaptationEdits()){adaptationError="Invalid custom adaptation limits or speed multipliers.";return;}
 ID3DBlob* bytecode=nullptr;if(!CompileCustomAdaptation(&bytecode))return;
 StopShaderProbe();RestoreAdaptationTest();ReleaseExposurePairResources();
 {std::lock_guard<std::mutex> lock(exposurePairMutex);exposurePair={};}
 StartSpeedTest(0.5f);
 if(speedBackups.empty()){bytecode->Release();adaptationError="Unable to capture native speeds: load a vehicle and verify climate APIs.";return;}
 std::ostringstream description;description<<"Custom adaptation shader active: hard limits "<<editMinScale<<" to "<<editMaxScale<<", darkening multiplier "<<editDarkSpeed<<", brightening multiplier "<<editBrightSpeed<<", bright-scene multiplier "<<editBrightSceneMultiplier<<", dark-scene multiplier "<<editDarkSceneMultiplier<<". Restore originals to return to stock.";
 const auto descriptionText=description.str();
 const bool installed=InstallShaderReplacement(bytecode->GetBufferPointer(),bytecode->GetBufferSize(),descriptionText.c_str(),false);
 bytecode->Release();
 if(!installed){RestoreAdaptationTest();adaptationError=shaderProbeStatus;return;}
 customShaderActive=true;testEnds=std::chrono::steady_clock::time_point::max();activeTest="Custom adaptation shader";adaptationError.clear();
}

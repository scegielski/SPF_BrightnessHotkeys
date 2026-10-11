// Run the actual compiled adaptation shader against controlled inputs on WARP.
float RunAdaptationGpu(ID3D11Device* device,ID3D11DeviceContext* context,float previous,int histogramBin=16) {
 using namespace SPF_BrightnessHotkeys;
 ID3DBlob* code=nullptr;Check(CompileCustomAdaptation(&code));ID3D11PixelShader* pixel=nullptr;
 Check(SUCCEEDED(device->CreatePixelShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&pixel)));code->Release();
 HMODULE compiler=LoadLibraryW(L"d3dcompiler_47.dll");Check(compiler!=nullptr);
 auto compile=reinterpret_cast<decltype(&D3DCompile)>(GetProcAddress(compiler,"D3DCompile"));
 const char* vertexSource="float4 main(uint i:SV_VertexID):SV_Position{return float4(i==2?3:-1,i==1?3:-1,0,1);}";
 Check(SUCCEEDED(compile(vertexSource,std::strlen(vertexSource),nullptr,nullptr,nullptr,"main","vs_5_0",0,0,&code,nullptr)));
 ID3D11VertexShader* vertex=nullptr;Check(SUCCEEDED(device->CreateVertexShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&vertex)));code->Release();FreeLibrary(compiler);
 float histogram[32]{};histogram[histogramBin]=100;float history[4]={previous,0,0,0};
 D3D11_TEXTURE2D_DESC desc{};desc.Width=4;desc.Height=2;desc.MipLevels=desc.ArraySize=1;desc.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
 D3D11_SUBRESOURCE_DATA data{histogram,64,0};ID3D11Texture2D* histogramTexture=nullptr;Check(SUCCEEDED(device->CreateTexture2D(&desc,&data,&histogramTexture)));
 desc.Width=desc.Height=1;data.pSysMem=history;data.SysMemPitch=16;ID3D11Texture2D* historyTexture=nullptr;Check(SUCCEEDED(device->CreateTexture2D(&desc,&data,&historyTexture)));
 ID3D11ShaderResourceView* views[2]{};Check(SUCCEEDED(device->CreateShaderResourceView(histogramTexture,nullptr,&views[0])));Check(SUCCEEDED(device->CreateShaderResourceView(historyTexture,nullptr,&views[1])));
 desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_RENDER_TARGET;ID3D11Texture2D* output=nullptr;Check(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&output)));
 ID3D11RenderTargetView* target=nullptr;Check(SUCCEEDED(device->CreateRenderTargetView(output,nullptr,&target)));
 desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ID3D11Texture2D* staging=nullptr;Check(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&staging)));
 float constants[8]={0,0,0,0,0.99f,0.99f,0,0};D3D11_BUFFER_DESC bufferDesc{};bufferDesc.ByteWidth=32;bufferDesc.Usage=D3D11_USAGE_IMMUTABLE;bufferDesc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
 data.pSysMem=constants;data.SysMemPitch=0;ID3D11Buffer* buffer=nullptr;Check(SUCCEEDED(device->CreateBuffer(&bufferDesc,&data,&buffer)));
 D3D11_RASTERIZER_DESC rasterDesc{};rasterDesc.FillMode=D3D11_FILL_SOLID;rasterDesc.CullMode=D3D11_CULL_NONE;rasterDesc.DepthClipEnable=TRUE;ID3D11RasterizerState* raster=nullptr;Check(SUCCEEDED(device->CreateRasterizerState(&rasterDesc,&raster)));
 D3D11_VIEWPORT viewport{0,0,1,1,0,1};context->RSSetState(raster);context->RSSetViewports(1,&viewport);context->IASetInputLayout(nullptr);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
 context->VSSetShader(vertex,nullptr,0);context->PSSetShader(pixel,nullptr,0);context->PSSetShaderResources(0,2,views);context->PSSetConstantBuffers(0,1,&buffer);context->OMSetRenderTargets(1,&target,nullptr);probeReplacement=pixel;customShaderActive=true;exposurePairObserving.store(true);InstallExecutingDrawObserver();Check(pairDrawSlot!=nullptr);const auto beforeDraw=pairExecutingDraws.load();context->Draw(3,0);Check(pairExecutingDraws.load()==beforeDraw+1);
 Check(exposurePairPending);
 context->OMSetRenderTargets(0,nullptr,nullptr);context->CopyResource(staging,output);
 D3D11_MAPPED_SUBRESOURCE mapped{};Check(SUCCEEDED(context->Map(staging,0,D3D11_MAP_READ,0,&mapped)));const float result=*static_cast<float*>(mapped.pData);context->Unmap(staging,0);
 context->OMSetRenderTargets(1,&target,nullptr);CaptureExposurePair();Check(exposurePair.valid && std::abs(exposurePair.exposure-result)<0.00001f && exposurePair.target>=editMinScale-0.000001 && exposurePair.target<=editMaxScale+0.000001);
 auto** restoredSlot=pairDrawSlot;auto* expectedDraw=reinterpret_cast<void*>(originalPairDraw);StopExecutingDrawObserver();Check(*restoredSlot==expectedDraw);customShaderActive=false;probeReplacement=nullptr;ReleaseExposurePairResources();context->ClearState();
 raster->Release();buffer->Release();staging->Release();target->Release();output->Release();views[0]->Release();views[1]->Release();histogramTexture->Release();historyTexture->Release();vertex->Release();pixel->Release();return result;
}

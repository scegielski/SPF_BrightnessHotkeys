// Input/output contract matches the native adaptation pass. Histogram layout:
// 32 luminance bins, four channels per texel in a 4-by-2 texture.
Texture2D<float4> histogram : register(t0);
Texture2D<float4> previousExposure : register(t1);
cbuffer NativeAdaptation : register(b0) { float4 nativeLimits; float4 historyWeights; };
float4 main() : SV_Target {
 float bins[32];float total=0;int first=31,last=0;
 [unroll] for(int i=0;i<32;++i){
  float4 texel=histogram.Load(int3((i/4)%4,i/16,0));
  bins[i]=texel[i%4];total+=bins[i];
  if(bins[i]>0){first=min(first,i);last=max(last,i);}
 }
 float lower=total*0.02,upper=total*0.98;
 float running=0,prior=lower,sum=0,samples=0;
 [unroll] for(int j=0;j<32;++j){
  running+=bins[j];float trimmed=clamp(running,lower,upper);
  float n=max(trimmed-prior,0);prior=trimmed;
  sum+=n*exp2(float(j)-31.5)*65504.0;samples+=n;
 }
 float luminance=sum/max(samples,1);
 float binIndex=max(31.99+log2(max(saturate(luminance/65504.0),exp2(-32.0))),0);
 float quantizedLuminance=exp2(binIndex-32.0)*65504.0;
 float rawTarget=1/max(quantizedLuminance,0.0001);
 float rangeStops=log2(MAX_SCALE)-log2(MIN_SCALE);
 float darkness=rangeStops>0.000001?saturate((log2(rawTarget)-log2(MIN_SCALE))/rangeStops):0.5;
 darkness=darkness*darkness*(3-2*darkness);
 float sceneMultiplier=lerp(BRIGHT_SCENE_MULTIPLIER,DARK_SCENE_MULTIPLIER,darkness);
 float target=clamp(rawTarget*sceneMultiplier,MIN_SCALE,MAX_SCALE);
 float previous=previousExposure.Load(int3(0,0,0)).x;
 previous=previous>0?clamp(previous,MIN_SCALE,MAX_SCALE):target;
 float targetStops=log2(target),previousStops=log2(previous);
 // The plugin holds both native speeds at 0.5, preserving the game's own
 // frame-time compensation. Raising this weight to an uncapped multiplier
 // changes the response continuously for values on either side of one.
 float multiplier=targetStops>previousStops?BRIGHT_SPEED:DARK_SPEED;
 float weight=pow(saturate(historyWeights.x),multiplier);
 float adapted=clamp(exp2(lerp(targetStops,previousStops,weight)),MIN_SCALE,MAX_SCALE);
 return float4(adapted,exp2(float(first)-32.0)*65504.0,exp2(float(last)-31.0)*65504.0,target);
}

cbuffer Frame : register(b0) {
    float4 camera, forwardAxis, rightAxis, upAxis, metric, optics, viewport, display;
};
Texture2D<float4> scene : register(t0);
Texture2D<float4> history : register(t1);
SamplerState linearClamp : register(s0);
struct Pixel { float4 position : SV_Position; float2 uv : TEXCOORD0; };
Pixel Fullscreen(uint id : SV_VertexID) {
    Pixel p; p.uv=float2((id<<1)&2,id&2);
    p.position=float4(p.uv*float2(2,-2)+float2(-1,1),0,1); return p;
}
float diskWeight(float2 uv) {
    float along=dot(-camera.xyz,forwardAxis.xyz);
    if(along<=0)return 0;
    float aspect=viewport.x/viewport.y;
    float2 center=float2(dot(-camera.xyz,rightAxis.xyz)/aspect,-dot(-camera.xyz,upAxis.xyz))/(2*along*optics.x)+.5;
    float radius=metric.w/(2*along*optics.x);
    return 1-smoothstep(.85*radius,1.2*radius,length((uv-center)*float2(aspect,1)));
}
float4 Meter(Pixel pixel) : SV_Target {
    float logSum=0,weight=0,highlight=0;
    int histogram[32]; int samples=0;
    [unroll]for(int i=0;i<32;++i)histogram[i]=0;
    // A fixed grid bounds work independently of window resolution.
    [loop]for(int y=0;y<18;++y)[loop]for(int x=0;x<32;++x) {
        float3 c=scene.SampleLevel(linearClamp,(float2(x,y)+.5)/float2(32,18),0).rgb;
        float luminance=dot(c,float3(.2126,.7152,.0722)),peak=max(c.r,max(c.g,c.b));
        if(luminance>.0001){logSum+=log2(luminance);weight++;}
        highlight=max(highlight,peak);
        if(peak>.5 && diskWeight((float2(x,y)+.5)/float2(32,18))>.5){histogram[clamp((int)floor(log2(peak))+16,0,31)]++;samples++;}
    }
    float average=weight>0?exp2(logSum/weight):.0007;
    float target=min(log2(.22/max(average,.0001)),log2(.9/max(highlight,.0001)));
    if(display.x>0 && samples>=8) {
        int accumulated=0; float percentile=.5;
        [loop]for(int i=0;i<32;++i){accumulated+=histogram[i];if(accumulated*4>=samples*3){percentile=exp2(i-15.5);break;}}
        target=log2(.55/percentile);
    }
    target=clamp(target,-18,8);
    float previous=history.Load(int3(0,0,0)).r;
    float speed=target<previous?4:1.1;
    float adapted=viewport.w>0?target:lerp(previous,target,1-exp(-viewport.z*speed));
    return float4(adapted,target,highlight,average);
}
float4 Present(Pixel pixel) : SV_Target {
    float3 c=scene.SampleLevel(linearClamp,pixel.uv,0).rgb;
    float ev=optics.y+(optics.z==1?history.Load(int3(0,0,0)).r:0);
    c*=exp2(ev);
    if(optics.z==0) {
        float peak=max(c.r,max(c.g,c.b));
        float mapped=peak/(1+peak);
        if(display.x>0)mapped=lerp(mapped,pow(mapped,.65),diskWeight(pixel.uv));
        c*=peak>0?mapped/peak:0;
    }
    c=saturate(c);
    c=float3(c.r<=.0031308?12.92*c.r:1.055*pow(c.r,1/2.4)-.055,
             c.g<=.0031308?12.92*c.g:1.055*pow(c.g,1/2.4)-.055,
             c.b<=.0031308?12.92*c.b:1.055*pow(c.b,1/2.4)-.055);
    return float4(c,1);
}

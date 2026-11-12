// The shared Kerr metric functions are prepended by Dx12Renderer.cpp.
cbuffer Frame : register(b0) {
    float4 camera, forwardAxis, rightAxis, upAxis, metric, optics, viewport, display;
};
struct Pixel { float4 position : SV_Position; float2 uv : TEXCOORD0; };
Pixel Fullscreen(uint id : SV_VertexID) {
    Pixel p; p.uv=float2((id<<1)&2,id&2);
    p.position=float4(p.uv*float2(2,-2)+float2(-1,1),0,1); return p;
}
float hash(float3 p) {
    p=frac(p*.1031); p+=dot(p,p.yzx+33.33); return frac((p.x+p.y)*p.z);
}
float3 blackbody(float kelvin) {
    float t=clamp(kelvin,1000,40000)/100;
    float r=t<=66?1:1.292936*pow(t-60,-.133205);
    float g=t<=66?.390082*log(t)-.631841:1.129891*pow(t-60,-.075515);
    float b=t>=66?1:(t<=19?0:.543207*log(t-10)-1.196254);
    return saturate(float3(r,g,b));
}
// Illustrative GPU-only directional sky until the catalogue/cubemap port lands.
float3 sky(float3 d) {
    float2 spherical=float2(atan2(d.y,d.x)/6.2831853+.5,acos(clamp(d.z,-1,1))/3.14159265);
    float2 grid=spherical*float2(1800,900),cell=floor(grid),local=frac(grid)-.5;
    float h=hash(float3(cell,618));
    float starPoint=exp(-dot(local,local)*220)*step(.994,h);
    float3 tint=lerp(float3(.45,.65,1),float3(1,.63,.3),hash(float3(cell,23)));
    return starPoint*tint*(.4+3*hash(float3(cell,62)));
}
float4 BlackHole(Pixel pixel) : SV_Target {
    float2 screen=pixel.uv*2-1;
    float3 direction=normalize(forwardAxis.xyz+rightAxis.xyz*screen.x*viewport.x/viewport.y*optics.x-upAxis.xyz*screen.y*optics.x);
    float rs=metric.x,spin=metric.y,inner=metric.z,outer=metric.w;
    float a=.5*rs*spin,horizon=.5*rs*(1+sqrt(1-spin*spin));
    float3 x=camera.xyz,p; float pt;
    if(dot(x,direction)>0 || length(cross(x,direction))>outer*1.5) return float4(sky(direction),1);
    kerrPastRay(x,direction,rs,spin,p,pt);
    float angularZ=cross(x,p).z;
    float3 light=0,tangent=direction;
    bool captured=false;
    [loop] for(int step=0;step<384;++step) {
        if(step>=(int)display.y)break;
        float radius=kerrRadius(x,a);
        if(radius<horizon*1.015){captured=true;break;}
        if(length(x)>length(camera.xyz)+4 && dot(x,tangent)>0)break;
        float interval=clamp(radius*.04,.003,.75);
        if(radius>outer*2)interval=max(interval,radius*.06);
        float3 dx1,dp1,dx2,dp2,dx3,dp3,dx4,dp4;
        kerrDerivative(x,p,pt,rs,spin,dx1,dp1);
        interval/=max(1,length(dx1));
        kerrDerivative(x+.5*interval*dx1,p+.5*interval*dp1,pt,rs,spin,dx2,dp2);
        kerrDerivative(x+.5*interval*dx2,p+.5*interval*dp2,pt,rs,spin,dx3,dp3);
        kerrDerivative(x+interval*dx3,p+interval*dp3,pt,rs,spin,dx4,dp4);
        float3 next=x+interval*(dx1+2*dx2+2*dx3+dx4)/6;
        p+=interval*(dp1+2*dp2+2*dp3+dp4)/6;
        float3 unused; kerrDerivative(next,p,pt,rs,spin,tangent,unused);
        if(x.z*next.z<0) {
            float3 intersection=lerp(x,next,x.z/(x.z-next.z));
            float r=kerrRadius(intersection,a);
            if(r>inner && r<outer) {
                float radial=(r-inner)/(outer-inner),angle=atan2(intersection.y,intersection.x)-optics.w*.0001;
                float bands=.72+.28*sin(radial*170+2*sin(angle*4));
                float spiral=.6+.4*cos(9*angle+34*radial);
                float shift=kerrDiskShift(r,rs,spin,pt,angularZ,float3(0,0,0),direction);
                float edge=smoothstep(0,.06,radial)*(1-smoothstep(.82,1,radial));
                light=3.2*blackbody(5200*pow(r/inner,-.75)*shift)*pow(shift,3.6)*edge*bands*(.55+.45*spiral);
                captured=true; break;
            }
        }
        x=next;
    }
    if(!captured)light+=sky(normalize(tangent));
    // Bounded half-float scene; alpha is reserved for source eligibility.
    return float4(clamp(light,0,60000),1);
}
// Fenced 1x1 validation pass. Same compiler, metric and integration as the scene.
float4 KerrProbe(Pixel pixel) : SV_Target {
    float3 x=camera.xyz,p;float pt;
    kerrPastRay(x,forwardAxis.xyz,metric.x,metric.y,p,pt);
    float3 dx1,dp1,dx2,dp2,dx3,dp3,dx4,dp4;
    float step=metric.z;
    kerrDerivative(x,p,pt,metric.x,metric.y,dx1,dp1);
    kerrDerivative(x+.5*step*dx1,p+.5*step*dp1,pt,metric.x,metric.y,dx2,dp2);
    kerrDerivative(x+.5*step*dx2,p+.5*step*dp2,pt,metric.x,metric.y,dx3,dp3);
    kerrDerivative(x+step*dx3,p+step*dp3,pt,metric.x,metric.y,dx4,dp4);
    return float4(x+step*(dx1+2*dx2+2*dx3+dx4)/6,pt);
}

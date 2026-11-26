#include "glad/glad.h"
#include "Atmosphere.h"
#ifdef RENDER_ENGINE_DX12
#include "dx12/Dx12SceneBridge.h"
#endif
#include <cmath>
#include <string>
#include <glm/gtc/type_ptr.hpp>

unsigned int compileShaderProgram(const char*, const char*);

bool Atmosphere::Initialize() {
    const char* vertex = R"(
#version 330 core
out vec2 screenPosition;
void main() {
    vec2 corner = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    screenPosition = corner * 2.0 - 1.0;
    gl_Position = vec4(screenPosition, 0.0, 1.0);
}
)";
    const std::string fragment=std::string("#version 330 core\n")+PlanetWeather::ShaderFunctions()+PlanetWeather::SurfaceShaderFunctions()+R"(
in vec2 screenPosition;
out vec4 fragColor;
uniform sampler2D scene;
uniform sampler2D sceneDepth;
uniform sampler2D previousDiskForeground;
uniform int diskForegroundPass;
uniform bool logarithmicDepth;
uniform mat4 inverseViewProjection;
uniform mat4 viewProjection;
uniform vec3 cameraPosition;
uniform vec3 planetCenter;
uniform vec3 lightPosition;
uniform float planetRadius;
uniform bool earthClouds;
uniform bool thinPolarClouds;
uniform float cloudTime;
uniform mat3 cloudFrame;
uniform bool oceanReflections;
uniform bool reflectionOnly;
uniform float waveTime,waterSpecularScale;
uniform vec3 waterOriginMetres;
uniform vec2 marineHazeLight; // radius and radiance of the extended source
uniform float depthBelowWater;

float cloudHash(vec3 p){p=fract(p*.1031);p+=dot(p,p.yzx+33.33);return fract((p.x+p.y)*p.z);}
float cloudNoise(vec3 p){
 vec3 b=floor(p),f=fract(p);f=f*f*(3.0-2.0*f);
 return mix(mix(mix(cloudHash(b),cloudHash(b+vec3(1,0,0)),f.x),mix(cloudHash(b+vec3(0,1,0)),cloudHash(b+vec3(1,1,0)),f.x),f.y),
 mix(mix(cloudHash(b+vec3(0,0,1)),cloudHash(b+vec3(1,0,1)),f.x),mix(cloudHash(b+vec3(0,1,1)),cloudHash(b+vec3(1,1,1)),f.x),f.y),f.z);
}
vec3 cloudCoordinates(vec3 n){
 float latitude=abs(n.y);
 float a=weatherAdvection.x,b=weatherAdvection.y,c=weatherAdvection.z;
 vec2 trades=mat2(cos(a),-sin(a),sin(a),cos(a))*n.xz;
 vec2 westerlies=mat2(cos(b),-sin(b),sin(b),cos(b))*n.xz;
 vec2 polar=mat2(cos(c),-sin(c),sin(c),cos(c))*n.xz;
 vec2 drift=mix(trades,westerlies,smoothstep(.28,.55,latitude));
 drift=mix(drift,polar,smoothstep(.72,.9,latitude));
 return vec3(drift.x,n.y,drift.y);
}
float weather(vec3 p){
 vec3 radial=normalize(cloudFrame*p),n=cloudCoordinates(radial);
 float latitude=abs(radial.y),seed=weatherParameters.z;
 vec3 warp=n+vec3(cloudNoise(n*4.0+seed),cloudNoise(n*4.0+seed+11.0),cloudNoise(n*4.0+seed+23.0))*.11;
 float macro=cloudNoise(warp*6.0+seed),banks=cloudNoise(warp*19.0+seed+17.0),cells=cloudNoise(warp*67.0+seed+31.0);
 float equatorial=exp(-pow((radial.y+.035*sin(seed))/.15,2.0));
 float dry=exp(-pow((latitude-.44)/.14,2.0));
 float fronts=exp(-pow((latitude-.70)/.19,2.0));
 float threshold=.50+.10*dry-.075*equatorial-.055*fronts+( .58-weatherParameters.y)*.30;
 float field=.58*macro+.29*banks+.13*cells;
 float coverage=smoothstep(threshold-.075,threshold+.14,field);
 // Long, broken fronts in the westerlies; small scattered cumulus in trades.
 float front=.5+.5*sin(n.y*34.0+macro*11.0+banks*3.0);
 coverage=max(coverage,fronts*smoothstep(.55,.85,front)*smoothstep(.35,.65,banks)*.78);
 if(thinPolarClouds){
  // Break up the high-latitude sheets so the ocean remains visible from orbit.
  float polar=smoothstep(.67,.93,latitude);
  float stream=cloudNoise(vec3(n.x*12.0,n.y*110.0,n.z*12.0)+seed+57.0);
  float wisps=smoothstep(.49,.79,stream)*smoothstep(.28,.72,banks);
  coverage=mix(coverage,.04*coverage+.055*wisps,polar);
 }
 for(int i=0;i<3;++i){
  if(weatherStorms[i].w<=0.0)continue;
  vec3 centre=weatherStorms[i].xyz;float cosine=clamp(dot(radial,centre),-1.0,1.0);
  float radius=acos(cosine)/weatherShapes[i].x;if(radius>1.5)continue;
  vec3 east=normalize(cross(vec3(0,1,0),centre)),north=cross(centre,east);
  float angle=atan(dot(radial,north),dot(radial,east));
  float spiral=.5+.5*cos(3.0*angle-weatherShapes[i].y*(radius*13.0+weatherShapes[i].z));
  float ragged=.75+.25*cloudNoise(radial*220.0+seed+float(i)*7.0);
  float wallRadius=.22+.022*sin(angle*4.0+weatherShapes[i].z)+.018*(cells-.5);
  float eyewall=exp(-pow((radius-wallRadius)/.105,2.0))*ragged;
  float bands=smoothstep(.34,.77,spiral)*exp(-radius*radius*1.8)*smoothstep(.13,.36,radius)*ragged;
  float life=clamp(weatherShapes[i].w,0.0,1.0);
  coverage*=1.0-(1.0-smoothstep(.065,.14,radius))*life;
  float anvil=exp(-radius*radius*3.0)*smoothstep(.12,.22,radius)*.30;
  coverage=max(coverage,(eyewall*.98+bands*.8+anvil)*life);
 }
 return clamp(coverage,0.0,1.0);
}
float cloudDensity(vec3 p,float coverage){
 float height=(length(p)-planetRadius)*149597870.7;
 if(height<2.0 || height>12.0)return 0.0;
 if(coverage<.03)return 0.0;
 float profile=smoothstep(2.0,3.2,height)*(1.0-smoothstep(7.0,12.0,height));
 vec3 local=cloudCoordinates(normalize(cloudFrame*p))*length(p)*149597870.7;
 float erosion=.65*cloudNoise(local*.13+weatherParameters.z)+.35*cloudNoise(local*.47);
 // Erode only boundaries: dense banks remain coherent and clear gaps stay open.
 return max(0.0,coverage-erosion*.32*(1.0-coverage))*profile*.75;
}

)"+R"(
vec2 sphereInterval(vec3 origin, vec3 direction, float radius) {
    // Solve in planet radii and use the product of roots near the surface.
    // Subtracting two nearly equal AU distances loses metre-scale intersections.
    vec3 scaled=origin/radius;
    float along = dot(scaled, direction);
    float c=(length(scaled)-1.0)*(length(scaled)+1.0);
    float discriminant = along * along - c;
    if (discriminant < 0.0) return vec2(1.0, -1.0);
    float root = sqrt(discriminant);
    float q=-along-(along>=0.0?root:-root);
    if(abs(q)<1e-12)return vec2(-along)*radius;
    vec2 interval=vec2(q,c/q)*radius;
    return vec2(min(interval.x,interval.y),max(interval.x,interval.y));
}

float depthDistance(float z){return (exp2(z*log2(1.0+20000.0*1e12))-1.0)/1e12;}

vec4 marchClouds(vec3 origin,vec3 direction,float begin,float end){
    if(end<=begin)return vec4(0,0,0,1);
    // Keep the weather sampling separate from the fine density samples.
    vec4 coverageKnots;
    for(int k=0;k<4;++k)coverageKnots[k]=weather(origin+direction*mix(begin,end,float(k)/4.0));
    float lastCoverage=weather(origin+direction*end);
    int steps=int(clamp(ceil((end-begin)*149597870.7/.65),24.0,128.0));
    float step=(end-begin)/float(steps);
    vec4 result=vec4(0,0,0,1);
    for(int i=0;i<128;++i){
        if(i>=steps)break;
        vec3 p=origin+direction*(begin+(float(i)+.5)*step);
        float knot=(float(i)+.5)*4.0/float(steps);
        int index=min(3,int(knot));
        float coverage=mix(coverageKnots[index],index<3?coverageKnots[min(index+1,3)]:lastCoverage,fract(knot));
        float density=cloudDensity(p,coverage);if(density<.001)continue;
        vec3 sun=normalize(lightPosition-planetCenter-p);
        float shadow=cloudDensity(p+sun*(3500.0/149597870700.0),coverage)*4.0;
        float daylight=smoothstep(-.12,.10,dot(normalize(p),sun));
        float phase=.25+.65*pow(max(dot(direction,sun),0.0),8.0);
        vec3 lighting=vec3(.12,.17,.25)*(.08+.92*daylight)
            +vec3(1.0,.96,.88)*daylight*exp(-shadow)*(.55+phase);
        float passing=exp(-density*step*149597870.7);
        result.rgb+=result.a*(1.0-passing)*lighting;
        result.a*=passing;
        // Complete the interval so bright background emission has no opacity floor.
    }
    return result;
}

vec3 auroralLight(vec3 origin,vec3 direction,float finish){
    if(auroraAxis.w<=0.0)return vec3(0);
    float km=149597870.7;
    float top=min(350.0,planetRadius*km*.09),base=top*.26;
    vec2 outer=sphereInterval(origin,direction,planetRadius+top/km);
    vec2 inner=sphereInterval(origin,direction,planetRadius+base/km);
    float begin=max(0.0,outer.x),end=min(finish,outer.y);
    if(inner.x>begin)end=min(end,inner.x);
    if(length(origin)<planetRadius+base/km)begin=max(begin,inner.y);
    if(end<=begin)return vec3(0);
    vec3 axis=auroraAxis.xyz,east=normalize(cross(vec3(0,0,1),axis)),north=cross(axis,east);
    vec3 light=vec3(0);float step=(end-begin)/20.0;
    for(int i=0;i<20;++i){
        vec3 p=origin+direction*(begin+(float(i)+.5)*step);
        float height=(length(p)-planetRadius)*km;
        vec3 n=normalize(cloudFrame*p);
        float angle=atan(dot(n,north),dot(n,east));
        float colatitude=acos(clamp(abs(dot(n,axis)),0.0,1.0));
        float folds=sin(angle*7.0+auroraMotion.x)*.018+sin(angle*13.0-auroraMotion.y)*.008;
        float oval=exp(-pow((colatitude-.32-folds)/.022,2.0));
        if(oval<.001)continue;
        float rays=.18+.82*pow(.5+.5*sin(angle*83.0+sin(angle*17.0+auroraMotion.x)*3.0+auroraMotion.y),2.0);
        float ripples=.55+.45*cloudNoise(n*75.0+vec3(auroraMotion.z,auroraMotion.x,0));
        float green=exp(-pow((height/top-.38)/.16,2.0));
        float red=exp(-pow((height/top-.75)/.21,2.0));
        float purple=exp(-pow((height/top-.28)/.055,2.0));
        vec3 sun=normalize(lightPosition-planetCenter-p);
        float night=1.0-smoothstep(-.15,.18,dot(normalize(p),sun));
        vec3 emission=vec3(.06,1.0,.20)*green+vec3(.34,.015,.05)*red+vec3(.14,.035,.30)*purple;
        light+=emission*oval*rays*ripples*night*step*km*.004*auroraAxis.w;
    }
    return light;
}

vec3 oceanReflection(vec3 background,vec3 origin,vec3 direction,float z){
    if(!oceanReflections || z>=.99999)return background;
    float waterMask=texture(scene,screenPosition*.5+.5).a;
    if(waterMask<.099 || waterMask>.405)return background;
    float distance=depthDistance(z);
    vec3 water=origin+direction*distance;
    // Only the ocean receiver, never the sky or foreground spacecraft.
    if(abs(length(water)-planetRadius)>planetRadius*.0025)return background;
    vec3 radial=normalize(water);
    // Depth reconstruction includes the actual large ocean waves. Add the
    // same two metre-scale travelling wave terms as the ground material,
    // with derivative-aware filtering instead of a rigid reflected stripe.
    vec3 dx=dFdx(water*149597870700.0),dy=dFdy(water*149597870700.0);
    vec3 coarse=cross(dx,dy);float normalLength=length(coarse);
    vec3 n=normalLength>1e-8?coarse/normalLength:radial;
    if(dot(n,radial)<0.0)n=-n;
    // Logarithmic D24 depth becomes quantized at grazing angles. Reject large
    // spurious slopes and let the continuous material waves supply fine detail.
    if(dot(n,radial)<.995)n=radial;else n=normalize(mix(radial,n,.15));
    vec3 surface=waterOriginMetres+cloudFrame*(direction*distance*149597870700.0);
    vec3 gradient=weatherWaveGradient(surface,normalize(cloudFrame*radial),true);
    gradient=transpose(cloudFrame)*gradient;gradient-=radial*dot(gradient,radial);
    n=normalize(n-gradient);
    vec3 reflected=reflect(direction,n);
    if(dot(reflected,normalize(water))<=0.0)return background;
    // Concentrate depth samples near the water so adjacent shores and objects
    // can be reflected as well as the background sky.
    vec2 hitUv=vec2(-1);float confidence=0.0;
    vec3 start=cameraPosition+direction*distance+n*(1.0/149597870700.0);
    float previousTravel=0.0;
    for(int i=0;i<24;++i){
        float u=float(i+1)/24.0;
        float travel=planetRadius*.5*u*u*u*u;
        vec3 point=start+reflected*travel;
        vec4 clip=viewProjection*vec4(point,1);
        if(clip.w<=0.0)break;
        vec2 uv=clip.xy/clip.w*.5+.5;
        if(min(uv.x,uv.y)<=0.0 || max(uv.x,uv.y)>=1.0)break;
        float sampleDepth=texture(sceneDepth,uv).r;
        float rayDistance=length(point-cameraPosition);
        float gap=rayDistance-depthDistance(sampleDepth);
        float thickness=max(20.0/149597870700.0,travel-previousTravel);
        previousTravel=travel;
        if(sampleDepth<.99999 && gap>=0.0 && gap<thickness){
            float receiver=texture(scene,uv).a;
            if(receiver>=.099 && receiver<=.405)continue; // No reflected copy of the same water/specular stripe.
            hitUv=uv;confidence=1.0;break;
        }
    }
    if(confidence==0.0){
        vec4 clip=viewProjection*vec4(reflected,0);
        if(clip.w<=0.0)return background;
        hitUv=clip.xy/clip.w*.5+.5;
        if(min(hitUv.x,hitUv.y)<=0.0 || max(hitUv.x,hitUv.y)>=1.0)return background;
        // Nearby geometry cannot stand in for an infinite reflected star.
        if(depthDistance(texture(sceneDepth,hitUv).r)<planetRadius*4.0)return background;
        confidence=1.0;
    }
    float border=min(min(hitUv.x,hitUv.y),min(1.0-hitUv.x,1.0-hitUv.y));
    confidence*=smoothstep(0.0,.055,border);
    confidence*=smoothstep(0.0,.025,dot(reflected,radial));
    confidence*=1.0-.85*weatherOceanFoam(surface,normalize(cloudFrame*radial));
    confidence*=1.0-smoothstep(1.12,1.32,length(origin)/planetRadius);
    vec2 texel=1.0/vec2(textureSize(scene,0));
    vec3 reflectedLight=texture(scene,hitUv).rgb*.5;
    reflectedLight+=(texture(scene,hitUv+texel*vec2(1,0)).rgb+texture(scene,hitUv-texel*vec2(1,0)).rgb
        +texture(scene,hitUv+texel*vec2(0,1)).rgb+texture(scene,hitUv-texel*vec2(0,1)).rgb)*.125;
    float fresnel=.0204+.9796*pow(1.0-max(dot(n,-direction),0.0),5.0);
    float analyticSpecular=clamp((waterMask-.1)/.3,0.0,1.0)*waterSpecularScale;
    // Replace the material's white analytic stripe only where a valid screen
    // reflection exists; retain that fallback when the source goes offscreen.
    vec3 transmitted=max(background-vec3(analyticSpecular*confidence),vec3(0));
    return mix(transmitted,reflectedLight,fresnel*confidence);
}

)" + R"(
vec4 underwaterScene(vec4 source,vec3 origin,vec3 direction,float sceneZ){
    const float metresPerAu=149597870700.0;
    vec3 radial=normalize(origin);
    float up=dot(direction,radial);
    float radius=length(origin)*metresPerAu;
    float offset=depthBelowWater*(2.0*radius+depthBelowWater);
    float b=radius*up,root=sqrt(b*b+offset);
    float exitDistance=b>0.0?offset/(root+b):root-b;
    float sceneDistance=depthDistance(sceneZ)*metresPerAu;
    float travel=min(sceneDistance,exitDistance);
    vec3 light=normalize(lightPosition-cameraPosition);
    float daylight=smoothstep(-.14,.23,dot(radial,light));
    float surfaceLight=daylight*exp(-depthBelowWater*.012);
    vec3 radiance=source.rgb;
    if(exitDistance<sceneDistance && exitDistance<160.0){
        vec3 point=waterOriginMetres+cloudFrame*direction*exitDistance;
        vec3 localUp=normalize(cloudFrame*radial);
        vec3 normal=normalize(radial-transpose(cloudFrame)*weatherWaveGradient(point,localUp,true));
        vec3 skyRay=refract(direction,-normal,1.333);
        float window=smoothstep(.642,.683,dot(direction,normal));
        float sun=pow(max(dot(skyRay,light),0.0),60.0);
        vec3 sky=mix(vec3(.001,.004,.009),vec3(.34,.65,.90)+sun*vec3(3.2,2.6,1.8),daylight);
        sky+=min(source.rgb,vec3(4.0))*(.65-.60*daylight);
        radiance=mix(vec3(.011,.09,.125)*surfaceLight,sky,window);
    }else if(sceneDistance<140.0){
        vec3 p=waterOriginMetres+cloudFrame*direction*sceneDistance;
        float a=sin(dot(p,vec3(.78,.09,.32))+waveTime*2.0+sin(dot(p,vec3(.21,.18,-.61))-waveTime));
        float c=sin(dot(p,vec3(-.28,.12,.90))-waveTime+sin(dot(p,vec3(.56,-.12,.28))+waveTime*3.0));
        float pattern=pow(max(0.0,1.0-abs(a+c)*.75),5.0);
        radiance*=.83+.68*pattern*surfaceLight;
    }
    vec3 transmission=exp(-min(travel,180.0)*vec3(.15,.051,.027));
    float skyLight=.72+.38*smoothstep(-.65,.58,up);
    float forwardLight=pow(max(0.0,dot(direction,light)),8.0);
    vec3 waterLight=mix(vec3(.0015,.007,.012),vec3(.016,.165,.24)*skyLight+
        vec3(.035,.11,.13)*forwardLight,surfaceLight);
    vec3 color=radiance*transmission+waterLight*(1.0-transmission);
    if(depthBelowWater>150.0 && sceneDistance<90.0 && sceneDistance<exitDistance){
        vec4 center=inverseViewProjection*vec4(0,0,1,1);
        vec3 forward=normalize(center.xyz-cameraPosition*center.w);
        float beam=smoothstep(.82,.997,dot(direction,forward))*(1.0-smoothstep(10.0,90.0,sceneDistance));
        // Model an inspection light below the sunlit ocean.
        color+=radiance*vec3(.54,.83,1.0)*exp(-sceneDistance*.033)*beam*3.6;
    }
    return vec4(color,0);
}

void main() {
    vec2 uv=screenPosition*.5+.5;
    vec4 source = texture(scene, uv);
    bool diskEmitter=source.a>1.5;
    if(diskForegroundPass!=0 && !diskEmitter){
        fragColor=vec4(0);return;
    }
    // Measuring the same transport with no background yields just the
    // foreground weather and air light. A nearer shell also attenuates any
    // foreground light already accumulated by a previous pass.
    vec3 background=diskForegroundPass==0?source.rgb:diskForegroundPass==2
        ?texture(previousDiskForeground,uv).rgb:vec3(0);
    vec4 farPoint = inverseViewProjection * vec4(screenPosition, 1.0, 1.0);
    vec3 direction = normalize(farPoint.xyz - cameraPosition * farPoint.w);
    vec3 origin = cameraPosition - planetCenter;
    float opaqueDepth=texture(sceneDepth,uv).r;
    if(depthBelowWater>0.0){
        fragColor=underwaterScene(source,origin,direction,opaqueDepth);return;
    }
    float auroraTop=auroraAxis.w>0.0?min(350000.0/149597870700.0,planetRadius*.09):0.0;
    vec2 shell = sphereInterval(origin, direction, max(planetRadius * 1.025,planetRadius+auroraTop));
    float start = max(shell.x, 0.0);
    float finish = shell.y;
    if(reflectionOnly){
        fragColor=vec4(oceanReflection(background,origin,direction,opaqueDepth),source.a);return;
    }
    if (opaqueDepth < 1.0) {
        float geometryDistance;
        if (logarithmicDepth) {
            geometryDistance = (exp2(opaqueDepth * log2(1.0 + 20000.0 * 1e12)) - 1.0) / 1e12;
        } else {
            vec4 geometry = inverseViewProjection * vec4(screenPosition, opaqueDepth * 2.0 - 1.0, 1.0);
            geometryDistance = dot(geometry.xyz / geometry.w - cameraPosition, direction);
        }
        finish = min(finish, geometryDistance);
    }
    vec2 ground = sphereInterval(origin, direction, planetRadius);
    if (ground.x > 0.0 && ground.y >= ground.x)
        finish = min(finish, ground.x);
    if (finish <= start) {
        fragColor = vec4(background, source.a);
        return;
    }
    float sourceEligibility=diskEmitter?source.a-2.0:source.a;
    bool waterReceiver=source.a>=.099 && source.a<=.405 && opaqueDepth<.99999;
    if(earthClouds){
        // A shell approximation and a bounded volume share weather and optical
        // depth. Fade by altitude; never replace the weather map on descent.
        float altitudeKm=(length(origin)-planetRadius)*149597870.7;
        float volumeWeight=1.0-smoothstep(60.0,200.0,altitudeKm);
        vec2 cloudShell=sphereInterval(origin,direction,planetRadius+12000.0/149597870700.0);
        vec2 cloudBase=sphereInterval(origin,direction,planetRadius+2000.0/149597870700.0);
        float cloudBegin=max(0.0,cloudShell.x),cloudEnd=min(finish,cloudShell.y);
        vec2 sheet=sphereInterval(origin,direction,planetRadius+5500.0/149597870700.0);
        float sheetDistance=sheet.x>0.0?sheet.x:sheet.y;
        float sheetOpticalDepth=0.0;vec3 sheetSource=vec3(0);
        if(sheetDistance>=start && sheetDistance<finish){
            vec3 p=origin+direction*sheetDistance;
            float grazing=max(.10,abs(dot(normalize(p),direction)));
            float coverage=weather(p);
            if(thinPolarClouds){
                // Keep the orbital cloud layer translucent across the ocean.
                vec3 n=cloudCoordinates(normalize(cloudFrame*p));
                float windBands=cloudNoise(vec3(n.x*12.0,n.y*85.0,n.z*12.0)+weatherParameters.z+83.0);
                coverage*=mix(.14,.40,smoothstep(.40,.77,windBands));
            }
            sheetOpticalDepth=coverage*2.8/grazing;
            vec3 sun=normalize(lightPosition-planetCenter-p);
            float illumination=smoothstep(-.12,.12,dot(normalize(p),sun));
            sheetSource=vec3(.83,.87,.94)*(.025+.975*illumination);
        }
        float sheetT=exp(-sheetOpticalDepth);
        vec3 sheetLight=sheetSource*(1.0-sheetT);
        vec4 volume=vec4(0,0,0,1);
        if(volumeWeight>0.0 && cloudEnd>cloudBegin){
            // Subtract the clear cavity from the outer cloud shell. A grazing
            // ray can leave the first bank and meet another before the disk.
            const float marchLimit=200000.0/149597870700.0;
            bool intersectsBase=cloudBase.y>cloudBase.x;
            float nearEnd=intersectsBase?min(cloudEnd,cloudBase.x):cloudEnd;
            nearEnd=min(nearEnd,cloudBegin+marchLimit);
            float farBegin=intersectsBase?max(cloudBegin,cloudBase.y):cloudEnd;
            float farEnd=min(cloudEnd,farBegin+marchLimit);
            bool hasBack=farEnd>farBegin;
            bool sheetInBack=hasBack && sheetDistance>=farBegin;
            // Keep the distant approximation on the correct side of the
            // back interval, and fade it in beyond the traced distance.
            float tracedEnd=sheetInBack?farEnd:nearEnd;
            float distantWeight=smoothstep(0.0,40.0,(sheetDistance-tracedEnd)*149597870.7);
            float distantT=exp(-sheetOpticalDepth*distantWeight);
            vec3 distantLight=sheetSource*(1.0-distantT);
            volume=marchClouds(origin,direction,cloudBegin,nearEnd);
            if(!sheetInBack){
                volume.rgb+=volume.a*distantLight;
                volume.a*=distantT;
            }
            if(hasBack){
                vec4 back=marchClouds(origin,direction,farBegin,farEnd);
                if(sheetInBack){
                    back.rgb+=back.a*distantLight;
                    back.a*=distantT;
                }
                volume.rgb+=volume.a*back.rgb;
                volume.a*=back.a;
            }
        }
        float cloudT=mix(sheetT,volume.a,volumeWeight);
        background=background*cloudT+mix(sheetLight,volume.rgb,volumeWeight);
        if(!waterReceiver)sourceEligibility*=cloudT;
    }
    const int samples = 12;
    // Quadratic intervals resolve the dense air next to a landed camera without
    // increasing the sample count or introducing a discontinuous horizon branch.
    // Earth-like Rayleigh and aerosol scale heights, scaled with body radius.
    // Light-path extinction produces a warm low Sun; Mie forward scattering
    // supplies the circumsolar aureole without introducing an optical emitter.
    vec3 betaRayleigh = vec3(37.0, 86.0, 210.0) / planetRadius;
    vec3 betaMie = vec3(22.0) / planetRadius;
    vec3 betaOzone = vec3(4.1, 12.0, .55) / planetRadius;
    float betaMist=0.0,mistHalfAngle=0.0,mistRadiance=0.0;
    if(marineHazeLight.x>0.0){
        betaMist=19.0/planetRadius;
        mistHalfAngle=atan(marineHazeLight.x/length(lightPosition-planetCenter))*.48;
        // Sea mist spreads the disk's light over a broad forward lobe.
        const float g=.54;
        float lightCosine=clamp(dot(direction,normalize(lightPosition-cameraPosition)),-1.0,1.0);
        float forward=pow((1.0-g)*(1.0-g)/(1.0+g*g-2.0*g*lightCosine),1.5);
        mistRadiance=marineHazeLight.y*(.10+.90*forward);
    }
    vec3 transmittance = vec3(1);
    vec3 scattering = vec3(0);
    for (int sampleIndex = 0; sampleIndex < samples; ++sampleIndex) {
        float u0=float(sampleIndex)/float(samples),u1=float(sampleIndex+1)/float(samples);
        float begin=mix(start,finish,u0*u0),end=mix(start,finish,u1*u1);
        float interval=end-begin;
        vec3 samplePosition = origin + direction * ((begin+end)*.5);
        float altitude = max(length(samplePosition) - planetRadius, 0.0);
        float density = exp(-altitude / (planetRadius * 0.00125));
        float haze = exp(-altitude / (planetRadius * .000188));
        float ozone = max(0.0,1.0-abs(altitude/planetRadius-.00392)/.00235);
        vec3 sunDirection = normalize(lightPosition - planetCenter - samplePosition);
        vec2 shadow = sphereInterval(samplePosition, sunDirection, planetRadius);
        float illumination = shadow.x > 0.0 && shadow.y >= shadow.x ? 0.0 : 1.0;
        float cosine = dot(direction, sunDirection);
        float rayleighPhase = .75 * (1.0 + cosine * cosine);
        const float g=.76;
        float miePhase=(1.0-g*g)/pow(max(1.0+g*g-2.0*g*cosine,.001),1.5);
        float sunlightLength=max(0.0,sphereInterval(samplePosition,sunDirection,planetRadius*1.025).y);
        vec3 sunlightDepth=vec3(0);
        if(illumination>0.0){
            // Four quadratically spaced samples concentrate work near dense air.
            for(int lightSample=0;lightSample<4;++lightSample){
                float u=(float(lightSample)+.5)/4.0;
                float h=max(0.0,length(samplePosition+sunDirection*(sunlightLength*u*u))-planetRadius);
                float rd=exp(-h/(planetRadius*.00125)),md=exp(-h/(planetRadius*.000188));
                float od=max(0.0,1.0-abs(h/planetRadius-.00392)/.00235);
                sunlightDepth+=(betaRayleigh*rd+betaMie*md*1.1+betaOzone*od)*(sunlightLength*2.0*u/4.0);
            }
        }
        vec3 sunTransmission=exp(-sunlightDepth)*illumination;
        float mistExtinction=0.0;
        vec3 mistScattering=vec3(0.0);
        if(betaMist>0.0){
            mistExtinction=betaMist*exp(-altitude/(planetRadius*.00061));
            // The upper disk can light marine droplets after its center has
            // set. Approximate the visible fraction of the extended emitter.
            float elevation=dot(normalize(samplePosition),sunDirection);
            float visibleDisk=smoothstep(-mistHalfAngle,mistHalfAngle,elevation);
            // The droplets also scatter blue skylight into the marine layer.
            mistScattering=vec3(.88,1.04,1.43)*mistExtinction*mistRadiance*visibleDisk;
        }
        vec3 extinction=betaRayleigh*density+betaMie*haze*1.1+betaOzone*ozone+mistExtinction;
        vec3 segment=exp(-extinction*interval);
        vec3 scatterCoefficient=betaRayleigh*density*rayleighPhase+betaMie*haze*miePhase;
        vec3 single=(scatterCoefficient*sunTransmission*.30+mistScattering)/max(extinction,vec3(1e-10));
        // Bounded second-order approximation brightens the lower blue sky.
        vec3 multiple=vec3(.055,.095,.16)*(1.0-exp(-density*.6))*(.03+.97*illumination);
        scattering+=transmittance*(1.0-segment)*(single+multiple);
        transmittance *= segment;
    }
    if(waterReceiver)sourceEligibility=.1+(source.a-.1)*dot(transmittance,vec3(.2126,.7152,.0722));
    if(diskEmitter)sourceEligibility+=2.0;
    fragColor = vec4(background * transmittance + scattering+auroralLight(origin,direction,finish)*sqrt(transmittance), sourceEligibility);
}
)";
    program = compileShaderProgram(vertex, fragment.c_str());
    int linked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) {
        Release();
        return false;
    }
    glGenVertexArrays(1, &vao);
    glGenTextures(1, &sceneTexture);
    glGenTextures(1, &depthTexture);
    glGenFramebuffers(1,&diskForegroundFramebuffer);
    return true;
}

void Atmosphere::Render(const glm::mat4& viewProjection, const glm::vec3& camera,
                        const glm::vec3& center, float radius, bool logarithmicDepth,
                        const glm::vec3& lightPosition,bool earthClouds,double seconds,const glm::mat3& rotation,bool oceanReflections,float waterSpecularScale,
                        const PlanetWeather::State* weather,glm::vec2 marineHazeLight,bool captureDiskForeground,bool renderAir,
                        float depthBelowWater) const {
#ifdef RENDER_ENGINE_DX12
    oceanReflections = oceanReflections && NativeDX12::ScreenSpaceReflectionsEnabled();
#endif
    const bool reflectOcean=oceanReflections && depthBelowWater<=0 && glm::length(center-camera)<radius*1.32f;
    if(!renderAir && !reflectOcean)return;
    int viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);
    if (viewport[2] <= 0 || viewport[3] <= 0)
        return;
    if(glm::length(center-camera)>radius*2) {
        const auto clip=viewProjection*glm::vec4(center,1);
        if(clip.w<=0)return;
        const float projectionScale=glm::length(glm::vec3(viewProjection[0][1],viewProjection[1][1],viewProjection[2][1]));
        const float screenRadius=radius*1.10f*projectionScale/clip.w;
        if(screenRadius*viewport[3]*.5f<1.0f)return;
        const auto ndc=glm::vec2(clip)/clip.w;
        if(std::abs(ndc.x)>1+screenRadius || std::abs(ndc.y)>1+screenRadius)return;
    }
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sceneTexture);
    if (textureWidth != viewport[2] || textureHeight != viewport[3]) {
        textureWidth = viewport[2];
        textureHeight = viewport[3];
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, textureWidth, textureHeight, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, depthTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, textureWidth, textureHeight, 0,
            GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, sceneTexture);
    }
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, viewport[0], viewport[1], textureWidth, textureHeight);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, depthTexture);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, viewport[0], viewport[1], textureWidth, textureHeight);
    glUseProgram(program);
    PlanetWeather::Upload(program,weather?*weather:PlanetWeather::Make(PlanetWeather::Profile{},seconds));
    const auto inverse = glm::inverse(viewProjection);
    glUniformMatrix4fv(glGetUniformLocation(program, "inverseViewProjection"), 1, GL_FALSE, glm::value_ptr(inverse));
    glUniformMatrix4fv(glGetUniformLocation(program, "viewProjection"), 1, GL_FALSE, glm::value_ptr(viewProjection));
    glUniform3fv(glGetUniformLocation(program, "cameraPosition"), 1, glm::value_ptr(camera));
    glUniform3fv(glGetUniformLocation(program, "planetCenter"), 1, glm::value_ptr(center));
    glUniform3fv(glGetUniformLocation(program, "lightPosition"), 1, glm::value_ptr(lightPosition));
    glUniform2f(glGetUniformLocation(program, "marineHazeLight"), marineHazeLight.x, marineHazeLight.y);
    glUniform1f(glGetUniformLocation(program, "planetRadius"), radius);
    glUniform1f(glGetUniformLocation(program,"depthBelowWater"),depthBelowWater);
    glUniform1i(glGetUniformLocation(program,"earthClouds"),earthClouds);
    glUniform1i(glGetUniformLocation(program,"oceanReflections"),oceanReflections);
    glUniform1i(glGetUniformLocation(program,"reflectionOnly"),false);
    glUniform1f(glGetUniformLocation(program,"cloudTime"),static_cast<float>(std::remainder(seconds,86400.0*100)));
    glUniform1f(glGetUniformLocation(program,"waveTime"),static_cast<float>(std::remainder(seconds,6.283185307179586)));
    glUniform1f(glGetUniformLocation(program,"waterSpecularScale"),waterSpecularScale);
    const auto cloudFrame=glm::transpose(rotation);
    const auto waterOrigin=glm::vec3(glm::mod(glm::dmat3(cloudFrame)*glm::dvec3(camera-center)*149597870700.0,glm::dvec3(4096)));
    glUniform3fv(glGetUniformLocation(program,"waterOriginMetres"),1,glm::value_ptr(waterOrigin));
    glUniformMatrix3fv(glGetUniformLocation(program,"cloudFrame"),1,GL_FALSE,glm::value_ptr(cloudFrame));
    glUniform1i(glGetUniformLocation(program, "scene"), 0);
    glUniform1i(glGetUniformLocation(program, "sceneDepth"), 1);
    const int foregroundPass=glGetUniformLocation(program,"diskForegroundPass");
    glUniform1i(foregroundPass,0);
    glUniform1i(glGetUniformLocation(program, "logarithmicDepth"), logarithmicDepth);
    const bool blending=glIsEnabled(GL_BLEND);glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glBindVertexArray(vao);
    if(renderAir)glDrawArrays(GL_TRIANGLES, 0, 3);
    if(captureDiskForeground){
        const int nextIndex=diskForegroundIndex==0?1:0;
        auto& layer=diskForeground[nextIndex];
        glActiveTexture(GL_TEXTURE2);
        if(!layer.texture)glGenTextures(1,&layer.texture);
        if(layer.width!=textureWidth || layer.height!=textureHeight){
            layer.width=textureWidth;layer.height=textureHeight;
            glBindTexture(GL_TEXTURE_2D,layer.texture);
            glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA16F,layer.width,layer.height,0,GL_RGBA,GL_FLOAT,nullptr);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        }
        glBindTexture(GL_TEXTURE_2D,DiskForegroundTexture());
        GLint target=0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING,&target);
        glBindFramebuffer(GL_FRAMEBUFFER,diskForegroundFramebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,layer.texture,0);
        glUniform1i(glGetUniformLocation(program,"previousDiskForeground"),2);
        glUniform1i(foregroundPass,diskForegroundIndex<0?1:2);
        glDrawArrays(GL_TRIANGLES,0,3);
        glBindFramebuffer(GL_FRAMEBUFFER,target);
        diskForegroundIndex=nextIndex;
        glUniform1i(foregroundPass,0);
        glBindTexture(GL_TEXTURE_2D,0);
        glActiveTexture(GL_TEXTURE1);
    }
    if(reflectOcean){
        // Reflect the finished weather/atmosphere, not the uncomposited sky.
        // This also includes the volume-cloud pass during a surface approach.
        glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,sceneTexture);
        if(renderAir)glCopyTexSubImage2D(GL_TEXTURE_2D,0,0,0,viewport[0],viewport[1],textureWidth,textureHeight);
        glUniform1i(glGetUniformLocation(program,"reflectionOnly"),true);
        glDrawArrays(GL_TRIANGLES,0,3);
    }
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);if(blending)glEnable(GL_BLEND);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Atmosphere::Release() {
    if (program) glDeleteProgram(program);
    if (vao) glDeleteVertexArrays(1, &vao);
    if (sceneTexture) glDeleteTextures(1, &sceneTexture);
    if (depthTexture) glDeleteTextures(1, &depthTexture);
    if(diskForegroundFramebuffer)glDeleteFramebuffers(1,&diskForegroundFramebuffer);
    for(auto& layer:diskForeground){
        if(layer.texture)glDeleteTextures(1,&layer.texture);
        layer={};
    }
    diskForegroundFramebuffer=0;
    ResetDiskForeground();
    program = vao = sceneTexture = depthTexture = 0;
    textureWidth = textureHeight = 0;
}

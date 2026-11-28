#include "CameraOptics.h"
#ifdef RENDER_ENGINE_DX12
#include "dx12/Dx12SceneBridge.h"
#endif
#include "LensCalibration.h"
#include "glad/glad.h"
#include <cmath>
#include <glm/gtc/type_ptr.hpp>
#include <initializer_list>
#include <string>

unsigned int compileShaderProgram(const char *, const char *);

namespace
{
using LensCalibration::profiles;

float UndistortedRadius(int profile, float distorted)
{
    const auto &lens = profiles[profile];
    float radius = distorted;
    for (int iteration = 0; iteration < 8; ++iteration)
    {
        float value = radius;
        float derivative = 1.0f;
        if (lens.distortionModel == 1)
        {
            const float k = lens.distortion.x;
            value = radius * (1.0f - k + k * radius * radius);
            derivative = 1.0f - k + 3.0f * k * radius * radius;
        }
        else if (lens.distortionModel == 2)
        {
            const auto c = lens.distortion;
            const float d = 1.0f - c.x - c.y - c.z;
            value = radius * (((c.x * radius + c.y) * radius + c.z) * radius + d);
            derivative = ((4.0f * c.x * radius + 3.0f * c.y) * radius + 2.0f * c.z) * radius + d;
        }
        radius -= (value - distorted) / derivative;
    }
    return radius;
}
} // namespace

const char *CameraOptics::Name(int profile)
{
    return profiles[profile].name;
}

float CameraOptics::FieldOfView(int profile, float aspect)
{
    return LensCalibration::FieldOfView(profile,aspect);
}

glm::vec2 CameraOptics::MapScreen(int profile, const glm::vec2 &screen, float aspect, float fieldOfView)
{
    const float zoom =
        std::tan(glm::radians(fieldOfView * 0.5f)) / std::tan(glm::radians(FieldOfView(profile, aspect) * 0.5f));
    const float radius = glm::length(screen * glm::vec2(1.5f, 1.5f / aspect)) * zoom;
    return radius > 0.00001f ? screen * (UndistortedRadius(profile, radius) / radius) : screen;
}

bool CameraOptics::Initialize()
{
    const char *vertex = R"(
#version 330 core
out vec2 screenPosition;
void main() {
    vec2 corner = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    screenPosition = corner * 2.0 - 1.0;
    gl_Position = vec4(screenPosition, 0, 1);
}
)";
    const char* sceneGrading=R"(
uniform sampler2D scene;
uniform sampler2D localDepth;
uniform vec3 stellarHint;
uniform vec2 texel;
uniform bool hdrMode;
vec3 gradedScene(vec2 uv,float lod){
    vec4 source=textureLod(scene,uv,lod);
    vec3 value=source.rgb;
    // Compress a resolved HDR photosphere while preserving uncovered sky
    // in filtered limb pixels, including the samples used for bloom.
    if(!hdrMode || source.a>1.5 || stellarHint.z<=texel.y*2.0)return value;
    vec2 offset=(uv-stellarHint.xy)*vec2(texel.y/texel.x,1);
    float reach=stellarHint.z*1.25+texel.y*exp2(lod)*2.0;
    if(dot(offset,offset)>=reach*reach)return value;
    float coverage=clamp(source.a,0.0,1.0);
    if(lod<.5){
        coverage=source.a>=.99?1.0:0.0;
        if(texture(localDepth,uv).r>=.99998)coverage=0.0;
        else if(max(value.r,max(value.g,value.b))>128.0)coverage=1.0;
    }
    if(coverage>0.0){
        vec2 outward=dot(offset,offset)>1e-16?normalize(offset):vec2(1,0);
        vec2 beyond=stellarHint.xy+outward*vec2(texel.x/texel.y,1)
            *(stellarHint.z*1.6+texel.y*exp2(lod)*3.0);
        vec3 sky=textureLod(scene,beyond,lod).rgb;
        value=value*.001+sky*(1.0-coverage)*.999;
    }
    return value;
}
)";
    const char* hdrCurve=R"(
const float hdrWhite=.85;
const float hdrKnee=.20;
float hdrShoulder(float peak){
    float excess=max(peak-hdrKnee,0.0);
    return min(peak,hdrKnee)+excess/(1.0+excess/(hdrWhite-hdrKnee));
}
float hdrUnmap(float peak){
    return min(peak,hdrKnee)+max(peak-hdrKnee,0.0)*(hdrWhite-hdrKnee)/max(hdrWhite-peak,.0001);
}
)";
    const std::string fragment=std::string("#version 330 core\n")+sceneGrading+hdrCurve+R"(
in vec2 screenPosition;
out vec4 fragColor;
uniform sampler2D bloom;
uniform sampler2D flareSources;
uniform bool systemScoped;
uniform sampler2D exposureState;
uniform bool automaticExposure;
uniform float atmosphereMetering;
uniform float marineMetering;
uniform bool referenceGlare;
uniform float flareStrength;
uniform int profile;
uniform int distortionModel;
uniform vec3 distortion;
uniform vec3 redTca;
uniform vec3 blueTca;
uniform vec3 vignetting;
uniform vec2 sensorScale;
uniform float exposureGain;

float undoTca(float distorted, vec3 terms) {
    float radius = distorted;
    for (int i = 0; i < 6; ++i) {
        float value = radius * ((terms.x * radius + terms.y) * radius + terms.z);
        float derivative = (3.0 * terms.x * radius + 2.0 * terms.y) * radius + terms.z;
        radius -= (value - distorted) / derivative;
    }
    return radius;
}

float undoDistortion(float distorted) {
    if(distortionModel==0)return distorted;
    float radius = distorted;
    for (int i = 0; i < 8; ++i) {
        float value = radius;
        float derivative = 1.0;
        if (distortionModel == 1) {
            value = radius * (1.0 - distortion.x + distortion.x * radius * radius);
            derivative = 1.0 - distortion.x + 3.0 * distortion.x * radius * radius;
        } else if (distortionModel == 2) {
            float d = 1.0 - distortion.x - distortion.y - distortion.z;
            value = radius * (((distortion.x * radius + distortion.y) * radius + distortion.z) * radius + d);
            derivative = ((4.0 * distortion.x * radius + 3.0 * distortion.y) * radius + 2.0 * distortion.z) * radius + d;
        }
        radius -= (value - distorted) / derivative;
    }
    return radius;
}

void main() {
    float radiusSquared = dot(screenPosition, screenPosition);
    float radius = length(screenPosition * sensorScale);
    float scale = radius > 0.00001 ? undoDistortion(radius) / radius : 1.0;
    vec2 mapped = screenPosition * scale;
    vec2 uv = 0.5 + 0.5 * mapped;
    if (any(lessThan(uv, vec2(0))) || any(greaterThan(uv, vec2(1)))) {
        fragColor = vec4(0, 0, 0, 1);
        return;
    }
    vec3 color=gradedScene(uv,0.0);
    if(radius>0.00001 && (any(notEqual(redTca,vec3(0,0,1))) || any(notEqual(blueTca,vec3(0,0,1))))){
        float redScale=undoDistortion(undoTca(radius,redTca))/radius;
        float blueScale=undoDistortion(undoTca(radius,blueTca))/radius;
        color.r=gradedScene(.5+.5*screenPosition*redScale,0.0).r;
        color.b=gradedScene(.5+.5*screenPosition*blueScale,0.0).b;
    }
    if (profile == 1) {
        vec2 offset = texel * smoothstep(0.05, 1.0, radiusSquared) * 2.5;
        vec3 peripheral = (gradedScene(uv + vec2(offset.x, 0),0.0)
            + gradedScene(uv - vec2(offset.x, 0),0.0)
            + gradedScene(uv + vec2(0, offset.y),0.0)
            + gradedScene(uv - vec2(0, offset.y),0.0)) * 0.25;
        color = mix(color, peripheral, smoothstep(0.05, 0.8, radiusSquared) * 0.65);
    }
    // PA vignetting uses radius normalised to the full-frame corner.
    float vignetteRadiusSquared = radius * radius / 3.25;
    color *= max(0.01, 1.0 + vignetteRadiusSquared * (vignetting.x
        + vignetteRadiusSquared * (vignetting.y + vignetteRadiusSquared * vignetting.z)));
    color+=texture(bloom,uv).rgb;
    vec3 opticalLight=vec3(0);
    // Analytic optical signatures, not measured coating/internal reflection data.
    if(profile>0 && flareStrength>0) {
        float strongest=0.0;int primary=-1,host=-1;
        for(int i=0;i<8;++i){
            vec4 candidate=texelFetch(flareSources,ivec2(i*2,0),0);
            // A resolved nearby photosphere wins ties over a distant point.
            float rank=candidate.z*(1.0+min(candidate.w*40.0,2.0));
            if(rank>strongest){strongest=rank;primary=i;}
            if(candidate.z>0.0 && stellarHint.z>0.0
                && length((candidate.xy-stellarHint.xy)*vec2(texel.y/texel.x,1))<max(texel.y*4.0,stellarHint.z*.15))host=i;
        }
        // Catalogue PSFs use a visibility grade, so their peak pixel values
        // cannot outrank a genuinely visible nearby photosphere by themselves.
        if(host>=0){primary=host;vec4 source=texelFetch(flareSources,ivec2(host*2,0),0);strongest=source.z*(1.0+min(source.w*40.0,2.0));}
        float primaryBrightness=primary>=0?texelFetch(flareSources,ivec2(primary*2,0),0).z:0.0;
        float primaryEnergy=min(2.5,log2(1.0+primaryBrightness))*.65*flareStrength;
        for(int source=0;source<8;++source) {
            vec4 emitter=texelFetch(flareSources,ivec2(source*2,0),0);
            if(emitter.z<=0)continue;
            bool duplicate=false;
            for(int previous=0;previous<source;++previous) {
                vec4 earlier=texelFetch(flareSources,ivec2(previous*2,0),0);
                if(earlier.z>0 && length((earlier.xy-emitter.xy)*vec2(texel.y/texel.x,1))
                    <max(.008,earlier.w+emitter.w))duplicate=true;
            }
            if(duplicate && !systemScoped)continue;
            vec3 tint=texelFetch(flareSources,ivec2(source*2+1,0),0).rgb;
            vec2 p=(uv-emitter.xy)*vec2(texel.y/texel.x,1);
            float r=length(p);
            float sourceAngle=r>1e-7?atan(p.y,p.x):0.0;
            float energy=min(2.5,log2(1+emitter.z))*.65*flareStrength;
            float rank=emitter.z*(1.0+min(emitter.w*40.0,2.0));
            // Independent optical centers: weaker sources never move the main
            // star's flare, and their combined ghost energy stays subordinate.
            if(source!=primary && !systemScoped)energy=min(energy*.12*pow(clamp(rank/max(strongest,.0001),0.0,1.0),2.0),primaryEnergy*.08);
            float sourceRadius=emitter.w;
            float coreRadius=.018+sourceRadius*.7;
            float halo=coreRadius*coreRadius/(r*r+coreRadius*coreRadius)*exp(-r/(coreRadius*4.0));
            if(profile==1) {
                float irregular=.8+.2*cos(7*sourceAngle+sin(11*sourceAngle));
                opticalLight+=tint*energy*halo*(.65+.35*irregular);
            }else{
                // Canon: eight warm rays; Nikon: broad cool halo/eighteen rays;
                // Otus: fine violet starburst; telephoto: long clean diffraction;
                // Distagon: soft amber glare and green internal reflections.
                float rays=8.0,rotation=.10,reach=.19,sharpness=150.0,ringRadius=.12;
                vec3 coating=vec3(1,.55,.22);
                int ghosts=2;
                if(profile==3){rays=18.0;rotation=.03;reach=.12;sharpness=90.0;ringRadius=.19;coating=vec3(.22,.72,1);ghosts=4;}
                if(profile==4){rays=18.0;rotation=.21;reach=.16;sharpness=270.0;ringRadius=.075;coating=vec3(.65,.28,1);ghosts=1;}
                if(profile==5){rays=10.0;rotation=0;reach=.34;sharpness=340.0;ringRadius=.045;coating=vec3(.55,.72,1);ghosts=0;}
                if(profile==6){rays=18.0;rotation=.33;reach=.14;sharpness=65.0;ringRadius=.145;coating=vec3(.35,1,.42);ghosts=3;}
                float finiteSource=1.0/(1.0+sourceRadius*150.0);
                float spikes=pow(abs(cos((sourceAngle+rotation)*rays*.5)),sharpness);
                vec3 tail=exp(-r/(reach*vec3(1.12,1,.86)))*.018/(r+.018);
                float ring=exp(-pow((r-ringRadius)/(.008+sourceRadius*.25),2.0));
                opticalLight+=energy*(tint*(halo*.55+tail*spikes*finiteSource*2.5)
                    +coating*ring*.045*finiteSource);
                float offAxis=smoothstep(.025,.23,length(emitter.xy-.5));
                for(int ghost=0;ghost<4;++ghost) {
                    if(ghost>=ghosts)break;
                    float index=float(ghost);
                    vec2 ghostCenter=.5+(.5-emitter.xy)*(.35+index*.62);
                    vec2 q=(uv-ghostCenter)*vec2(texel.y/texel.x,1);
                    float size=.012+index*.009+float(profile==3)*.016;
                    float a=atan(q.y,q.x)+rotation;
                    float polygon=cos(3.14159265/rays)/cos(mod(a,6.2831853/rays)-3.14159265/rays);
                    float edge=length(q)/(size*polygon);
                    float aperture=(1.0-smoothstep(.75,1.0,edge))*.32+exp(-pow((edge-.92)*12.0,2.0))*.5;
                    vec3 ghostTint=mix(coating,coating.brg,index*.2);
                    opticalLight+=ghostTint*energy*aperture*offAxis*.13;
                }
                if(referenceGlare) {
                    // Soft radial scatter, broad spectral arcs and a reflection train.
                    // These are analytic light shapes, not a texture over the scene.
                    float detail=.5+.5*sin(sourceAngle*83.0+sin(sourceAngle*37.0)*2.0+rotation);
                    float fineRays=pow(detail,7.0);
                    float scatter=exp(-r/.22)*.018/(r+.025);
                    float smallSource=1.0/(1.0+sourceRadius*90.0);
                    vec3 warmWhite=normalize(tint+vec3(1.2,1.1,.85))*1.7;
                    float core=exp(-r*r/.00012)*6.0;
                    float lineWidth=max(texel.y*.55,.0003)+r*.0008;
                    float streak=exp(-abs(p.y)/lineWidth)*exp(-abs(p.x)/.32)
                        +exp(-abs(p.x)/lineWidth)*exp(-abs(p.y)/.24);
                    opticalLight+=warmWhite*energy*smallSource*(core+scatter*(.2+fineRays*2.0));
                    // A thermal-coloured cross for unresolved star points. Broad
                    // diffuse signatures remain in their existing lens profiles.
                    opticalLight+=tint*energy*smallSource*streak*(profile==5?.7:.24);
                    float aspect=texel.y/texel.x;
                    float arcRadius=(.36+.015*float(profile))*aspect;
                    vec3 bands=exp(-pow((vec3(r)-arcRadius*vec3(1.055,1.0,.945))
                        /(arcRadius*.055),vec3(2)));
                    float lateral=pow(abs(p.x)/max(r,.0001),6.0);
                    opticalLight+=bands*energy*smallSource*lateral*.12;
                    // Soft overlapping discs/rings avoid hard opaque "bubble" ghosts.
                    for(int reflection=0;reflection<5;++reflection) {
                        float index=float(reflection);
                        vec2 center=.5+(.5-emitter.xy)*(.3+index*.70);
                        vec2 q=(uv-center)*vec2(aspect,1);
                        float size=.018+index*.017;
                        float edge=length(q)/size;
                        float soft=exp(-edge*edge*2.0)*.18+exp(-pow((edge-.9)*3.0,2.0))*.22;
                        vec3 tintGhost=mix(vec3(.28,.65,.22),vec3(.45,.18,.65),index*.25);
                        float lensResponse=profile==4?.45:profile==3?1.2:.8;
                        // An ideal telephoto has diffraction/scatter but no internal reflections.
                        if(profile!=5)opticalLight+=tintGhost*energy*soft*offAxis*smallSource*lensResponse*.35;
                    }
                }
            }
        }
    }
    float adaptedGain=automaticExposure?exp2(texelFetch(exposureState,ivec2(0),0).r):1.0;
    float localGain=adaptedGain;
    float commonMetering=max(atmosphereMetering,marineMetering);
    if(automaticExposure && commonMetering<1.0 && adaptedGain<.65 && texture(localDepth,uv).r<.99998){
        vec4 surface=texture(scene,uv);
        float peak=max(surface.r,max(surface.g,surface.b));
        float weight=(1.0-smoothstep(1.0,2.0,peak))*(1.0-smoothstep(.75,.99,surface.a));
        localGain=mix(adaptedGain,.65,weight*(1.0-commonMetering));
    }
    color = max(color * exposureGain * localGain, vec3(0));
    if (hdrMode) {
        // Apply the ordinary HDR shoulder once to the surrounding scene.
        float peak=max(color.r,max(color.g,color.b));
        color*=hdrShoulder(peak)/max(peak,.000001);
    } else {
        if(automaticExposure && marineMetering>0.0){
            // Preserve hue as the disk, haze and reflections approach white.
            float peak=max(color.r,max(color.g,color.b));
            float excess=max(peak-.62,0.0);
            float rolloff=min(peak,.62)+excess/(1.0+excess/.36);
            color*=mix(1.0,rolloff/max(peak,1e-6),marineMetering);
        }
        color = clamp(color, 0.0, 1.0);
    }
    // Expose/tone-map the scene first. Optical artifacts add bounded local light
    // without entering scene metering or rescaling its RGB highlight shoulder.
    vec3 glare=1.0-exp(-max(opticalLight*exposureGain*adaptedGain,vec3(0)));
    color+=(1.0-color)*glare;
    color = mix(12.92 * color, 1.055 * pow(color, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), color));
    fragColor = vec4(color, 1.0);
}
)";
    program = compileShaderProgram(vertex, fragment.c_str());
    int linked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked)
    {
        Release();
        return false;
    }
    glGenVertexArrays(1, &vao);
#ifdef RENDER_ENGINE_DX12
    glGenTextures(1, &reconstructionTexture);
#endif
    glGenFramebuffers(1, &framebuffer);
    glGenTextures(1, &sceneColor);
    glGenRenderbuffers(1, &sceneDepth);
    glGenTextures(1,&depthTexture);
    const char *extract = R"(#version 330 core
out vec4 result;
uniform sampler2D scene;
uniform vec3 stellarHint;
uniform vec4 systemSources[8];
uniform int systemSourceCount;
// Rank real stellar continua above a green selection crosshair at every mip.
float eligibility(vec4 c){return clamp(c.a>1.5?c.a-2.0:c.a,0.0,1.0);}
float brightness(vec4 c){return max(c.r,c.b)*eligibility(c);}
float stellarBrightness(vec4 c){
    return brightness(c)*step(.99,eligibility(c))*(1.0-smoothstep(1.15,1.5,c.g/max(max(c.r,c.b),.0001)));
}
float extent(vec2 position,vec2 direction,float threshold,vec2 dimensions) {
    float previous=0;
    for(int step=0;step<24;++step) {
        float distance=pow(float(step+1),2.0)*1.5;
        vec2 candidate=position+direction*distance/dimensions;
        if(any(lessThan(candidate,vec2(0))) || any(greaterThan(candidate,vec2(1))))break;
        if(brightness(textureLod(scene,candidate,0))<threshold)break;
        previous=distance;
    }
    return previous;
}
void main(){
    int pixel=int(gl_FragCoord.x),tile=pixel/2;
    vec2 origin=vec2(tile%4,tile/4)/vec2(4,2);
    vec2 tileSize=1.0/vec2(4,2);
    vec2 dimensions=vec2(textureSize(scene,0));
    if(systemSourceCount>=0){
        if(tile>=systemSourceCount){result=vec4(0);return;}
        vec4 hint=systemSources[tile];
        if(hint.z<=0 || any(lessThan(hint.xy,vec2(0))) || any(greaterThanEqual(hint.xy,vec2(1)))){result=vec4(0);return;}
        if(hint.w>.5){
            // Each member owns a slot, even when two stars share a screen tile.
            // Read real scene radiance at that member, never another sky peak.
            vec3 sum=vec3(0);float count=0,value=0;
            if(hint.z*dimensions.y>=2.0){
                for(int i=0;i<16;++i){
                    float angle=(float(i)+.37)*6.2831853/16.0;
                    vec2 uv=hint.xy+vec2(cos(angle)*dimensions.y/dimensions.x,sin(angle))*hint.z*.55;
                    if(any(lessThan(uv,vec2(0))) || any(greaterThanEqual(uv,vec2(1))))continue;
                    vec4 sample=textureLod(scene,uv,0);
                    if(stellarBrightness(sample)>.05){sum+=sample.rgb;count+=1;}
                }
                if(count>=3){sum/=count;value=max(sum.r,max(sum.g,sum.b));}
            }else{
                vec2 anchor=(floor(hint.xy*dimensions)+.5)/dimensions;
                for(int y=-3;y<=3;++y)for(int x=-3;x<=3;++x){
                    vec2 uv=anchor+vec2(x,y)/dimensions;
                    if(any(lessThan(uv,vec2(0))) || any(greaterThanEqual(uv,vec2(1))))continue;
                    vec4 sample=textureLod(scene,uv,0);float b=stellarBrightness(sample);
                    if(b>value){value=b;sum=sample.rgb;}
                }
            }
            float accepted=smoothstep(.70,1.15,value);
            result=pixel%2==0?vec4(hint.xy,value*accepted,hint.z):vec4(sum/max(max(sum.r,sum.g),max(sum.b,.00001)),value);
            return;
        }
        // Accretion flows may have an off-centre beamed highlight. Search only
        // their projected emitting area, rather than unrelated sky tiles.
        vec2 extent=hint.z*vec2(dimensions.y/dimensions.x,1);
        origin=max(vec2(0),hint.xy-extent);
        tileSize=min(vec2(1),hint.xy+extent)-origin;
    }
    // The coarse grid must cover every mip texel, including narrow sources at
    // tile boundaries. Rounding down can skip the central column entirely.
    float level=max(0.0,ceil(log2(max(dimensions.x*tileSize.x/8.0,dimensions.y*tileSize.y/8.0))));
    float best=-1;vec2 position=origin+tileSize*.5;
    for(int y=0;y<8;++y)for(int x=0;x<8;++x){
        vec2 candidate=origin+(vec2(x,y)+.5)*tileSize/8.0;
        vec2 mipSize=vec2(textureSize(scene,int(level)));
        candidate=(floor(candidate*mipSize)+.5)/mipSize;
        float b=brightness(textureLod(scene,candidate,level));
        if(b>best){best=b;position=candidate;}
    }
    for(int step=7;step>=0;--step){
        if(float(step)>=level)continue;
        // Use actual mip sizes: a 1021px framebuffer does not halve evenly.
        vec2 mipSize=vec2(textureSize(scene,step));
        vec2 cell=floor(position*mipSize);
        best=-1;
        for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x){
            vec2 candidate=(cell+vec2(x,y)+.5)/mipSize;
            if(systemSourceCount>=0 && (any(lessThan(candidate,origin)) || any(greaterThan(candidate,origin+tileSize))))continue;
            float b=brightness(textureLod(scene,candidate,float(step)));
            if(b>best){best=b;position=candidate;}
        }
    }
    // A known host provides a search seed, never synthetic radiance. Visibility
    // and brightness still come from the composited image, including occlusion.
    int hintTile=int(stellarHint.x*4.0)+int(stellarHint.y*2.0)*4;
    if(systemSourceCount<0 && stellarHint.z>0 && all(greaterThanEqual(stellarHint.xy,vec2(0)))
        && all(lessThan(stellarHint.xy,vec2(1))) && hintTile==tile)
        position=stellarHint.xy;
    // Resolve a real highlight beside a selection crosshair. Suppressing green
    // during the coarse mip search would also erase the star underneath it.
    vec2 anchor=(floor(position*dimensions)+.5)/dimensions;
    best=-1;
    for(int y=-3;y<=3;++y)for(int x=-3;x<=3;++x){
        vec2 candidate=anchor+vec2(x,y)/dimensions;
        float b=stellarBrightness(textureLod(scene,candidate,0));
        if(b>best){best=b;position=candidate;}
    }
    vec4 source=textureLod(scene,position,0);
    vec3 color=source.rgb;
    float value=max(color.r,max(color.g,color.b));
    float broad=brightness(textureLod(scene,position,3));
    float compact=smoothstep(1.7,5.0,value/max(broad,.00001))*smoothstep(.70,1.15,value);
    float threshold=max(.35,value*.25);
    float left=extent(position,vec2(-1,0),threshold,dimensions);
    float right=extent(position,vec2(1,0),threshold,dimensions);
    float down=extent(position,vec2(0,-1),threshold,dimensions);
    float up=extent(position,vec2(0,1),threshold,dimensions);
    float radius=max(left+right,down+up)*.5;
    float extended=smoothstep(1.5,6.0,min(left+right,down+up)) * smoothstep(.70,1.4,value);
    vec2 center=position+vec2(right-left,up-down)*.5/dimensions;
    // The navigation reticle is green; real stellar continua are not green-dominant.
    float rejectReticle=1.0-smoothstep(1.15,1.5,color.g/max(max(color.r,color.b),.0001));
    float accepted=max(compact,extended)*rejectReticle*step(.99,eligibility(source));
    bool hostVisible=stellarHint.z>0 && all(greaterThanEqual(stellarHint.xy,vec2(0)))
        && all(lessThan(stellarHint.xy,vec2(1)));
    if(hostVisible && systemSourceCount<0) {
        float distance=length((position-stellarHint.xy)*vec2(dimensions.x/dimensions.y,1));
        if(tile==hintTile) {
            // One disk, one optical center. Read the visible photosphere rather
            // than the brightest erupting strand or active-region footpoint.
            vec3 sum=vec3(0);float count=0;
            for(int i=0;i<16;++i){
                float angle=(float(i)+.37)*6.2831853/16.0;
                vec2 uv=stellarHint.xy+vec2(cos(angle)*dimensions.y/dimensions.x,sin(angle))*stellarHint.z*.55;
                if(any(lessThan(uv,vec2(0))) || any(greaterThan(uv,vec2(1))))continue;
                vec4 sample=textureLod(scene,uv,0);
                if(stellarBrightness(sample)>.05){sum+=sample.rgb;count+=1;}
            }
            color=count>=3?sum/count:vec3(0);
            value=max(color.r,max(color.g,color.b));
            accepted=count>=3?smoothstep(.70,1.15,value):0;
            position=center=stellarHint.xy;radius=stellarHint.z*dimensions.y;
        } else if(distance<stellarHint.z*1.9+4.0/dimensions.y)accepted=0;
    }
    if(pixel%2==0)result=vec4(mix(position,center,extended),value*accepted,radius/dimensions.y);
    else result=vec4(color/max(value,.00001),value);
})";
    flareProgram = compileShaderProgram(vertex, extract);
    glGetProgramiv(flareProgram, GL_LINK_STATUS, &linked);
    if (!linked)
    {
        Release();
        return false;
    }
    glGenFramebuffers(1, &flareFramebuffer);
    glGenTextures(1, &flareTexture);
    glBindTexture(GL_TEXTURE_2D, flareTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 16, 1, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, flareFramebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, flareTexture, 0);
    const bool flareComplete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (!flareComplete)
    {
        Release();
        return false;
    }
    // Grade at native texels before creating bloom mips. Filtering a disk tag
    // first gives a dark silhouette surrounded by ungraded bright mip halos.
    const std::string hdrDisk=std::string("#version 330 core\n")+hdrCurve+R"(
uniform sampler2D scene,diskForeground,exposureState;
uniform bool hasDiskForeground;
out vec4 result;
void main(){
    vec4 source=texelFetch(scene,ivec2(gl_FragCoord.xy),0);vec3 value=source.rgb;
    if(source.a>1.5){
        // Weather and in-scattered light keep their ordinary HDR exposure.
        vec2 uv=gl_FragCoord.xy/vec2(textureSize(scene,0));
        vec3 foreground=hasDiskForeground?min(value,texture(diskForeground,uv).rgb):vec3(0);
        vec3 transmitted=value-foreground;
        float peak=max(transmitted.r,max(transmitted.g,transmitted.b));
        float gain=exp2(texelFetch(exposureState,ivec2(0),0).b);
        float desired=.84*(1.0-exp(-pow(max(peak*gain,0.0),.65)*1.3));
        // Invert the final HDR shoulder, so it is applied exactly once.
        float encoded=hdrUnmap(desired);
        vec3 graded=transmitted*encoded/max(peak,1e-8);
        value=foreground+mix(transmitted,graded,clamp((source.a-2.0)*4.0,0.0,1.0));
    }
    result=vec4(value,source.a);
})";
    hdrDiskProgram=compileShaderProgram(vertex,hdrDisk.c_str());
    glGetProgramiv(hdrDiskProgram,GL_LINK_STATUS,&linked);
    if(!linked){Release();return false;}
    glGenFramebuffers(1,&hdrDiskFramebuffer);glGenTextures(1,&hdrDiskTexture);
    const std::string bloomShader=std::string("#version 330 core\n")+sceneGrading+R"(
in vec2 screenPosition;
out vec4 result;
const vec2 offsets[8]=vec2[8](vec2(1,0),vec2(.7071,.7071),vec2(0,1),vec2(-.7071,.7071),
    vec2(-1,0),vec2(-.7071,-.7071),vec2(0,-1),vec2(.7071,-.7071));
void main(){
    vec2 uv=screenPosition*.5+.5;
    vec3 glow=max(gradedScene(uv,2.0)-1.0,vec3(0))*.04;
    for(int i=0;i<8;++i)glow+=max(gradedScene(uv+offsets[i]*texel*12.0,4.0)-.5,vec3(0))*.0035;
    result=vec4(glow,0);
}
)";
    bloomProgram=compileShaderProgram(vertex,bloomShader.c_str());
    glGetProgramiv(bloomProgram,GL_LINK_STATUS,&linked);
    if(!linked){Release();return false;}
    glGenFramebuffers(1,&bloomFramebuffer);glGenTextures(1,&bloomTexture);
    if(!exposureMeter.Initialize()) { Release(); return false; }
    return true;
}

void CameraOptics::BeginFrame()
{
    frameActive = false;
    diskForegroundTexture=0;
    if (!program)
        return;
    int viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);
    if (viewport[2] <= 0 || viewport[3] <= 0)
        return;
    glActiveTexture(GL_TEXTURE0);
    if (width != viewport[2] || height != viewport[3])
    {
        width = viewport[2];
        height = viewport[3];
        glBindTexture(GL_TEXTURE_2D,sceneColor);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA16F,width,height,0,GL_RGBA,GL_FLOAT,nullptr);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glBindRenderbuffer(GL_RENDERBUFFER, sceneDepth);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
        glBindTexture(GL_TEXTURE_2D,depthTexture);
        glTexImage2D(GL_TEXTURE_2D,0,GL_DEPTH_COMPONENT24,width,height,0,GL_DEPTH_COMPONENT,GL_UNSIGNED_INT,nullptr);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    hdrDiskValid=sceneMipsValid=false;
    bloomInput=flareInput=0;
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sceneColor, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, sceneDepth);
    frameActive = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    if (!frameActive)
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
}

void CameraOptics::SetSystemSources(const std::array<glm::vec4,8>& sources,int count) {
    count=glm::clamp(count,0,8);
    if(systemSourceCount==count && systemSources==sources)return;
    systemSources=sources;
    systemSourceCount=count;
    flareInput=0;
}

void CameraOptics::PrepareHdrDisk() const {
    if(hdrDiskValid)return;
    glActiveTexture(GL_TEXTURE0);
    if(hdrDiskExtent!=resolvedExtent){
        hdrDiskExtent=resolvedExtent;
        glBindTexture(GL_TEXTURE_2D,hdrDiskTexture);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA16F,resolvedExtent.x,resolvedExtent.y,0,GL_RGBA,GL_FLOAT,nullptr);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    }
    glBindFramebuffer(GL_FRAMEBUFFER,hdrDiskFramebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,hdrDiskTexture,0);
    glViewport(0,0,resolvedExtent.x,resolvedExtent.y);
    glUseProgram(hdrDiskProgram);
    glBindTexture(GL_TEXTURE_2D,resolvedScene);
    glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,diskForegroundTexture);
    glActiveTexture(GL_TEXTURE2);glBindTexture(GL_TEXTURE_2D,exposureMeter.Texture());
    glUniform1i(glGetUniformLocation(hdrDiskProgram,"scene"),0);
    glUniform1i(glGetUniformLocation(hdrDiskProgram,"diskForeground"),1);
    glUniform1i(glGetUniformLocation(hdrDiskProgram,"hasDiskForeground"),diskForegroundTexture!=0);
    glUniform1i(glGetUniformLocation(hdrDiskProgram,"exposureState"),2);
    glDrawArrays(GL_TRIANGLES,0,3);
    glBindFramebuffer(GL_FRAMEBUFFER,0);
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,hdrDiskTexture);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
    hdrDiskValid=true;
}

void CameraOptics::PrepareBloom(unsigned source,bool hdrMode) const {
    if(bloomInput==source && bloomHdr==hdrMode && (!hdrMode || bloomStellarHint==stellarHint))return;
    const glm::ivec2 extent=(resolvedExtent+3)/4;
    glActiveTexture(GL_TEXTURE0);
    if(extent!=bloomExtent){
        bloomExtent=extent;
        glBindTexture(GL_TEXTURE_2D,bloomTexture);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA16F,extent.x,extent.y,0,GL_RGBA,GL_FLOAT,nullptr);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    }
    glBindFramebuffer(GL_FRAMEBUFFER,bloomFramebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,bloomTexture,0);
    glViewport(0,0,extent.x,extent.y);
    glUseProgram(bloomProgram);
    glBindTexture(GL_TEXTURE_2D,source);
    glActiveTexture(GL_TEXTURE3);glBindTexture(GL_TEXTURE_2D,depthTexture);
    glUniform1i(glGetUniformLocation(bloomProgram,"scene"),0);
    glUniform1i(glGetUniformLocation(bloomProgram,"localDepth"),3);
    glUniform1i(glGetUniformLocation(bloomProgram,"hdrMode"),hdrMode);
    glUniform3fv(glGetUniformLocation(bloomProgram,"stellarHint"),1,glm::value_ptr(stellarHint));
    glUniform2f(glGetUniformLocation(bloomProgram,"texel"),1.f/resolvedExtent.x,1.f/resolvedExtent.y);
    glDrawArrays(GL_TRIANGLES,0,3);
    bloomInput=source;
    bloomHdr=hdrMode;
    bloomStellarHint=stellarHint;
}

void CameraOptics::Render(int profile, float fieldOfView, float exposureEv, bool hdrMode, float flareStrength,
                          bool reuseScene, float realSeconds) const
{
    if (!program || !frameActive)
        return;
    glActiveTexture(GL_TEXTURE0);
    if (!reuseScene || !resolvedScene)
    {
        glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);
        glBindTexture(GL_TEXTURE_2D,depthTexture);
        glCopyTexSubImage2D(GL_TEXTURE_2D,0,0,0,0,0,width,height);
        resolvedScene=sceneColor;
        resolvedExtent={width,height};
#ifdef RENDER_ENGINE_DX12
        if(NativeDX12::ResolveSceneReconstruction(sceneColor,sceneDepth,reconstructionTexture,realSeconds)){
            resolvedScene=reconstructionTexture;
            glBindTexture(GL_TEXTURE_2D,resolvedScene);
            glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_WIDTH,&resolvedExtent.x);
            glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_HEIGHT,&resolvedExtent.y);
        }
#endif
    }
    int viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);
    const bool adaptive=!hdrMode && autoExposure;
    const auto diskRadius=1.5f*blackHoleHint.z*glm::vec2(float(resolvedExtent.y)/resolvedExtent.x,1);
    const auto diskCenter=glm::vec2(blackHoleHint);
    const bool diskVisible=blackHoleVisibility && blackHoleHint.z>0 &&
        glm::all(glm::greaterThan(diskCenter+diskRadius,glm::vec2(0))) &&
        glm::all(glm::lessThan(diskCenter-diskRadius,glm::vec2(1)));
    const auto diskHint=diskVisible?blackHoleHint:glm::vec3(-1,-1,0);
    const bool gradeDisk=hdrMode && diskVisible;
    glBindFramebuffer(GL_FRAMEBUFFER,0);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
    glBindVertexArray(vao);
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,resolvedScene);
    if(!sceneMipsValid && !gradeDisk){
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
        sceneMipsValid=true;
    }else if(!sceneMipsValid){
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    }
    if(adaptive || (gradeDisk && !hdrDiskValid))
        exposureMeter.Update(resolvedScene,stellarHint,realSeconds,diskHint,reuseScene,adaptive,
            atmosphereMetering,adaptive?marineMetering:0.f);
    else if(!gradeDisk)exposureMeter.Reset();
    if(gradeDisk)PrepareHdrDisk();
    const unsigned source=gradeDisk?hdrDiskTexture:resolvedScene;
    PrepareBloom(source,hdrMode);
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,source);
    if (profile > 0 && flareStrength > 0 && (flareInput!=source || flareStellarHint!=stellarHint))
    {
        glBindFramebuffer(GL_FRAMEBUFFER, flareFramebuffer);
        glViewport(0, 0, 16, 1);
        glDisable(GL_DEPTH_TEST);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glDisable(GL_BLEND);
        glUseProgram(flareProgram);
        glUniform1i(glGetUniformLocation(flareProgram, "scene"), 0);
        glUniform3fv(glGetUniformLocation(flareProgram,"stellarHint"),1,glm::value_ptr(stellarHint));
        glUniform1i(glGetUniformLocation(flareProgram,"systemSourceCount"),systemSourceCount);
        glUniform4fv(glGetUniformLocation(flareProgram,"systemSources"),8,glm::value_ptr(systemSources[0]));
        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        flareInput=source;
        flareStellarHint=stellarHint;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);
    glUseProgram(program);
    glUniform3fv(glGetUniformLocation(program,"stellarHint"),1,glm::value_ptr(stellarHint));
    glUniform1i(glGetUniformLocation(program, "scene"), 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, flareTexture);
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(glGetUniformLocation(program, "flareSources"), 1);
    glUniform1i(glGetUniformLocation(program,"systemScoped"),systemSourceCount>=0);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D,exposureMeter.Texture());
    glUniform1i(glGetUniformLocation(program,"exposureState"),2);
    glUniform1i(glGetUniformLocation(program,"automaticExposure"),adaptive);
    glUniform1f(glGetUniformLocation(program,"atmosphereMetering"),atmosphereMetering);
    glUniform1f(glGetUniformLocation(program,"marineMetering"),marineMetering);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D,depthTexture);
    glUniform1i(glGetUniformLocation(program,"localDepth"),3);
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D,bloomTexture);
    glUniform1i(glGetUniformLocation(program,"bloom"),4);
    glUniform1i(glGetUniformLocation(program,"referenceGlare"),referenceGlare);
    glActiveTexture(GL_TEXTURE0);
    glUniform1f(glGetUniformLocation(program, "flareStrength"), flareStrength);
    glUniform1i(glGetUniformLocation(program, "profile"), profile);
    const auto &lens = profiles[profile];
    glUniform1i(glGetUniformLocation(program, "distortionModel"), lens.distortionModel);
    glUniform3fv(glGetUniformLocation(program, "distortion"), 1, glm::value_ptr(lens.distortion));
    glUniform3fv(glGetUniformLocation(program, "redTca"), 1, glm::value_ptr(lens.redTca));
    glUniform3fv(glGetUniformLocation(program, "blueTca"), 1, glm::value_ptr(lens.blueTca));
    glUniform3fv(glGetUniformLocation(program, "vignetting"), 1, glm::value_ptr(lens.vignetting));
    const float aspect = static_cast<float>(width) / height;
    const float zoom =
        std::tan(glm::radians(fieldOfView * 0.5f)) / std::tan(glm::radians(FieldOfView(profile, aspect) * 0.5f));
    glUniform2f(glGetUniformLocation(program, "sensorScale"), 1.5f * zoom, 1.5f * zoom / aspect);
    glUniform1f(glGetUniformLocation(program, "exposureGain"), std::exp2(exposureEv));
    glUniform1i(glGetUniformLocation(program, "hdrMode"), hdrMode);
    glUniform2f(glGetUniformLocation(program, "texel"), 1.f/resolvedExtent.x, 1.f/resolvedExtent.y);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void CameraOptics::Release()
{
    if(bloomProgram)glDeleteProgram(bloomProgram);
    if(bloomFramebuffer)glDeleteFramebuffers(1,&bloomFramebuffer);
    if(bloomTexture)glDeleteTextures(1,&bloomTexture);
    bloomProgram=bloomFramebuffer=bloomTexture=bloomInput=flareInput=resolvedScene=0;
    bloomExtent=resolvedExtent={0,0};sceneMipsValid=false;
    if(hdrDiskProgram)glDeleteProgram(hdrDiskProgram);
    if(hdrDiskFramebuffer)glDeleteFramebuffers(1,&hdrDiskFramebuffer);
    if(hdrDiskTexture)glDeleteTextures(1,&hdrDiskTexture);
    hdrDiskProgram=hdrDiskFramebuffer=hdrDiskTexture=0;hdrDiskValid=false;hdrDiskExtent={0,0};
    exposureMeter.Release();
    if (flareProgram)
        glDeleteProgram(flareProgram);
    if (flareFramebuffer)
        glDeleteFramebuffers(1, &flareFramebuffer);
    if (flareTexture)
        glDeleteTextures(1, &flareTexture);
    flareProgram = flareFramebuffer = flareTexture = 0;
    if (program)
        glDeleteProgram(program);
    if (vao)
        glDeleteVertexArrays(1, &vao);
    if (reconstructionTexture)
        glDeleteTextures(1, &reconstructionTexture);
    if (sceneColor)
        glDeleteTextures(1, &sceneColor);
    if (sceneDepth)
        glDeleteRenderbuffers(1, &sceneDepth);
    if(depthTexture)glDeleteTextures(1,&depthTexture);
    if (framebuffer)
        glDeleteFramebuffers(1, &framebuffer);
    program = vao = reconstructionTexture = sceneColor = sceneDepth = depthTexture = framebuffer = 0;
    frameActive = false;
    width = height = 0;
}

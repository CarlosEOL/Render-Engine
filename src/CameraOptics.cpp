#include "CameraOptics.h"
#include "LensCalibration.h"
#include "glad/glad.h"
#include <cmath>
#include <glm/gtc/type_ptr.hpp>
#include <initializer_list>

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
    if (profile == 0)
        return 45.0f;
    if (profile == 1)
        return glm::degrees(2.0f * std::atan(std::tan(glm::radians(50.0f)) / aspect));
    // A 36 mm sensor width, vertically cropped to the viewport's aspect ratio.
    const auto &lens = profiles[profile];
    const float realFocal =
        lens.focalLength *
        (lens.distortionModel == 2 ? 1.0f - lens.distortion.x - lens.distortion.y - lens.distortion.z : 1.0f);
    return glm::degrees(2.0f * std::atan(18.0f / (realFocal * aspect)));
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
    const char *fragment = R"(
#version 330 core
in vec2 screenPosition;
out vec4 fragColor;
uniform sampler2D scene;
uniform sampler2D flareSources;
uniform sampler2D exposureState;
uniform bool automaticExposure;
uniform bool referenceGlare;
uniform float flareStrength;
uniform int profile;
uniform int distortionModel;
uniform vec3 distortion;
uniform vec3 redTca;
uniform vec3 blueTca;
uniform vec3 vignetting;
uniform vec2 sensorScale;
uniform vec2 texel;
uniform float exposureGain;
uniform bool hdrMode;

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
    float redScale = radius > 0.00001 ? undoDistortion(undoTca(radius, redTca)) / radius : 1.0;
    float blueScale = radius > 0.00001 ? undoDistortion(undoTca(radius, blueTca)) / radius : 1.0;
    vec3 color = vec3(texture(scene, 0.5 + 0.5 * screenPosition * redScale).r,
        texture(scene, uv).g, texture(scene, 0.5 + 0.5 * screenPosition * blueScale).b);
    if (profile == 1) {
        vec2 offset = texel * smoothstep(0.05, 1.0, radiusSquared) * 2.5;
        vec3 peripheral = (texture(scene, uv + vec2(offset.x, 0)).rgb
            + texture(scene, uv - vec2(offset.x, 0)).rgb
            + texture(scene, uv + vec2(0, offset.y)).rgb
            + texture(scene, uv - vec2(0, offset.y)).rgb) * 0.25;
        color = mix(color, peripheral, smoothstep(0.05, 0.8, radiusSquared) * 0.65);
    }
    // PA vignetting uses radius normalised to the full-frame corner.
    float vignetteRadiusSquared = radius * radius / 3.25;
    color *= max(0.01, 1.0 + vignetteRadiusSquared * (vignetting.x
        + vignetteRadiusSquared * (vignetting.y + vignetteRadiusSquared * vignetting.z)));
    vec3 glow = max(textureLod(scene, uv, 2.0).rgb - 1.0, vec3(0)) * 0.04;
    for(int tap=0;tap<8;++tap) {
        float angle=float(tap)*.78539816;
        vec2 offset=vec2(cos(angle),sin(angle))*texel*12.0;
        glow+=max(textureLod(scene,uv+offset,4.0).rgb-.5,vec3(0))*.0035;
    }
    color += glow;
    vec3 opticalLight=vec3(0);
    // Analytic optical signatures, not measured coating/internal reflection data.
    if(profile>0 && flareStrength>0) {
        for(int source=0;source<8;++source) {
            vec4 emitter=texelFetch(flareSources,ivec2(source*2,0),0);
            if(emitter.z<=0)continue;
            bool duplicate=false;
            for(int previous=0;previous<source;++previous) {
                vec4 earlier=texelFetch(flareSources,ivec2(previous*2,0),0);
                if(earlier.z>0 && length((earlier.xy-emitter.xy)*vec2(texel.y/texel.x,1))
                    <max(.008,earlier.w+emitter.w))duplicate=true;
            }
            if(duplicate)continue;
            vec3 tint=texelFetch(flareSources,ivec2(source*2+1,0),0).rgb;
            vec2 p=(uv-emitter.xy)*vec2(texel.y/texel.x,1);
            float r=length(p);
            float sourceAngle=r>1e-7?atan(p.y,p.x):0.0;
            float energy=min(2.5,log2(1+emitter.z))*.65*flareStrength;
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
                    float streak=exp(-abs(p.y)/(.0012+r*.003))*exp(-abs(p.x)/.48)*.10;
                    opticalLight+=warmWhite*energy*smallSource*(core+scatter*(.2+fineRays*2.0)+streak);
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
    color = max(color * exposureGain * adaptedGain, vec3(0));
    if (hdrMode) {
        // Apply the shoulder to peak radiance, preserving highlight hue ratios.
        float peak=max(color.r,max(color.g,color.b));
        float mappedPeak=clamp((peak*(2.51*peak+.03))/(peak*(2.43*peak+.59)+.14),0.0,1.0);
        color*=mappedPeak/max(peak,.000001);
    } else
        color = clamp(color, 0.0, 1.0);
    // Expose/tone-map the scene first. Optical artifacts add bounded local light
    // without entering scene metering or rescaling its RGB highlight shoulder.
    vec3 glare=1.0-exp(-max(opticalLight*exposureGain*adaptedGain,vec3(0)));
    color+=(1.0-color)*glare;
    color = mix(12.92 * color, 1.055 * pow(color, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), color));
    fragColor = vec4(color, 1.0);
}
)";
    program = compileShaderProgram(vertex, fragment);
    int linked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked)
    {
        Release();
        return false;
    }
    glGenVertexArrays(1, &vao);
    glGenTextures(1, &sceneTexture);
    glGenFramebuffers(1, &framebuffer);
    glGenTextures(1, &sceneColor);
    glGenRenderbuffers(1, &sceneDepth);
    const char *extract = R"(#version 330 core
out vec4 result;
uniform sampler2D scene;
uniform vec3 stellarHint;
// Rank real stellar continua above a green selection crosshair at every mip.
float brightness(vec4 c){return max(c.r,c.b)*clamp(c.a,0.0,1.0);}
float stellarBrightness(vec4 c){
    return brightness(c)*step(.99,c.a)*(1.0-smoothstep(1.15,1.5,c.g/max(max(c.r,c.b),.0001)));
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
    // The coarse grid must cover every mip texel, including narrow sources at
    // tile boundaries. Rounding down can skip the central column entirely.
    float level=max(0.0,ceil(log2(max(dimensions.x/32.0,dimensions.y/16.0))));
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
            float b=brightness(textureLod(scene,candidate,float(step)));
            if(b>best){best=b;position=candidate;}
        }
    }
    // A known host provides a search seed, never synthetic radiance. Visibility
    // and brightness still come from the composited image, including occlusion.
    int hintTile=int(stellarHint.x*4.0)+int(stellarHint.y*2.0)*4;
    if(stellarHint.z>0 && all(greaterThanEqual(stellarHint.xy,vec2(0)))
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
    float accepted=max(compact,extended)*rejectReticle*step(.99,source.a);
    bool hostVisible=stellarHint.z>0 && all(greaterThanEqual(stellarHint.xy,vec2(0)))
        && all(lessThan(stellarHint.xy,vec2(1)));
    if(hostVisible) {
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
    if(!exposureMeter.Initialize()) { Release(); return false; }
    return true;
}

void CameraOptics::BeginFrame()
{
    frameActive = false;
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
        for (const auto texture : {sceneTexture, sceneColor})
        {
            glBindTexture(GL_TEXTURE_2D, texture);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        }
        glBindRenderbuffer(GL_RENDERBUFFER, sceneDepth);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sceneColor, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, sceneDepth);
    frameActive = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    if (!frameActive)
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
}

void CameraOptics::Render(int profile, float fieldOfView, float exposureEv, bool hdrMode, float flareStrength,
                          bool reuseScene, float realSeconds) const
{
    if (!program || !frameActive)
        return;
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sceneTexture);
    if (!reuseScene)
    {
        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, width, height);
        glGenerateMipmap(GL_TEXTURE_2D);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    int viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);
    const bool adaptive=!hdrMode && autoExposure;
    if (profile > 0 && flareStrength > 0)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, flareFramebuffer);
        glViewport(0, 0, 16, 1);
        glDisable(GL_DEPTH_TEST);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glDisable(GL_BLEND);
        glUseProgram(flareProgram);
        glUniform1i(glGetUniformLocation(flareProgram, "scene"), 0);
        glUniform3fv(glGetUniformLocation(flareProgram,"stellarHint"),1,glm::value_ptr(stellarHint));
        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    }
    if(adaptive) exposureMeter.Update(sceneTexture,stellarHint,realSeconds);
    else exposureMeter.Reset();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glUseProgram(program);
    glUniform1i(glGetUniformLocation(program, "scene"), 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, flareTexture);
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(glGetUniformLocation(program, "flareSources"), 1);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D,exposureMeter.Texture());
    glUniform1i(glGetUniformLocation(program,"exposureState"),2);
    glUniform1i(glGetUniformLocation(program,"automaticExposure"),adaptive);
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
    glUniform2f(glGetUniformLocation(program, "texel"), 1.0f / width, 1.0f / height);
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
    if (sceneTexture)
        glDeleteTextures(1, &sceneTexture);
    if (sceneColor)
        glDeleteTextures(1, &sceneColor);
    if (sceneDepth)
        glDeleteRenderbuffers(1, &sceneDepth);
    if (framebuffer)
        glDeleteFramebuffers(1, &framebuffer);
    program = vao = sceneTexture = sceneColor = sceneDepth = framebuffer = 0;
    frameActive = false;
    width = height = 0;
}

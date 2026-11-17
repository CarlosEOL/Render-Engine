#include "glad/glad.h"
#include "Atmosphere.h"
#include <cmath>
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
    const char* fragment = R"(
#version 330 core
in vec2 screenPosition;
out vec4 fragColor;
uniform sampler2D scene;
uniform sampler2D sceneDepth;
uniform bool logarithmicDepth;
uniform mat4 inverseViewProjection;
uniform mat4 viewProjection;
uniform vec3 cameraPosition;
uniform vec3 planetCenter;
uniform vec3 lightPosition;
uniform float planetRadius;
uniform bool earthClouds;
uniform float cloudTime;
uniform mat3 cloudFrame;
uniform bool oceanReflections;
uniform bool reflectionOnly;
uniform float waveTime,waterSpecularScale;
uniform vec3 waterOriginMetres;

float cloudHash(vec3 p){p=fract(p*.1031);p+=dot(p,p.yzx+33.33);return fract((p.x+p.y)*p.z);}
float cloudNoise(vec3 p){
 vec3 b=floor(p),f=fract(p);f=f*f*(3.0-2.0*f);
 return mix(mix(mix(cloudHash(b),cloudHash(b+vec3(1,0,0)),f.x),mix(cloudHash(b+vec3(0,1,0)),cloudHash(b+vec3(1,1,0)),f.x),f.y),
 mix(mix(cloudHash(b+vec3(0,0,1)),cloudHash(b+vec3(1,0,1)),f.x),mix(cloudHash(b+vec3(0,1,1)),cloudHash(b+vec3(1,1,1)),f.x),f.y),f.z);
}
float weather(vec3 p){
 vec3 n=normalize(cloudFrame*p);n+=vec3(cloudTime*.0000007,0,cloudTime*.0000003);
 float field=.58*cloudNoise(n*11.0)+.28*cloudNoise(n*29.0+17.0)+.14*cloudNoise(n*73.0+31.0);
 return smoothstep(.43,.65,field);
}
float cloudDensity(vec3 p,float coverage){
 float height=(length(p)-planetRadius)*149597870.7;
 if(height<2.0 || height>12.0)return 0.0;
 if(coverage<.03)return 0.0;
 float profile=smoothstep(2.0,3.2,height)*(1.0-smoothstep(7.0,12.0,height));
 vec3 local=cloudFrame*p*149597870.7;
 float erosion=.65*cloudNoise(local*.13+vec3(cloudTime*.00001,0,0))+.35*cloudNoise(local*.47);
 return max(0.0,coverage-erosion*.35)*profile*.75;
}

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
    vec3 k1=vec3(.7,.1,.5)*2.0,k2=vec3(-.3,.2,.8)*5.0;
    float a=dot(surface,k1)+waveTime,b=dot(surface,k2)-waveTime;
    float filter1=1.0-smoothstep(.7,3.0,fwidth(a)),filter2=1.0-smoothstep(.7,3.0,fwidth(b));
    vec3 gradient=.025*(k1*cos(a)*filter1+.4*k2*cos(b)*filter2);
    gradient=transpose(cloudFrame)*gradient;gradient-=radial*dot(gradient,radial);
    n=normalize(n-gradient);
    vec3 reflected=reflect(direction,n);
    if(dot(reflected,normalize(water))<=0.0)return background;
    // Bounded depth march for finite receivers. The direction projection below
    // handles the already-lensed stellar sky, whose depth is intentionally infinite.
    vec2 hitUv=vec2(-1);float confidence=0.0;
    vec3 start=cameraPosition+direction*distance+n*(1.0/149597870700.0);
    for(int i=0;i<24;++i){
        float u=float(i+1)/24.0;
        vec3 point=start+reflected*(planetRadius*.5*u*u);
        vec4 clip=viewProjection*vec4(point,1);
        if(clip.w<=0.0)break;
        vec2 uv=clip.xy/clip.w*.5+.5;
        if(min(uv.x,uv.y)<=0.0 || max(uv.x,uv.y)>=1.0)break;
        float sampleDepth=texture(sceneDepth,uv).r;
        float rayDistance=length(point-cameraPosition);
        float gap=rayDistance-depthDistance(sampleDepth);
        if(sampleDepth<.99999 && gap>=0.0 && gap<max(20.0/149597870700.0,rayDistance*.015)){
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
    vec2 texel=1.0/vec2(textureSize(scene,0));
    vec3 reflectedLight=texture(scene,hitUv).rgb*.5;
    reflectedLight+=(texture(scene,hitUv+texel*vec2(1,0)).rgb+texture(scene,hitUv-texel*vec2(1,0)).rgb
        +texture(scene,hitUv+texel*vec2(0,1)).rgb+texture(scene,hitUv-texel*vec2(0,1)).rgb)*.125;
    float fresnel=.02+.98*pow(1.0-max(dot(n,-direction),0.0),5.0);
    float analyticSpecular=clamp((waterMask-.1)/.3,0.0,1.0)*waterSpecularScale;
    // Replace the material's white analytic stripe only where a valid screen
    // reflection exists; retain that fallback when the source goes offscreen.
    return max(background-vec3(analyticSpecular*confidence),vec3(0))+reflectedLight*fresnel*confidence;
}

void main() {
    vec4 source = texture(scene, screenPosition * 0.5 + 0.5);
    vec3 background = source.rgb;
    vec4 farPoint = inverseViewProjection * vec4(screenPosition, 1.0, 1.0);
    vec3 direction = normalize(farPoint.xyz - cameraPosition * farPoint.w);
    vec3 origin = cameraPosition - planetCenter;
    vec2 shell = sphereInterval(origin, direction, planetRadius * 1.025);
    float start = max(shell.x, 0.0);
    float finish = shell.y;
    float opaqueDepth = texture(sceneDepth, screenPosition * 0.5 + 0.5).r;
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
    float sourceEligibility=source.a;
    bool waterReceiver=source.a>=.099 && source.a<=.405 && opaqueDepth<.99999;
    if(earthClouds){
        // A shell approximation and a bounded volume share weather and optical
        // depth. Fade by altitude; never replace the weather map on descent.
        float altitudeKm=(length(origin)-planetRadius)*149597870.7;
        float volumeWeight=1.0-smoothstep(60.0,200.0,altitudeKm);
        vec2 cloudShell=sphereInterval(origin,direction,planetRadius+12000.0/149597870700.0);
        vec2 cloudBase=sphereInterval(origin,direction,planetRadius+2000.0/149597870700.0);
        float cloudBegin=max(0.0,cloudShell.x),cloudEnd=min(finish,cloudShell.y);
        if(cloudBase.x>cloudBegin)cloudEnd=min(cloudEnd,cloudBase.x);
        // From below the clouds, skip the empty cavity before entering the base.
        if(length(origin)<planetRadius+2000.0/149597870700.0)cloudBegin=max(cloudBegin,cloudBase.y);
        vec2 sheet=sphereInterval(origin,direction,planetRadius+5500.0/149597870700.0);
        float sheetDistance=sheet.x>0.0?sheet.x:sheet.y;
        float sheetT=1.0;vec3 sheetLight=vec3(0);
        if(sheetDistance>=start && sheetDistance<finish){
            vec3 p=origin+direction*sheetDistance;
            float grazing=max(.10,abs(dot(normalize(p),direction)));
            sheetT=exp(-weather(p)*2.8/grazing);
            vec3 sun=normalize(lightPosition-planetCenter-p);
            float illumination=smoothstep(-.12,.12,dot(normalize(p),sun));
            sheetLight=vec3(.83,.87,.94)*(.025+.975*illumination)*(1.0-sheetT);
        }
        vec3 volumeLight=vec3(0);float volumeT=1.0;
        if(volumeWeight>0.0 && cloudEnd>cloudBegin){
            cloudEnd=min(cloudEnd,cloudBegin+200000.0/149597870700.0);
            float coverage=weather(origin+direction*((cloudBegin+cloudEnd)*.5));
            float step=(cloudEnd-cloudBegin)/24.0;
            for(int i=0;i<24;++i){
                if(coverage<.03)break;
                vec3 p=origin+direction*(cloudBegin+(float(i)+.5)*step);
                float density=cloudDensity(p,coverage);if(density<.001)continue;
                vec3 sun=normalize(lightPosition-planetCenter-p);
                float shadow=cloudDensity(p+sun*(3500.0/149597870700.0),coverage)*4.0;
                float daylight=smoothstep(-.12,.10,dot(normalize(p),sun));
                float phase=.25+.65*pow(max(dot(direction,sun),0.0),8.0);
                vec3 lighting=vec3(.12,.17,.25)*(.08+.92*daylight)
                    +vec3(1.0,.96,.88)*daylight*exp(-shadow)*(.55+phase);
                float passing=exp(-density*step*149597870.7);
                volumeLight+=volumeT*(1.0-passing)*lighting;volumeT*=passing;
                if(volumeT<.01)break;
            }
            if(sheetDistance>cloudEnd){volumeLight+=volumeT*sheetLight;volumeT*=sheetT;}
        }
        float cloudT=mix(sheetT,volumeT,volumeWeight);
        background=background*cloudT+mix(sheetLight,volumeLight,volumeWeight);
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
        vec3 extinction=betaRayleigh*density+betaMie*haze*1.1+betaOzone*ozone;
        vec3 segment=exp(-extinction*interval);
        vec3 scatterCoefficient=betaRayleigh*density*rayleighPhase+betaMie*haze*miePhase;
        vec3 single=scatterCoefficient/max(extinction,vec3(1e-10))*sunTransmission*.30;
        // Bounded second-order approximation brightens the lower blue sky.
        vec3 multiple=vec3(.055,.095,.16)*(1.0-exp(-density*.6))*(.03+.97*illumination);
        scattering+=transmittance*(1.0-segment)*(single+multiple);
        transmittance *= segment;
    }
    if(waterReceiver)sourceEligibility=.1+(source.a-.1)*dot(transmittance,vec3(.2126,.7152,.0722));
    fragColor = vec4(background * transmittance + scattering, sourceEligibility);
}
)";
    program = compileShaderProgram(vertex, fragment);
    int linked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) {
        Release();
        return false;
    }
    glGenVertexArrays(1, &vao);
    glGenTextures(1, &sceneTexture);
    glGenTextures(1, &depthTexture);
    return true;
}

void Atmosphere::Render(const glm::mat4& viewProjection, const glm::vec3& camera,
                        const glm::vec3& center, float radius, bool logarithmicDepth,
                        const glm::vec3& lightPosition,bool earthClouds,double seconds,const glm::mat3& rotation,bool oceanReflections,float waterSpecularScale) const {
    int viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);
    if (viewport[2] <= 0 || viewport[3] <= 0)
        return;
    if(glm::length(center-camera)>radius*2) {
        const auto clip=viewProjection*glm::vec4(center,1);
        if(clip.w<=0)return;
        const float projectionScale=glm::length(glm::vec3(viewProjection[0][1],viewProjection[1][1],viewProjection[2][1]));
        const float screenRadius=radius*1.03f*projectionScale/clip.w;
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
    const auto inverse = glm::inverse(viewProjection);
    glUniformMatrix4fv(glGetUniformLocation(program, "inverseViewProjection"), 1, GL_FALSE, glm::value_ptr(inverse));
    glUniformMatrix4fv(glGetUniformLocation(program, "viewProjection"), 1, GL_FALSE, glm::value_ptr(viewProjection));
    glUniform3fv(glGetUniformLocation(program, "cameraPosition"), 1, glm::value_ptr(camera));
    glUniform3fv(glGetUniformLocation(program, "planetCenter"), 1, glm::value_ptr(center));
    glUniform3fv(glGetUniformLocation(program, "lightPosition"), 1, glm::value_ptr(lightPosition));
    glUniform1f(glGetUniformLocation(program, "planetRadius"), radius);
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
    glUniform1i(glGetUniformLocation(program, "logarithmicDepth"), logarithmicDepth);
    const bool blending=glIsEnabled(GL_BLEND);glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    if(oceanReflections && glm::length(center-camera)<radius*1.04f){
        // Reflect the finished weather/atmosphere, not the uncomposited sky.
        // This also includes the volume-cloud pass during a surface approach.
        glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,sceneTexture);
        glCopyTexSubImage2D(GL_TEXTURE_2D,0,0,0,viewport[0],viewport[1],textureWidth,textureHeight);
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
    program = vao = sceneTexture = depthTexture = 0;
    textureWidth = textureHeight = 0;
}

#include "glad/glad.h"
#include "BlackHole.h"
#include "GalacticCore.h"
#include "StellarPhotometry.h"
#include "OrbitalMechanics.h"
#include "PlanetTerrain.h"
#include "StarSystem.h"
#include "InterstellarWorlds.h"
#include "KerrSpacetime.h"

#include <cmath>
#include <algorithm>
#include <limits>
#include <cstddef>
#include <random>
#include <string>
#include <vector>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

unsigned int compileShaderProgram(const char* vertexSrc, const char* fragmentSrc);

namespace {
    constexpr double gravitationalParameter = Astronomy::gargantuaGravityAu;
    constexpr float horizonRadius = static_cast<float>(Astronomy::gargantuaHorizonAu);
    constexpr float innerDiskRadius = 3.0f * horizonRadius;
    constexpr float outerDiskRadius = horizonRadius * (1.03f/.24f);
    constexpr double distanceScale = Astronomy::gargantuaHorizonAu / .24;
    const double timeScale = std::sqrt(distanceScale*distanceScale*distanceScale*1.8/gravitationalParameter);
    const double boosterAcceleration=InterstellarWorlds::EngineAccelerationMetres/Astronomy::metresPerAu;
    constexpr float craftScale = static_cast<float>(25.0/Astronomy::metresPerAu/.045);

    enum Surface { Planet = 2, Star = 3 };

    struct Body {
        OrbitalMechanics::Orbit orbit;
        float radius;
        glm::vec3 color;
        Surface surface;
        float temperature;
    };

    const Body bodies[] = {
        {InterstellarWorlds::Get(0).orbit,static_cast<float>(InterstellarWorlds::Get(0).radiusEarth*Astronomy::earthRadiusAu),InterstellarWorlds::Get(0).color,Planet,0},
        {InterstellarWorlds::Get(1).orbit,static_cast<float>(InterstellarWorlds::Get(1).radiusEarth*Astronomy::earthRadiusAu),InterstellarWorlds::Get(1).color,Planet,0},
        {InterstellarWorlds::Get(2).orbit,static_cast<float>(InterstellarWorlds::Get(2).radiusEarth*Astronomy::earthRadiusAu),InterstellarWorlds::Get(2).color,Planet,0},
        {InterstellarWorlds::CompanionOrbit(0), static_cast<float>(Astronomy::solarRadiusAu), {1.0f, 0.77f, 0.45f}, Star, 5200.0f},
        {InterstellarWorlds::CompanionOrbit(1), static_cast<float>(Astronomy::solarRadiusAu*2), {0.64f, 0.80f, 1.0f}, Star, 10000.0f},
        {InterstellarWorlds::CompanionOrbit(2), static_cast<float>(Astronomy::solarRadiusAu*.65), {1.0f, 0.52f, 0.36f}, Star, 3500.0f},
        {InterstellarWorlds::CompanionOrbit(3), static_cast<float>(Astronomy::solarRadiusAu*1.2), {1.0f, 0.92f, 0.77f}, Star, 6200.0f},
        {InterstellarWorlds::CompanionOrbit(4), static_cast<float>(Astronomy::solarRadiusAu*1.8), {0.68f, 0.81f, 1.0f}, Star, 8500.0f},
    };

    glm::mat4 DiskOrientation() {
        return glm::mat4(glm::mat3(InterstellarWorlds::Orientation()));
    }

    glm::mat4 AttitudeAlong(const glm::vec3& direction) {
        const auto forward = glm::normalize(direction);
        const glm::vec3 referenceUp = std::abs(forward.y) < 0.95f
            ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
        const auto right = glm::normalize(glm::cross(referenceUp, forward));
        glm::mat4 orientation(1.0f);
        orientation[0] = glm::vec4(right, 0.0f);
        orientation[1] = glm::vec4(glm::cross(forward, right), 0.0f);
        orientation[2] = glm::vec4(forward, 0.0f);
        return orientation;
    }

    const char* vertexBody = R"(
layout (location = 0) in vec3 position;
layout (location = 1) in vec3 packetColor;
layout (location = 2) in float stellarTemperature;
layout (location = 3) in float stellarLuminosity;
layout (location = 4) in vec3 positionLow;
uniform vec3 cameraLow;
uniform bool stellarPackets;
uniform vec3 detectorHigh,detectorLow;
uniform float stellarSeconds;
uniform float limitingMagnitude;
uniform mat4 model;
uniform bool patchEnabled;
uniform mat4 viewProjection;
uniform float wavePhase;
uniform int surface;
uniform bool oceanWorld;
out vec3 localPosition;
out vec3 worldPosition;
out vec3 relativePosition;
out vec3 patchDeltaMetres;
uniform vec3 cameraPosition;
out vec3 photonColor;
out float sourceTemperature;
out float sourceLuminosity;

void main() {
    patchDeltaMetres=vec3(0);
    if(surface==2 && patchDrawing){
        localPosition=surfacePatchDirection(position.xy);
        patchDeltaMetres=surfacePatchDelta(position.xy,interstellarElevation(localPosition,wavePhase));
        relativePosition=surfacePatchRelative(patchDeltaMetres);
        worldPosition=relativePosition+cameraPosition;
        photonColor=vec3(0);sourceTemperature=0;sourceLuminosity=0;
        gl_Position=viewProjection*vec4(relativePosition,1);return;
    }
    vec3 displaced = position;
    if (surface == 2) {
        displaced=position*(1.0+interstellarElevation(position,wavePhase));
    }
    vec4 world = model * vec4(displaced, 1.0);
    localPosition = position;
    relativePosition = world.xyz;
    worldPosition = world.xyz + cameraPosition;
    photonColor = packetColor;
    sourceTemperature=stellarTemperature;
    sourceLuminosity=stellarLuminosity;
    gl_Position = viewProjection * world;
    if(surface==7){
        relativePosition=(position-cameraPosition)+(positionLow-cameraLow);
        if(!stellarPackets){
            float pc=max(length(relativePosition)/206264.806247,1e-12);
            float magnitude=4.74-2.5*log(max(sourceLuminosity,1e-12))/log(10.0)+5.0*log(pc/10.0)/log(10.0);
            float starVisibility=1.0-smoothstep(limitingMagnitude-.2,limitingMagnitude+.2,magnitude);
            if(starVisibility<=0.0){gl_Position=vec4(2,2,2,1);return;}
        }
        if(stellarPackets){
            vec3 toDetector=(detectorHigh-position)+(detectorLow-positionLow);
            float duration=max(length(toDetector)/0.0020039888,.001);
            float fraction=fract(stellarSeconds/duration+float(gl_InstanceID)*.25);
            relativePosition+=toDetector*fraction;
        }else if(length(relativePosition)<3.0){gl_Position=vec4(2,2,2,1);return;}
        gl_Position=viewProjection*vec4(relativePosition,1);
        gl_Position.z=gl_Position.w*.999999;
    }
}
)";

    const char* fragmentBody = R"(
in vec3 localPosition;
in vec3 worldPosition;
in vec3 relativePosition;
in vec3 patchDeltaMetres;
in vec3 photonColor;
in float sourceTemperature;
in float sourceLuminosity;
out vec4 fragColor;
uniform int surface;
uniform bool oceanWorld;
uniform vec3 bodyColor;
uniform vec3 cameraPosition;
uniform vec3 velocityBeta;
uniform float temperature;
uniform bool stellarPackets;
uniform float wavePhase;
uniform mat4 model;
uniform bool patchEnabled;
uniform bool physicalObserver;
uniform sampler2D stellarCorona;
uniform float limitingMagnitude;

void main() {
    gl_FragDepth=surface==7?0.999999:clamp(log2(1.0+length(relativePosition)*1e12)/log2(1.0+20000.0*1e12),0.0,.999998);
    if (surface == 2) {
        vec3 radial = normalize(localPosition);
        if(patchEnabled && ((dot(radial,patchCenter)>cos(.020))!=patchDrawing))discard;
        vec3 surfacePosition=radial*(1.0+interstellarElevation(radial,wavePhase));
        vec3 localNormal=patchDrawing?normalize(cross(dFdx(patchDeltaMetres),dFdy(patchDeltaMetres))):normalize(cross(dFdx(surfacePosition),dFdy(surfacePosition)));
        vec3 material=bodyColor;
        if(!oceanWorld){
            float fractures=abs(sin(dot(radial,vec3(31,19,43)))+sin(dot(radial,vec3(73,-41,17))));
            if(interstellarPlanet==1)material=mix(vec3(.18,.32,.42),vec3(.82,.88,.91),smoothstep(.06,.5,fractures));
            else material*=.7+.3*sin(radial.x*36.0+sin(radial.z*17.0));
        }
        if(oceanWorld) {
            localNormal=radial;
            vec3 reference=abs(radial.y)<.9?vec3(0,1,0):vec3(1,0,0);
            vec3 tangent=normalize(cross(reference,radial)),bitangent=cross(radial,tangent);
            vec2 ripple=vec2(dot(radial,vec3(1,.31,.17)),dot(radial,vec3(.19,1,.47)))*500000.0;
            float fade=1.0-smoothstep(.25,1.0,max(fwidth(ripple.x),fwidth(ripple.y)));
            localNormal=normalize(localNormal+.018*fade*(tangent*sin(ripple.x+wavePhase)+bitangent*cos(ripple.y-wavePhase*.7)));
        }
        if (dot(localNormal, radial) < 0.0)
            localNormal = -localNormal;
        groundDetail(material,localNormal,patchDeltaMetres,oceanWorld?1.0:0.0,length(relativePosition)*149597870700.0,wavePhase);
        vec3 normal = normalize(mat3(transpose(inverse(model))) * localNormal);
        vec3 lightDirection = normalize(-worldPosition);
        vec3 viewDirection = normalize(-relativePosition);
        float light = max(dot(normal, lightDirection), 0.0);
        float specular = pow(max(dot(normal, normalize(lightDirection + viewDirection)), 0.0), 100.0)*(oceanWorld?1.0:interstellarPlanet==1?.3:.03);
        if(oceanWorld) {
            float fresnel=.02+.98*pow(1.0-max(dot(normal,viewDirection),0.0),5.0);
            vec3 ocean=mix(vec3(.006,.025,.065),vec3(.045,.10,.15),fresnel);
            fragColor=vec4(ocean*(.03+.97*light)+specular*.65,0);
            return;
        }
        fragColor=vec4(material*(.035+.965*light)+specular,0);
    } else if (surface == 3) {
        float shift = physicalObserver?1.0:frequencyRatio(length(worldPosition), length(cameraPosition),
            velocityBeta, normalize(-relativePosition));
        vec3 radial=normalize(localPosition);
        float mu=dot(normalize(mat3(model)*radial),normalize(-relativePosition));
        fragColor = vec4(stellarSurface(radial,blackbody(temperature * shift),mu,temperature*shift), 1.0);
    } else if (surface == 8) {
        fragColor = vec4(photonColor, 1.0);
    } else if (surface == 7) {
        if(stellarPackets){fragColor=vec4(.35,.65,.85,1);return;}
        float seed = fract(sin(dot(localPosition, vec3(12.9898, 78.233, 37.719))) * 43758.5453);
        float distanceSquared = dot(relativePosition,relativePosition);
        vec2 psf=gl_PointCoord*2.0-1.0;
        float pointRadius=dot(psf,psf);
        if(pointRadius>1.0)discard;
        // A display visibility floor keeps the distant navigation catalogue
        // visible; the ceiling prevents subpixel points overwhelming nearby disks.
        float pc=max(sqrt(distanceSquared)/206264.806247,1e-12);
        float magnitude=4.74-2.5*log(max(sourceLuminosity,1e-12))/log(10.0)+5.0*log(pc/10.0)/log(10.0);
        float starVisibility=1.0-smoothstep(limitingMagnitude-.2,limitingMagnitude+.2,magnitude);
        float flux=clamp(2.0*pow(10.0,-.4*(magnitude-4.0)),.03,40.0)*starVisibility;
        float angle=seed*6.2831853,c=cos(angle),s=sin(angle);
        vec4 corona=texture(stellarCorona,mat2(c,-s,s,c)*psf*.5+.5);
        float core=exp(-pointRadius*35.0)*4.0;
        float streamers=dot(corona.rgb,vec3(.2126,.7152,.0722))*corona.a*.12;
        vec3 tint = blackbody(max(sourceTemperature,2500.0));
        tint/=max(max(tint.r,tint.g),max(tint.b,.00001));
        fragColor = vec4(tint * flux*(core+streamers),pointRadius<.08?1.0:0.0);
    } else if (surface == 5) {
        // AU-scale derivatives underflow when crossed/squared on metre-sized
        // craft. Compute the face normal in metres before normalization.
        vec3 faceNormal=cross(dFdx(relativePosition*149597870700.0),dFdy(relativePosition*149597870700.0));
        vec3 normal=faceNormal*inversesqrt(max(dot(faceNormal,faceNormal),1e-16));
        vec3 viewDirection = normalize(-relativePosition);
        if (dot(normal, viewDirection) < 0.0)
            normal = -normal;
        vec3 lightDirection = normalize(vec3(1.0, 1.0, 2.0) - worldPosition);
        float diffuse = max(dot(normal, lightDirection), 0.0);
        vec3 halfVector=lightDirection+viewDirection;
        float specular=dot(halfVector,halfVector)>1e-12?pow(max(dot(normal,normalize(halfVector)),0.0),24.0):0.0;
        fragColor = vec4(bodyColor * (0.22 + 0.78 * diffuse) + 0.5 * specular, 1.0);
    } else if (surface == 6) {
        vec3 normal = normalize(mat3(transpose(inverse(model))) * localPosition);
        float facing = max(dot(normal, normalize(-relativePosition)), 0.0);
        float head = clamp(0.5 + 0.5 * localPosition.z, 0.0, 1.0);
        vec3 flame = mix(vec3(0.02, 0.12, 0.8), vec3(0.8, 0.95, 1.0), head);
        fragColor = vec4(flame, 0.8 * pow(facing, 0.7) * smoothstep(0.0, 0.5, head));
    } else {
        fragColor = vec4(bodyColor, 1.0);
    }
}
)";

    const char* screenVertexSource = R"(
#version 330 core
out vec2 screenPosition;
void main() {
    vec2 corner = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    screenPosition = corner * 2.0 - 1.0;
    gl_Position = vec4(screenPosition, 0.0, 1.0);
}
)";

    const std::string lensFragmentBody = std::string(R"(
in vec2 screenPosition;
out vec4 fragColor;
uniform mat4 inverseViewProjection;
uniform mat4 viewProjection;
uniform mat4 inverseDiskOrientation;
uniform vec3 cameraPosition;
uniform vec3 diskAxis;
uniform vec2 diskRadii;
uniform float diskPhase;
uniform float plasmaTime;
uniform float diskEmission,diskTemperature;
uniform bool quasarJet;
uniform int accretionModel;
uniform bool hotFlowDiagnostic;
uniform float flowTime;
uniform sampler2D sourceScene;
uniform sampler2D sourceDepth;
uniform samplerCube sky;
uniform float blackHoleSpin;
uniform int rayStepBudget;
uniform bool lensingDiagnostic;
uniform float captureRadius;
uniform float captureStopRadius;
uniform bool physicalObserver;
uniform vec3 physicalObserverBeta;

float depthDistance(float depth) {
    return (exp2(depth*log2(1.0+20000.0*1e12))-1.0)/1e12;
}

// Analytic integration of the Gaussian vertical slab avoids tiny steps
// asymptotically approaching its plane and consuming the ray budget.
vec3 raySegmentPoint(vec3 start,vec3 end,vec3 initial,vec3 final,float interval,float t){
    float t2=t*t,t3=t2*t;
    return (2.0*t3-3.0*t2+1.0)*start+(t3-2.0*t2+t)*interval*initial
        +(-2.0*t3+3.0*t2)*end+(t3-t2)*interval*final;
}
float gaussianErf(float x){
    float a=abs(x),t=1.0/(1.0+.3275911*a);
    float polynomial=(((((1.061405429*t-1.453152027)*t)+1.421413741)*t-.284496736)*t+.254829592)*t;
    return sign(x)*(1.0-polynomial*exp(-a*a));
}
float slabAverage(float before,float after,float sigma){
    float delta=after-before;
    if(abs(delta)<sigma*.001){float q=(before+after)*.5/sigma;return exp(-.5*q*q);}
    float scale=1.41421356237*sigma;
    return clamp(1.25331413732*sigma*(gaussianErf(after/scale)-gaussianErf(before/scale))/delta,0.0,1.0);
}

float hash13(vec3 p) {
    p = fract(p * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z);
}

float valueNoise(vec3 p) {
    vec3 cell = floor(p);
    vec3 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float n000 = hash13(cell + vec3(0.0, 0.0, 0.0));
    float n100 = hash13(cell + vec3(1.0, 0.0, 0.0));
    float n010 = hash13(cell + vec3(0.0, 1.0, 0.0));
    float n110 = hash13(cell + vec3(1.0, 1.0, 0.0));
    float n001 = hash13(cell + vec3(0.0, 0.0, 1.0));
    float n101 = hash13(cell + vec3(1.0, 0.0, 1.0));
    float n011 = hash13(cell + vec3(0.0, 1.0, 1.0));
    float n111 = hash13(cell + vec3(1.0, 1.0, 1.0));
    return mix(mix(mix(n000, n100, f.x), mix(n010, n110, f.x), f.y),
               mix(mix(n001, n101, f.x), mix(n011, n111, f.x), f.y), f.z);
}

float fbm(vec3 p) {
    float value = 0.0;
    float amplitude = 0.55;
    for (int octave = 0; octave < 4; ++octave) {
        value += amplitude * valueNoise(p);
        p = p * 2.07 + vec3(11.3, 7.7, 5.1);
        amplitude *= 0.48;
    }
    return value;
}

)") + R"(
void main() {
    vec4 farPoint = inverseViewProjection * vec4(screenPosition, 1.0, 1.0);
    vec3 cameraDirection = normalize(farPoint.xyz);
    vec3 direction = physicalObserver?cameraDirection:observerRayDirection(cameraDirection);
    bool spinning=abs(blackHoleSpin)>.0001 || physicalObserver;
    mat3 toDisk=mat3(inverseDiskOrientation),fromDisk=transpose(toDisk);
    vec3 kx=toDisk*cameraPosition,kp=vec3(0);
    float pt=1.0;
    if(spinning){
        if(physicalObserver)kerrObserverPastRay(kx,toDisk*direction,horizonRadius,blackHoleSpin,toDisk*physicalObserverBeta,kp,pt);
        else kerrPastRay(kx,toDisk*direction,horizonRadius,blackHoleSpin,kp,pt);
    }
    float angularZ=cross(kx,kp).z;
    vec2 originalUv=screenPosition*.5+.5;
    float originalDepth=texture(sourceDepth,originalUv).r;
    const float localSceneRadius=1000000.0/149597870700.0;
    float backgroundStart=max(localSceneRadius,length(cameraPosition)-3.0*horizonRadius);
    if(originalDepth<.99999 && depthDistance(originalDepth)<backgroundStart) {
        fragColor=texture(sourceScene,originalUv);gl_FragDepth=originalDepth;return;
    }
    float observerRadius = length(cameraPosition);
    bool insideHorizon = (spinning?kerrRadius(kx,.5*horizonRadius*blackHoleSpin):observerRadius) < captureRadius;
    // Rays whose impact parameters exceed the disk's apparent extent miss the scene.
    if (!physicalObserver && dot(cameraPosition, direction) > 0.0 && observerRadius > diskRadii.y*1.5)
        discard;
    if (!physicalObserver && length(cross(cameraPosition, direction)) > diskRadii.y*1.5)
        discard;

    vec3 position = cameraPosition;
    vec3 tangent = photonInitialTangent(position, direction);
    if(spinning){vec3 initialDx,initialDp;kerrDerivative(kx,kp,pt,horizonRadius,blackHoleSpin,initialDx,initialDp);tangent=fromDisk*initialDx;}
    vec3 momentum = cross(position, tangent);
    float angularMomentumSquared = dot(momentum, momentum);
    float travelled = 0.0;
    bool hit = false;
    int rayResult=0; // unresolved, captured, disk, escaped
    vec3 radiance = vec3(0.0);
    float opticalEligibility = 1.0;
    vec3 plasma = vec3(0.0);
    float flowTransmission=1.0;
    int effectiveBudget=rayStepBudget;
    bool usedNearHoleReserve=false;
    // Retain actual-horizon tracing where a nearby finite emitter may sit
    // inside the otherwise empty capture region.
    float stopRadius=originalDepth<.99999 && depthDistance(originalDepth)<observerRadius+3.0*horizonRadius
        ?captureRadius*.9999:captureStopRadius;

    for (int step = 0; step < 384; ++step) {
        float radius = length(position);
        float metricRadius=spinning?kerrRadius(kx,.5*horizonRadius*blackHoleSpin):radius;
        if(step>=effectiveBudget){
            // Near-extremal past rays need extra affine steps. Reserve work
            // only for rays already inside the disk's inner edge.
            if(!usedNearHoleReserve && spinning && metricRadius<diskRadii.x && effectiveBudget<384){
                effectiveBudget=min(384,rayStepBudget+192);usedNearHoleReserve=true;
            }else break;
        }
        if (!insideHorizon && metricRadius < stopRadius) {
            hit = true;rayResult=1;
            break;
        }
        float innerLimit=max(horizonRadius*.03,.5*horizonRadius*(1.0-sqrt(1.0-blackHoleSpin*blackHoleSpin))*1.05);
        if (insideHorizon && metricRadius < innerLimit) {
            hit = true;rayResult=1;
            break;
        }
        if (radius > max(observerRadius, diskRadii.y*1.5) + 4.0 && dot(position, tangent) > 0.0) {
            rayResult=3;break;
        }

        float interval = clamp(radius * (spinning?.06:.055), 0.003, spinning?(accretionModel==1?2.0:1.0):.45);
        // AU-sized fixed steps exhausted the budget before a ray from Mann's
        // distance ever reached Gargantua. Resolve the weak-field approach
        // proportionally, retaining fine steps around the disk and photon shell.
        float weakFieldStart=accretionModel==1 || quasarJet?diskRadii.y*2.0:horizonRadius*(2.0*1.03/.24);
        if(radius>weakFieldStart)interval=max(interval,radius*(spinning?.18:.10));
        // Geometry controls the step; the emitting slab is integrated per segment.

        vec3 nextPosition,nextTangent;
        if(spinning){
            vec3 dx1,dp1,dx2,dp2,dx3,dp3,dx4,dp4;
            kerrDerivative(kx,kp,pt,horizonRadius,blackHoleSpin,dx1,dp1);
            interval/=max(1.0,length(dx1));
            kerrDerivative(kx+.5*interval*dx1,kp+.5*interval*dp1,pt,horizonRadius,blackHoleSpin,dx2,dp2);
            kerrDerivative(kx+.5*interval*dx2,kp+.5*interval*dp2,pt,horizonRadius,blackHoleSpin,dx3,dp3);
            kerrDerivative(kx+interval*dx3,kp+interval*dp3,pt,horizonRadius,blackHoleSpin,dx4,dp4);
            kx+=interval*(dx1+2.0*dx2+2.0*dx3+dx4)/6.0;
            kp+=interval*(dp1+2.0*dp2+2.0*dp3+dp4)/6.0;
            vec3 dx,dp;kerrDerivative(kx,kp,pt,horizonRadius,blackHoleSpin,dx,dp);
            nextPosition=fromDisk*kx;nextTangent=fromDisk*dx;
        }else{
            vec3 acceleration=photonAcceleration(position,angularMomentumSquared);
            nextPosition=position+interval*tangent+.5*interval*interval*acceleration;
            nextTangent=tangent+.5*interval*(acceleration+photonAcceleration(nextPosition,angularMomentumSquared));
        }

        if(any(isnan(nextPosition)) || any(isinf(nextPosition)) || any(isnan(nextTangent)) || any(isinf(nextTangent))){
            hit=true;rayResult=0;opticalEligibility=0.0;break;
        }
        vec3 segment = nextPosition - position;
        float segmentLength = length(segment);
        vec3 midpoint = position + 0.5 * segment;
        float midRadius = length(midpoint);
        if(spinning)midRadius=kerrRadius(toDisk*midpoint,.5*horizonRadius*blackHoleSpin);
        float midFraction = (midRadius - diskRadii.x) / (diskRadii.y - diskRadii.x);
        float midPlane = dot(midpoint, diskAxis);
)" + R"(
        // Reference-inspired polar outflow: an artistic quasar preset, not
        // a resolved measurement of TON 618's jet geometry.
        if(quasarJet){
            vec3 p=toDisk*midpoint/horizonRadius;
            float axial=abs(p.z),radial=length(p.xy),width=.10+axial*.28;
            float edge=smoothstep(.7,1.4,axial)*(1.0-smoothstep(10.0,16.0,axial));
            float density=exp(-radial*radial/(width*width))*edge;
            density*=.6+.4*valueNoise(vec3(radial*28.0,axial*6.0-plasmaTime*.4,0));
            plasma+=blackbody(12500.0/(1.0+axial*.14))*density*segmentLength/horizonRadius*diskEmission*2.0;
        }
        if(accretionModel==1){
            // Original RIAF-style illustration, not a GRMHD/measured spectrum.
            vec3 p=toDisk*midpoint/horizonRadius;
            float rho=length(p.xy),height=abs(p.z),inner=diskRadii.x/horizonRadius;
            if(rho<25.0 && height<22.0){
                float envelope=smoothstep(inner*.85,inner*1.3,rho)*(1.0-smoothstep(17.0,24.0,rho));
                float thickness=.25+.38*rho;
                float density=envelope*exp(-.5*p.z*p.z/(thickness*thickness))*pow(max(rho/inner,1.0),-1.1);
                float cone=.65+height*.45,width=.45+height*.15;
                float wind=exp(-pow((rho-cone)/width,2.0)-height/9.0)*smoothstep(1.5,3.0,height)*(1.0-smoothstep(16.0,22.0,height));
                if(density+wind>.0001){
                    float emissionTime=flowTime-2.0*(travelled+.5*segmentLength)/horizonRadius;
                    float angle=atan(p.y,p.x)-emissionTime/(pow(max(2.0*rho,3.0),1.5)+blackHoleSpin);
                    vec3 cloudPoint=vec3(cos(angle),sin(angle),p.z*.45)*(2.0+rho*.7);
                    float turbulent=fbm(cloudPoint+vec3(0,0,-emissionTime*.012));
                    density=(density+.13*wind)*mix(.2,2.2,smoothstep(.18,.76,turbulent));
                    float shift=spinning?kerrDiskShift(max(midRadius,diskRadii.x*1.05),horizonRadius,blackHoleSpin,pt,angularZ,vec3(0),direction):1.0;
                    float temperature=diskTemperature*pow(max(midRadius/diskRadii.x,1.0),-.7);
                    // Normalize the hot-plasma tint. Treating billion-K gas
                    // as an optically thick optical blackbody blows out the sky.
                    vec3 spectrum=blackbody(min(40000.0,temperature*shift));
                    spectrum/=max(max(spectrum.r,spectrum.g),max(spectrum.b,.00001));
                    float innerHeat=exp(-max(0.0,midRadius/diskRadii.x-1.0)*.45);
                    vec3 tint=mix(spectrum,vec3(1),innerHeat*.35)*vec3(.7,1.0,1.7);
                    float path=segmentLength/horizonRadius,tau=density*path*.004,passing=exp(-tau);
                    float integral=tau<.0001?1.0:(1.0-passing)/tau;
                    float emissivity=density*pow(max(midRadius/diskRadii.x,1.0),-.45)*pow(clamp(shift,.1,4.0),3.0);
                    plasma+=flowTransmission*tint*emissivity*path*integral*diskEmission*1.8;
                    flowTransmission*=passing;
                }
            }
        }
        // Other presets retain their existing thin-disk atmosphere.
        if (accretionModel==0 && midFraction > 0.0 && midFraction < 1.0) {
            float flare = 0.012 + 0.065 * pow(midFraction, 1.35);
            float vertical = slabAverage(dot(position,diskAxis),dot(nextPosition,diskAxis),flare);
            if (vertical > 0.02) {
                vec3 diskPosition = (inverseDiskOrientation * vec4(midpoint, 1.0)).xyz;
                float cloudAngle = diskPhase * 0.35;
                float cloudCos = cos(cloudAngle);
                float cloudSin = sin(cloudAngle);
                vec2 rotated = vec2(cloudCos * diskPosition.x - cloudSin * diskPosition.y,
                                    cloudSin * diskPosition.x + cloudCos * diskPosition.y);
                vec3 cloudPosition = vec3(rotated * 7.5, diskPosition.z * 26.0)
                    + vec3(0.0, 0.0, plasmaTime * 0.3);
                float turbulence = fbm(cloudPosition);
                float clumps = smoothstep(0.30, 0.78, turbulence);
                float density = vertical * (0.20 + 1.8 * clumps);
                vec3 toObserver = -localPhotonDirection(midpoint, tangent);
                float shift = clamp(frequencyRatio(midRadius, observerRadius,
                    circularVelocityBeta(midpoint, diskAxis), toObserver), 0.05, 6.0);
                if(spinning)shift=kerrDiskShift(kerrRadius(toDisk*midpoint,.5*horizonRadius*blackHoleSpin),horizonRadius,blackHoleSpin,pt,angularZ,physicalObserver?vec3(0):observerBeta,direction);
                float temperature = diskTemperature * pow(max(midRadius / diskRadii.x, 1.0), -0.75);
                float beaming = pow(shift, physicalObserver?3.0:3.2);
                plasma += blackbody(temperature * shift) * beaming * density * segmentLength * 0.55 * diskEmission;
            }
        }

        float planeBefore = dot(position, diskAxis);
        float planeAfter = dot(nextPosition, diskAxis);
        if (accretionModel==0 && planeBefore * planeAfter < 0.0) {
            float lower=0.0,upper=1.0;
            // Locate the plane event on the curved RK segment, rather than
            // using a chord whose radius jumps when the step count changes.
            for(int refine=0;refine<12;++refine){
                float t=(lower+upper)*.5;
                float height=dot(raySegmentPoint(position,nextPosition,tangent,nextTangent,interval,t),diskAxis);
                if((height>0.0)==(planeBefore>0.0))lower=t;else upper=t;
            }
            float fraction=(lower+upper)*.5;
            vec3 intersection=raySegmentPoint(position,nextPosition,tangent,nextTangent,interval,fraction);
            float diskRadius = length(intersection);
            if(spinning)diskRadius=kerrRadius(toDisk*intersection,.5*horizonRadius*blackHoleSpin);
            if (diskRadius > diskRadii.x && diskRadius < diskRadii.y) {
                vec3 diskPosition = (inverseDiskOrientation * vec4(intersection, 1.0)).xyz;
                float radial = (diskRadius - diskRadii.x) / (diskRadii.y - diskRadii.x);
                float angle = atan(diskPosition.y, diskPosition.x) - diskPhase;
                float sheared=angle+.7*log(diskRadius/diskRadii.x)-plasmaTime*.16/pow(diskRadius/diskRadii.x,1.5);
                float turbulent=fbm(vec3(vec2(cos(sheared),sin(sheared))*(3.0+radial*9.0),radial*38.0));
                float bands=.82+.18*sin(22.0*radial+9.0*turbulent);
                float fineBands=.40+1.25*smoothstep(.24,.76,turbulent);
                float spiral=.85+.15*cos(6.0*angle+18.0*radial+4.0*turbulent);
                vec3 toObserver = -localPhotonDirection(intersection, mix(tangent, nextTangent, fraction));
                float shift = clamp(frequencyRatio(diskRadius, observerRadius,
                    circularVelocityBeta(intersection, diskAxis), toObserver), 0.05, 6.0);
                if(spinning)shift=kerrDiskShift(kerrRadius(toDisk*intersection,.5*horizonRadius*blackHoleSpin),horizonRadius,blackHoleSpin,pt,angularZ,physicalObserver?vec3(0):observerBeta,direction);
                float temperature = diskTemperature * pow(diskRadius / diskRadii.x, -0.75);
                float edge = smoothstep(0.0, 0.06, radial) * (1.0 - smoothstep(0.82, 1.0, radial));
                float beaming = pow(shift, physicalObserver?3.0:3.6);
                radiance = 3.2 * diskEmission * blackbody(temperature * shift) * beaming * edge
                    * (0.4 + 0.3 * bands + 0.3 * spiral) * fineBands;
                travelled += length(intersection - position);
                hit = true;rayResult=2;
                break;
            }
        }
        travelled += segmentLength;
        position = nextPosition;
        tangent = nextTangent;
    }
    // Exhausting the integrator is not evidence that a ray reached infinity.
    if(!hit && rayResult!=3){hit=true;opticalEligibility=0.0;}
    if(!hit) {
        vec3 incoming=normalize(tangent);
        vec4 skySample=texture(sky,incoming);
        radiance=skySample.rgb;opticalEligibility=skySample.a;
        vec4 mapped=viewProjection*vec4(incoming,0);
        if(mapped.w>0) {
            vec2 mappedUv=mapped.xy/mapped.w*.5+.5;
            if(min(mappedUv.x,mappedUv.y)>0 && max(mappedUv.x,mappedUv.y)<1) {
                float mappedDepth=texture(sourceDepth,mappedUv).r;
                if(mappedDepth>=.99999 || depthDistance(mappedDepth)>backgroundStart) {
                    vec4 source=texture(sourceScene,mappedUv);
                    radiance=source.rgb;
                    opticalEligibility=source.a;
                }
            }
        }
        if(!physicalObserver){
            float influence=1.0-smoothstep(diskRadii.y*1.1,diskRadii.y*1.5,length(cross(cameraPosition,direction)));
            vec4 original=texture(sourceScene,originalUv);
            radiance=mix(original.rgb,radiance,influence);
            opticalEligibility=mix(original.a,opticalEligibility,influence);
        }
        if(physicalObserver){
            // The ray was normalized to unit received energy in the camera
            // tetrad. A stationary source at infinity measures pt. RGB sky
            // intensities use the specific-radiance g^3 invariant; finite-body
            // emission velocities and galaxy spectra remain approximations.
            float shift=pt>0.0?clamp(1.0/pt,.02,32.0):0.0;
            radiance*=shift*shift*shift;
        }
    } else if(originalDepth<.99999 && depthDistance(originalDepth)<travelled) {
        fragColor=texture(sourceScene,originalUv);gl_FragDepth=originalDepth;return;
    }
    // Preserve stellar-plasma exclusion through lensing; the accretion disk's
    // own emissive plasma remains an eligible optical light source.
    // Keep extreme blueshift within finite RGBA16F storage; this is a numeric
    // target limit, not an event-horizon blackout or a change in ray geometry.
    if(accretionModel==1 && dot(plasma,vec3(.2126,.7152,.0722))>.0001)opticalEligibility=1.0;
    fragColor = vec4(clamp(radiance*flowTransmission + plasma,vec3(0),vec3(60000)), opticalEligibility);
    if(hotFlowDiagnostic && accretionModel==1)fragColor=vec4(vec3(flowTransmission),0);
    if(lensingDiagnostic)fragColor=vec4(rayResult==1?vec3(1,0,0):rayResult==2?vec3(0,1,0):rayResult==3?vec3(0,0,1):vec3(1,0,1),0);
    // Approximate depth allows the rasterized orbiting bodies to cross the lensed disk.
    gl_FragDepth = hit?clamp(log2(1.0+travelled*1e12)/log2(1.0+20000.0*1e12),0.0,.999998):.999999;
}
)";

    glm::vec3 SpherePoint(float latitude, float longitude) {
        const float ring = std::cos(latitude);
        return {ring * std::cos(longitude), ring * std::sin(longitude), std::sin(latitude)};
    }

    std::vector<glm::vec3> MakeSphere() {
        constexpr int latitudeSegments = 64;
        constexpr int longitudeSegments = 96;
        std::vector<glm::vec3> vertices;
        for (int row = 0; row < latitudeSegments; ++row) {
            const float lower = -glm::half_pi<float>() + glm::pi<float>() * row / latitudeSegments;
            const float upper = lower + glm::pi<float>() / latitudeSegments;
            for (int col = 0; col < longitudeSegments; ++col) {
                const float left = glm::two_pi<float>() * col / longitudeSegments;
                const float right = glm::two_pi<float>() * (col + 1) / longitudeSegments;
                const auto a = SpherePoint(lower, left);
                const auto b = SpherePoint(lower, right);
                const auto c = SpherePoint(upper, right);
                const auto d = SpherePoint(upper, left);
                vertices.insert(vertices.end(), {a, b, c, a, c, d});
            }
        }
        return vertices;
    }

    std::vector<glm::vec3> MakeCraftRing() {
        std::vector<glm::vec3> vertices;
        const auto point = [](int ring, int tube) {
            const float angle = glm::two_pi<float>() * ring / 48.0f;
            const float section = glm::two_pi<float>() * tube / 8.0f;
            const float radius = 0.045f + 0.005f * std::cos(section);
            return glm::vec3(radius * std::cos(angle), radius * std::sin(angle), 0.005f * std::sin(section));
        };
        for (int ring = 0; ring < 48; ++ring) {
            for (int tube = 0; tube < 8; ++tube) {
                vertices.insert(vertices.end(), {point(ring, tube), point(ring + 1, tube),
                    point(ring + 1, tube + 1), point(ring, tube), point(ring + 1, tube + 1), point(ring, tube + 1)});
            }
        }
        return vertices;
    }

}

bool BlackHole::Initialize() {
    const std::string vertexSource = std::string("#version 330 core\n")
        + PlanetTerrain::ShaderFunctions() + InterstellarWorlds::ShaderFunctions() + vertexBody;
    const std::string fragmentSource = std::string("#version 330 core\nuniform float horizonRadius;\nuniform vec3 observerBeta;\n")
        + OrbitalMechanics::RelativityShaderFunctions() + PlanetTerrain::ShaderFunctions() + PlanetTerrain::SurfaceDetailShaderFunctions() + InterstellarWorlds::ShaderFunctions() + StellarActivity::ShaderFunctions() + fragmentBody;
    program = compileShaderProgram(vertexSource.c_str(), fragmentSource.c_str());
    const std::string lensSource = std::string("#version 330 core\nuniform float horizonRadius;\nuniform vec3 observerBeta;\n")
        + OrbitalMechanics::RelativityShaderFunctions() + KerrSpacetime::ShaderFunctions() + lensFragmentBody;
    lensProgram = compileShaderProgram(screenVertexSource, lensSource.c_str());
    int linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    int lensLinked = GL_FALSE;
    glGetProgramiv(lensProgram, GL_LINK_STATUS, &lensLinked);
    if (linked != GL_TRUE || lensLinked != GL_TRUE) {
        Release();
        return false;
    }

    const auto upload = [](Mesh& mesh, const std::vector<glm::vec3>& vertices) {
        mesh.vertexCount = static_cast<int>(vertices.size());
        glGenVertexArrays(1, &mesh.vao);
        glGenBuffers(1, &mesh.buffer);
        glBindVertexArray(mesh.vao);
        glBindBuffer(GL_ARRAY_BUFFER, mesh.buffer);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(glm::vec3), vertices.data(), GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), nullptr);
        glEnableVertexAttribArray(0);
    };
    upload(sphere, MakeSphere());
    upload(craftRing, MakeCraftRing());
    {
        // A compact, tapered Ranger silhouette, expressed in physical metres.
        const glm::vec3 p[]={{-2,-1,-4},{2,-1,-4},{-3,-1,3},{3,-1,3},{-1,1.5,-1},{1,1.5,-1},{-2,1,3},{2,1,3}};
        const int faces[]={0,1,4,1,5,4,0,4,2,2,4,6,1,3,5,3,7,5,4,5,6,5,7,6,2,6,3,3,6,7,0,2,1,1,2,3};
        std::vector<glm::vec3> vertices;for(int index:faces)vertices.push_back(p[index]);upload(rangerMesh,vertices);
    }
    glGenVertexArrays(1, &photonPackets.vao);
    glGenBuffers(1, &photonPackets.buffer);
    glBindVertexArray(photonPackets.vao);
    glBindBuffer(GL_ARRAY_BUFFER, photonPackets.buffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(PhotonPacket), nullptr, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(PhotonPacket), nullptr);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(PhotonPacket),
        reinterpret_cast<void*>(offsetof(PhotonPacket, color)));
    glEnableVertexAttribArray(1);
    glGenVertexArrays(1, &craftTrail.vao);
    glGenBuffers(1, &craftTrail.buffer);
    glBindVertexArray(craftTrail.vao);
    glBindBuffer(GL_ARRAY_BUFFER, craftTrail.buffer);
    glBufferData(GL_ARRAY_BUFFER, 2048 * sizeof(glm::vec3), nullptr, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), nullptr);
    glEnableVertexAttribArray(0);
    glGenVertexArrays(1, &screenVao);
    glGenVertexArrays(1,&guides.vao);
    glGenBuffers(1,&guides.buffer);
    glBindVertexArray(guides.vao);
    glBindBuffer(GL_ARRAY_BUFFER,guides.buffer);
    glBufferData(GL_ARRAY_BUFFER,sizeof(glm::vec3),nullptr,GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(glm::vec3),nullptr);
    glEnableVertexAttribArray(0);
    glGenVertexArrays(1,&waterPatch.vao);glGenBuffers(1,&waterPatch.buffer);
    glBindVertexArray(waterPatch.vao);glBindBuffer(GL_ARRAY_BUFFER,waterPatch.buffer);
    glBufferData(GL_ARRAY_BUFFER,sizeof(glm::vec3),nullptr,GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(glm::vec3),nullptr);glEnableVertexAttribArray(0);
    catalogue.Generate();const auto& destinations=catalogue.Positions();const auto& order=catalogue.DrawOrder();
    std::vector<glm::vec3> stars,residuals;stars.reserve(destinations.size()+1);residuals.reserve(destinations.size()+1);
    std::vector<glm::vec2> properties;properties.reserve(destinations.size()+1);
    starLuminosities.resize(destinations.size());starBatches.clear();
    constexpr int batchSize=256;
    for(int first=0;first<static_cast<int>(order.size());first+=batchSize){
        const int count=std::min(batchSize,static_cast<int>(order.size())-first);
        glm::dvec3 low(std::numeric_limits<double>::max()),highBound(-std::numeric_limits<double>::max());float brightest=0;
        for(int k=first;k<first+count;++k){
            const int index=order[k];const auto& position=destinations[index];const glm::vec3 high(position);
            stars.push_back(high);residuals.push_back(glm::vec3(position-glm::dvec3(high)));
            double temperature,luminosity;
            if(index<Universe::LegacyStarCount){const auto metadata=StarSystem::DescribeStar(Universe::StarId(index),"");temperature=metadata.temperature;luminosity=metadata.luminositySolar;}
            else {const auto metadata=Universe::Properties(index);temperature=metadata.temperature;luminosity=metadata.luminositySolar;}
            properties.push_back({static_cast<float>(temperature),static_cast<float>(luminosity)});
            starLuminosities[index]=static_cast<float>(luminosity);brightest=std::max(brightest,static_cast<float>(luminosity));
            low=glm::min(low,position);highBound=glm::max(highBound,position);
        }
        starBatches.push_back({(low+highBound)*.5,glm::length(highBound-low)*.5,brightest,first,count});
    }
    // The authored Solar preset shares the same far-point tier when left behind.
    stars.push_back(glm::vec3(0));residuals.push_back(glm::vec3(0));
    upload(backgroundStars,stars);
    glGenBuffers(1,&stellarResiduals);glBindBuffer(GL_ARRAY_BUFFER,stellarResiduals);
    glBufferData(GL_ARRAY_BUFFER,residuals.size()*sizeof(glm::vec3),residuals.data(),GL_STATIC_DRAW);
    glVertexAttribPointer(4,3,GL_FLOAT,GL_FALSE,sizeof(glm::vec3),nullptr);glEnableVertexAttribArray(4);
    properties.push_back({5772,1});
    glGenBuffers(1,&stellarProperties);glBindBuffer(GL_ARRAY_BUFFER,stellarProperties);
    glBufferData(GL_ARRAY_BUFFER,properties.size()*sizeof(glm::vec2),properties.data(),GL_STATIC_DRAW);
    glVertexAttribPointer(2,1,GL_FLOAT,GL_FALSE,sizeof(glm::vec2),nullptr);glEnableVertexAttribArray(2);
    glVertexAttribPointer(3,1,GL_FLOAT,GL_FALSE,sizeof(glm::vec2),reinterpret_cast<void*>(sizeof(float)));glEnableVertexAttribArray(3);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    ResetSpacecraft(0.0);
    if (!atmosphere.Initialize() || !stellarActivity.Initialize()) {
        Release();
        return false;
    }
    galaxies.Initialize();
    if(!milkyWay.Initialize()){Release();return false;}
    glGenTextures(1,&skyCube);
    glGenTextures(1,&sourceColor);
    glGenTextures(1,&sourceDepth);
    BuildSkyCube();
    return true;
}

void BlackHole::ResetSpacecraft(double seconds) {
    spacecraft=InterstellarWorlds::DepartureState(seconds);
    spacecraftTime = seconds;
    fuelSeconds = InterstellarWorlds::FuelSeconds;
    boosterFiring = false;
    manualBoost = false;
    spacecraftCaptured = false;
    spacecraftProperTime=0;rangersDetached=false;rangerSeparationTime=0;rangerHistory.clear();
    rangers=InterstellarWorlds::SeparateRangers(spacecraft);
    trailSampleTime = seconds;
    trail.assign(1, spacecraft.position);
    missionStart = seconds;
    flightHistory = {{seconds, spacecraft, false, false}};
}

void BlackHole::BoostSpacecraft() {
    if (!spacecraftCaptured && fuelSeconds > 0.0){
        boosterFiring = true;
        manualBoost=true;
    }
}

double BlackHole::SpacecraftEnergy() const {
    return InterstellarWorlds::SpecificEnergy(spacecraft);
}

const char* BlackHole::SpacecraftStatus() const {
    if (spacecraftCaptured)
        return "Captured";
    if (boosterFiring && fuelSeconds > 0.0)
        return "Boosting";
    if(InterstellarWorlds::SpecificEnergy(spacecraft)>0)
        return glm::dot(spacecraft.position,spacecraft.velocity)>0?"Unbound outbound / escaping":"Unbound flyby";
    if (fuelSeconds > 0.0)
        return "Inbound from Mann / Oberth burn armed";
    const double specificEnergy = 0.5 * glm::dot(spacecraft.velocity, spacecraft.velocity)
        - gravitationalParameter / glm::length(spacecraft.position);
    return specificEnergy > 0.0 ? "Escape trajectory" : "Bound orbit";
}

void BlackHole::Update(double seconds) {
    if (seconds <= spacecraftTime || spacecraftCaptured)
        return;
    while (spacecraftTime < seconds && !spacecraftCaptured) {
        const double radius = glm::length(spacecraft.position);
        if (radius <= horizonRadius * 1.05) {
            spacecraftCaptured = true;
            flightHistory.push_back({spacecraftTime, spacecraft, false, true});
            break;
        }
        const double fuelBefore=fuelSeconds;
        double proper=0;
        const double interval=InterstellarWorlds::AdvanceMission(spacecraft,fuelSeconds,boosterFiring,manualBoost,seconds-spacecraftTime,&proper);
        spacecraftProperTime+=proper;
        if(rangersDetached){
            for(auto& ranger:rangers)OrbitalMechanics::AdvanceSchwarzschild(ranger,gravitationalParameter,Astronomy::lightSpeedAu,0,interval);
        }
        const bool burning=fuelBefore>fuelSeconds;
        spacecraftTime += interval;
        const bool separation=!rangersDetached && fuelBefore>0 && fuelSeconds==0;
        if(separation){
            const auto axis=glm::dmat3(TargetRotation(9,spacecraftTime))*glm::dvec3(1,0,0);
            const double lapse=std::sqrt(1-Astronomy::gargantuaHorizonAu/glm::length(spacecraft.position));
            for(int i=0;i<2;++i){
                rangers[i]=RangerAt(spacecraftTime,i);
                rangers[i].velocity+=axis*((i==0?-1:1)*3*lapse/Astronomy::metresPerAu);
            }
            rangersDetached=true;rangerSeparationTime=spacecraftTime;
            rangerHistory.push_back({spacecraftTime,rangers});
        }
        const double sampleInterval = std::max(1.0,std::min(std::max(0.02*timeScale,(spacecraftTime-missionStart)*.01),
            .02*std::sqrt(radius*radius*radius/gravitationalParameter)));
        if (spacecraftTime - flightHistory.back().time >= sampleInterval || burning || separation)
            flightHistory.push_back({spacecraftTime, spacecraft, boosterFiring, false,spacecraftProperTime});
        if(rangersDetached && spacecraftTime-rangerHistory.back().time>=sampleInterval)
            rangerHistory.push_back({spacecraftTime,rangers});
        if (spacecraftTime - trailSampleTime >= 0.04*timeScale) {
            if (trail.size() == 2048)
                trail.erase(trail.begin(), trail.begin() + 512);
            trail.push_back(spacecraft.position);
            trailSampleTime = spacecraftTime;
        }
    }
}

BlackHole::FlightSample BlackHole::FlightAt(double seconds) const {
    if(seconds<=spacecraftTime && seconds>=spacecraftTime-1.0) {
        const double dt=spacecraftTime-seconds;
        const auto acceleration=OrbitalMechanics::SchwarzschildAcceleration(spacecraft,gravitationalParameter,Astronomy::lightSpeedAu,boosterFiring?boosterAcceleration:0);
        return {seconds,{spacecraft.position-spacecraft.velocity*dt+.5*acceleration*dt*dt,
            spacecraft.velocity-acceleration*dt},boosterFiring,spacecraftCaptured,
            spacecraftProperTime-dt*OrbitalMechanics::SchwarzschildClockRate(spacecraft,horizonRadius,Astronomy::lightSpeedAu)};
    }
    if (seconds < missionStart) {
        const auto position=PlanetPosition(seconds,1);
        auto state=OrbitalMechanics::ParticleState{position+glm::normalize(position)*(bodies[1].radius+100000/Astronomy::metresPerAu),
            InterstellarWorlds::Orientation()*glm::dvec3(OrbitalMechanics::VelocityAtTime(bodies[1].orbit,gravitationalParameter,seconds))};
        return {seconds, state, false, false};
    }
    auto upper = std::lower_bound(flightHistory.begin(), flightHistory.end(), seconds,
        [](const FlightSample& sample, double time) { return sample.time < time; });
    if (upper == flightHistory.begin())
        return *upper;
    if (upper == flightHistory.end()) {
        const auto& lower = flightHistory.back();
        const double span = spacecraftTime - lower.time;
        const double fraction = span > 0.0 ? glm::clamp((seconds - lower.time) / span, 0.0, 1.0) : 0.0;
        return {seconds, {glm::mix(lower.state.position, spacecraft.position, fraction),
                          glm::mix(lower.state.velocity, spacecraft.velocity, fraction)},
                lower.firing, spacecraftCaptured,glm::mix(lower.properTime,spacecraftProperTime,fraction)};
    }
    const auto& lower = *(upper - 1);
    const double fraction = (seconds - lower.time) / (upper->time - lower.time);
    return {seconds, {glm::mix(lower.state.position, upper->state.position, fraction),
                      glm::mix(lower.state.velocity, upper->state.velocity, fraction)},
            lower.firing, lower.captured,glm::mix(lower.properTime,upper->properTime,fraction)};
}

BlackHole::FlightSample BlackHole::ObservedFlight(const glm::dvec3& camera, double seconds) const {
    // Solve t_observe = t_emit + travelDelay(t_emit) against the simulated history.
    double lower = missionStart - OrbitalMechanics::LightTravelDelay(
        flightHistory.front().state.position, glm::dvec3(camera), gravitationalParameter, horizonRadius) - 10.0;
    double upper = seconds;
    for (int iteration = 0; iteration < 56; ++iteration) {
        const double emission = 0.5 * (lower + upper);
        const auto sample = FlightAt(emission);
        const double arrival = emission + OrbitalMechanics::LightTravelDelay(
            sample.state.position, glm::dvec3(camera), gravitationalParameter, horizonRadius);
        if (arrival < seconds)
            lower = emission;
        else
            upper = emission;
    }
    return FlightAt(0.5 * (lower + upper));
}

OrbitalMechanics::ParticleState BlackHole::RangerAt(double seconds,int index) const {
    if(!rangersDetached || seconds<rangerSeparationTime){
        const auto flight=FlightAt(seconds);
        const auto rotation=glm::dmat3(TargetRotation(9,seconds));
        const double sign=index==0?-1:1;
        const auto offset=rotation*glm::dvec3(sign*25/Astronomy::metresPerAu,0,0);
        const double rate=OrbitalMechanics::SchwarzschildClockRate(flight.state,horizonRadius,Astronomy::lightSpeedAu);
        return {flight.state.position+offset,flight.state.velocity+glm::cross(rotation*glm::dvec3(0,0,.4*rate),offset)};
    }
    if(seconds>=spacecraftTime-1){
        const auto& current=rangers[index];const double dt=spacecraftTime-seconds;
        const auto acceleration=OrbitalMechanics::SchwarzschildAcceleration(current,gravitationalParameter,Astronomy::lightSpeedAu);
        return {current.position-current.velocity*dt+.5*acceleration*dt*dt,current.velocity-acceleration*dt};
    }
    auto upper=std::lower_bound(rangerHistory.begin(),rangerHistory.end(),seconds,
        [](const RangerSample& sample,double time){return sample.time<time;});
    if(upper==rangerHistory.begin())return upper->state[index];
    const auto& lower=*(upper-1);
    const auto& next=upper==rangerHistory.end()?rangers[index]:upper->state[index];
    const double nextTime=upper==rangerHistory.end()?spacecraftTime:upper->time;
    const double t=glm::clamp((seconds-lower.time)/(nextTime-lower.time),0.0,1.0);
    return {glm::mix(lower.state[index].position,next.position,t),glm::mix(lower.state[index].velocity,next.velocity,t)};
}

OrbitalMechanics::ParticleState BlackHole::ObservedRanger(const glm::dvec3& camera,double seconds,int index) const {
    double lower=missionStart-OrbitalMechanics::LightTravelDelay(flightHistory.front().state.position,camera,gravitationalParameter,horizonRadius)-10,upper=seconds;
    for(int iteration=0;iteration<56;++iteration){
        const double t=.5*(lower+upper);
        const auto sample=RangerAt(t,index);
        if(t+OrbitalMechanics::LightTravelDelay(sample.position,camera,gravitationalParameter,horizonRadius)<seconds)lower=t;
        else upper=t;
    }
    return RangerAt(.5*(lower+upper),index);
}

double BlackHole::ObserverClockRate(const glm::dvec3& camera,double seconds,const glm::dvec3& velocity) const {
    double rate=OrbitalMechanics::SchwarzschildClockRate({camera,velocity},Astronomy::gargantuaHorizonAu,Astronomy::lightSpeedAu);
    const double c2=Astronomy::lightSpeedAu*Astronomy::lightSpeedAu;
    double potential=0;
    if(glm::length(camera)>20000)return rate;
    for(int planet=0;planet<3;++planet){
        const double r=std::max(glm::length(camera-PlanetPosition(seconds,planet)),static_cast<double>(bodies[planet].radius));
        potential+=Astronomy::solarGravityAu*(InterstellarWorlds::Get(planet).massEarth/332946.0487)/r;
    }
    return rate*std::sqrt(std::max(0.0,1-2*potential/c2));
}

double BlackHole::SpacecraftLightAge(const glm::dvec3& camera, double seconds) const {
    return seconds - ObservedFlight(camera, seconds).time;
}

glm::dvec3 BlackHole::ObservedSpacecraftPosition(const glm::dvec3& camera,double seconds) const {
    return ObservedFlight(camera,seconds).state.position;
}

BlackHole::VehiclePose BlackHole::CockpitPose(int vehicle,double seconds) const {
    auto rotation=glm::dmat3(TargetRotation(9,seconds));
    if(vehicle==1){
        const auto flight=FlightAt(seconds);
        const auto seat=flight.state.position+rotation*glm::dvec3(23/Astronomy::metresPerAu,0,0);
        rotation*=glm::dmat3(glm::rotate(glm::dmat4(1),-glm::half_pi<double>(),glm::dvec3(0,1,0)));
        return {seat,flight.state.velocity,rotation};
    }
    const auto state=RangerAt(seconds,glm::clamp(vehicle-2,0,1));
    if(rangersDetached && seconds>=rangerSeparationTime)rotation=glm::dmat3(AttitudeAlong(glm::vec3(state.velocity)));
    return {state.position+rotation*glm::dvec3(0,.35/Astronomy::metresPerAu,-1/Astronomy::metresPerAu),state.velocity,rotation};
}

glm::dvec3 BlackHole::PlanetPosition(double seconds, int planet) {
    return InterstellarWorlds::Orientation()*OrbitalMechanics::PositionAtTimeDouble(bodies[planet].orbit,gravitationalParameter,seconds);
}

int BlackHole::NearestPlanet(const glm::dvec3& position, double seconds) {
    int nearest = 0;
    double distance = glm::length(position - PlanetPosition(seconds));
    for (int planet = 1; planet < 3; ++planet) {
        const double candidate = glm::length(position - PlanetPosition(seconds, planet));
        if (candidate < distance) {
            distance = candidate;
            nearest = planet;
        }
    }
    return nearest;
}

BlackHole::Target BlackHole::ResolveTarget(int id, double seconds) const {
    if(GalacticCores::IsCore(id)){const auto core=GalacticCores::Get(id);return {core.position,static_cast<float>(core.RadiusAu()),core.name};}
    if (id == 0)
        return {glm::vec3(0), horizonRadius, "Black hole"};
    if (id >= 1 && id <= 8) {
        const auto& body = bodies[id - 1];
        const auto position=InterstellarWorlds::Orientation()*OrbitalMechanics::PositionAtTimeDouble(body.orbit,gravitationalParameter,seconds);
        return {position, body.radius, body.surface == Planet
            ? InterstellarWorlds::Get(id-1).name : "Orbiting star " + std::to_string(id - 3)};
    }
    if (id == 9)
        return {SpacecraftPosition(), static_cast<float>(50.0/Astronomy::metresPerAu), "Spacecraft"};
    const auto& destinations=catalogue.Positions();const int star = Universe::StarIndex(id);
    if (star >= 0 && star < static_cast<int>(destinations.size())) {
        const auto name=Universe::StarName(star);
        const auto metadata=StarSystem::DescribeStar(id,name);
        return {destinations[star],static_cast<float>(metadata.radiusSolar*Astronomy::solarRadiusAu), name};
    }
    return {glm::vec3(0), horizonRadius, "Black hole"};
}

glm::mat3 BlackHole::TargetRotation(int id,double seconds) const {
    if (id>=1 && id<=3) return glm::mat3(PlanetRotation(seconds,id-1));
    if (id==9) {
        const auto flight=FlightAt(seconds);
        return glm::mat3(AttitudeAlong(glm::vec3(flight.state.position))
            *glm::rotate(glm::mat4(1),static_cast<float>(std::remainder(flight.properTime*.4,glm::two_pi<double>())),glm::vec3(0,0,1)));
    }
    if (id>=4) return glm::mat3(glm::rotate(glm::mat4(1),
        static_cast<float>(std::remainder(seconds*glm::two_pi<double>()/(25.38*Astronomy::secondsPerDay),glm::two_pi<double>())),glm::vec3(0,1,0)));
    return glm::mat3(1); // Schwarzschild's central object has no intrinsic spin.
}

int BlackHole::PickCatalogueStar(const glm::dvec3& origin,const glm::vec3& direction,double angularTolerance,int exclude) const {
    int selected=-1;
    double best=std::max(0.0,angularTolerance);
    const auto ray=glm::normalize(glm::dvec3(direction));
    const auto& destinations=catalogue.Positions();const auto& order=catalogue.DrawOrder();
    const auto consider=[&](int index){
        const bool sun=index<0;const int id=sun?-2:Universe::StarId(index);if(id==exclude)return;
        const auto offset=(sun?glm::dvec3(0):destinations[index])-origin;
        const double along=glm::dot(offset,ray),distance=glm::length(offset);
        if(along<=0 || distance<3)return;
        if(!StellarPhotometry::Visible(sun?1:starLuminosities[index],distance,magnitudeLimit))return;
        const double separation=glm::length(offset-along*ray)/distance;
        if(separation<best){best=separation;selected=id;}
    };
    for(const auto& batch:starBatches){
        const auto delta=batch.center-origin;const double along=glm::dot(delta,ray),distance=glm::length(delta);
        if(along+batch.radius<=0 || glm::length(delta-along*ray)>batch.radius+best*(distance+batch.radius))continue;
        if(!StellarPhotometry::Visible(batch.maxLuminosity,std::max(3.0,distance-batch.radius),magnitudeLimit))continue;
        for(int k=batch.first;k<batch.first+batch.count;++k)consider(order[k]);
    }
    consider(-1);
    return selected;
}

int BlackHole::PickTarget(const glm::dvec3& origin, const glm::vec3& direction, double seconds) const {
    int selected = -1;
    float bestScore = 1.0f;
    for (int id = 0; id < 10; ++id) {
        const auto target = ResolveTarget(id, seconds);
        const auto offset = target.position - origin;
        const double distance = glm::length(offset);
        const double along = glm::dot(offset, glm::dvec3(direction));
        if (along <= 0.0 || distance < 1e-14)
            continue;
        const float separation = static_cast<float>(glm::length(offset - along * glm::dvec3(direction)) / distance);
        const float tolerance = glm::max(0.012f, static_cast<float>(target.radius / distance));
        const float score = separation / tolerance;
        if (score < bestScore) {
            bestScore = score;
            selected = id;
        }
    }
    return selected>=0?selected:PickCatalogueStar(origin,direction,.012);
}

void BlackHole::RenderCatalogueSelection(const glm::mat4& vp,const glm::dvec3& camera,int selected,double seconds) const {
    if(selected<10 || !guides.vao)return;
    const auto target=glm::vec3(glm::normalize(ResolveTarget(selected,seconds).position-camera)*10.0);
    const auto clip=vp*glm::vec4(target,1);
    if(clip.w<=0)return;
    const auto forward=glm::normalize(target);
    const auto reference=std::abs(forward.y)<.95f?glm::vec3(0,1,0):glm::vec3(1,0,0);
    const auto right=glm::normalize(glm::cross(forward,reference)),up=glm::cross(right,forward);
    const float distance=glm::length(target);
    std::vector<glm::vec3> vertices;
    for(auto axis:{right,up})for(int sign:{-1,1}){
        vertices.push_back(target+axis*(sign*distance*.005f));
        vertices.push_back(target+axis*(sign*distance*.014f));
    }
    glUseProgram(program);
    glUniformMatrix4fv(glGetUniformLocation(program,"viewProjection"),1,GL_FALSE,glm::value_ptr(vp));
    const glm::mat4 identity(1);
    glUniformMatrix4fv(glGetUniformLocation(program,"model"),1,GL_FALSE,glm::value_ptr(identity));
    glUniform1i(glGetUniformLocation(program,"surface"),4);
    glUniform3f(glGetUniformLocation(program,"bodyColor"),.15f,1,.65f);
    glDisable(GL_DEPTH_TEST);
    glBindVertexArray(guides.vao);glBindBuffer(GL_ARRAY_BUFFER,guides.buffer);
    glBufferData(GL_ARRAY_BUFFER,vertices.size()*sizeof(glm::vec3),vertices.data(),GL_DYNAMIC_DRAW);
    glDrawArrays(GL_LINES,0,static_cast<int>(vertices.size()));
    glBindVertexArray(0);glEnable(GL_DEPTH_TEST);
}

glm::mat4 BlackHole::PlanetRotation(double seconds,int planet) {
    if(planet==0){
        const auto position=PlanetPosition(seconds,0);
        const auto velocity=InterstellarWorlds::Orientation()*glm::dvec3(OrbitalMechanics::VelocityAtTime(bodies[0].orbit,gravitationalParameter,seconds));
        const auto toward=glm::normalize(-position),up=glm::normalize(glm::cross(position,velocity));
        return glm::mat4(glm::mat3(glm::dmat3(glm::normalize(glm::cross(up,toward)),up,toward)));
    }
    const double day=(planet==1?30.0:28.0)*3600;
    return glm::rotate(glm::rotate(glm::mat4(1),planet==1?.14f:.4f,glm::vec3(1,0,0)),
        static_cast<float>(std::remainder(seconds/day,1.0)*glm::two_pi<double>()),glm::vec3(0,1,0));
}

double BlackHole::PlanetSurfaceRadius(const glm::vec3& worldDirection, double seconds, int planet) const {
    const auto direction = glm::vec3(glm::transpose(PlanetRotation(seconds,planet)) * glm::vec4(worldDirection, 0.0f));
    const float phase = static_cast<float>(std::remainder(seconds, glm::two_pi<double>()));
    return bodies[planet].radius*(1.0+InterstellarWorlds::Elevation(direction,planet,phase));
}

void BlackHole::Render(const glm::mat4& viewProjection, const glm::dvec3& cameraPosition, double seconds,
                      const glm::vec3& observerVelocity, bool trajectories, int selected,int interiorVehicle,
                      const glm::vec3& physicalObserverBeta,bool physicalObserver) const {
    if(!skyRefreshDeferred && (skyCubeDirty || glm::length(skyCubeOrigin)>1))BuildSkyCube();
    glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    const glm::mat4 identity(1.0f);
    const glm::vec3 cameraFloat(cameraPosition);
    const auto diskTilt = DiskOrientation();
    const float diskPhase = static_cast<float>(std::remainder(
        OrbitalMechanics::MeanMotion(gravitationalParameter, outerDiskRadius) * seconds,
        glm::two_pi<double>()));
    auto observerBeta = observerVelocity / static_cast<float>(std::sqrt(2.0 * gravitationalParameter / horizonRadius));
    if (glm::length(observerBeta) > 0.99f)
        observerBeta = glm::normalize(observerBeta) * 0.99f;

    glUseProgram(program);
    glUniform1i(glGetUniformLocation(program, "oceanWorld"), true);
    glUniform1i(glGetUniformLocation(program, "physicalObserver"), physicalObserver);
    glUniform1f(glGetUniformLocation(program, "seaLevel"), 0.035f);
    glUniform1f(glGetUniformLocation(program, "terrainRelief"), 0.1f);
    glUniform1f(glGetUniformLocation(program, "waterWaveHeight"), 0.0015f);
    glUniform3fv(glGetUniformLocation(program, "observerBeta"), 1, glm::value_ptr(observerBeta));
    glUniform1f(glGetUniformLocation(program, "terrainSeed"), static_cast<float>(terrainSeed));
    glUniform3fv(glGetUniformLocation(program, "cameraPosition"), 1, glm::value_ptr(cameraFloat));
    glUniform1f(glGetUniformLocation(program, "horizonRadius"), horizonRadius);
    const auto diskAxis = glm::vec3(diskTilt * glm::vec4(0.0f, 0.0f, 1.0f, 0.0f));
    glUniform1f(glGetUniformLocation(program, "wavePhase"), static_cast<float>(std::remainder(seconds, glm::two_pi<double>())));
    glUniformMatrix4fv(glGetUniformLocation(program, "viewProjection"), 1, GL_FALSE, glm::value_ptr(viewProjection));
    const int modelLocation = glGetUniformLocation(program, "model");
    const int surfaceLocation = glGetUniformLocation(program, "surface");
    const int colorLocation = glGetUniformLocation(program, "bodyColor");

    RenderBackground(viewProjection,cameraPosition);

    glUseProgram(program);
    glBindVertexArray(sphere.vao);
    const auto drawSphere = [&](const glm::dvec3& center, float radius, Surface surface, const glm::vec3& color, int seed, float temperature) {
        const bool blend=glIsEnabled(GL_BLEND);glDisable(GL_BLEND);
        auto model = glm::translate(identity, glm::vec3(center-cameraPosition));
        if (surface == Planet)
            model *= PlanetRotation(seconds,seed-1);
        else if (surface == Star)
            model *= glm::mat4(TargetRotation(seed,seconds));
        model = glm::scale(model, glm::vec3(radius));
        glUniformMatrix4fv(modelLocation, 1, GL_FALSE, glm::value_ptr(model));
        glUniform1i(surfaceLocation, surface);
        glUniform3fv(colorLocation, 1, glm::value_ptr(color));
        if(surface==Star) stellarActivity.BindSurface(program,seed,seconds);
        const int planet=seed-1;
        glUniform1i(glGetUniformLocation(program,"interstellarPlanet"),glm::clamp(planet,0,2));
        glUniform1i(glGetUniformLocation(program,"oceanWorld"),planet==0);
        const bool patch=surface==Planet && glm::length(cameraPosition-center)-PlanetSurfaceRadius(glm::normalize(glm::vec3(cameraPosition-center)),seconds,planet)<20000.0/Astronomy::metresPerAu;
        glUniform1i(glGetUniformLocation(program,"patchEnabled"),patch);
        glUniform1i(glGetUniformLocation(program,"patchDrawing"),false);
        if(patch) {
            const auto rotation=glm::dmat3(PlanetRotation(seconds,planet));
            const auto direction=glm::normalize(glm::transpose(rotation)*(cameraPosition-center));
            const double metres=radius*Astronomy::metresPerAu;
            if(!waterPatch.vertexCount || waterPatchRadiusMetres!=metres) {
                waterPatchDirection=direction;
                waterPatchRadiusMetres=metres;
                const auto vertices=PlanetTerrain::SurfaceLodVertices(metres);
                waterPatch.vertexCount=static_cast<int>(vertices.size());
                glBindBuffer(GL_ARRAY_BUFFER,waterPatch.buffer);
                glBufferData(GL_ARRAY_BUFFER,vertices.size()*sizeof(glm::vec3),vertices.data(),GL_STATIC_DRAW);
            }else if(glm::length(direction-waterPatchDirection)*metres>2.0)waterPatchDirection=direction;
            const float elevation=InterstellarWorlds::Elevation(glm::vec3(waterPatchDirection),planet,static_cast<float>(std::remainder(seconds,glm::two_pi<double>())));
            PlanetTerrain::ConfigureSurfacePatch(program,waterPatchDirection,rotation,center-cameraPosition,metres,elevation);
        }
        glBindVertexArray(sphere.vao);
        glDrawArrays(GL_TRIANGLES, 0, sphere.vertexCount);
        if(patch) {
            glUniform1i(glGetUniformLocation(program,"patchDrawing"),true);
            glBindVertexArray(waterPatch.vao);glDrawArrays(GL_TRIANGLES,0,waterPatch.vertexCount);
            glBindVertexArray(sphere.vao);
        }
        glUniform1i(glGetUniformLocation(program,"patchEnabled"),false);
        glUniform1i(glGetUniformLocation(program,"patchDrawing"),false);
        if(surface==Star)stellarActivity.Render(viewProjection,model,temperature,seed,seconds);
        if(blend)glEnable(GL_BLEND);
    };
    int bodyIndex = 0;
    for (const auto& body : bodies) {
        glUniform1f(glGetUniformLocation(program, "terrainSeed"), static_cast<float>((terrainSeed + bodyIndex * 137) % 10000));
        const auto orbitalPosition = OrbitalMechanics::PositionAtTime(body.orbit, gravitationalParameter, seconds);
        const auto worldPosition = glm::vec3(diskTilt * glm::vec4(orbitalPosition, 1.0f));
        const auto velocity = glm::vec3(diskTilt * glm::vec4(
            OrbitalMechanics::VelocityAtTime(body.orbit, gravitationalParameter, seconds), 0.0f));
        const auto beta = velocity / static_cast<float>(std::sqrt(2.0 * gravitationalParameter / horizonRadius));
        glUniform3fv(glGetUniformLocation(program, "velocityBeta"), 1, glm::value_ptr(beta));
        glUniform1f(glGetUniformLocation(program, "temperature"), body.temperature);
        glUniform1f(glGetUniformLocation(program,"waterWaveHeight"),static_cast<float>(2.0/Astronomy::metresPerAu/body.radius));
        const auto preciseCenter=InterstellarWorlds::Orientation()*OrbitalMechanics::PositionAtTimeDouble(body.orbit,gravitationalParameter,seconds);
        drawSphere(preciseCenter, body.radius, body.surface, body.color,bodyIndex+1,body.temperature);
        ++bodyIndex;
    }
    glUniform3f(glGetUniformLocation(program, "velocityBeta"), 0, 0, 0);
    for (int id : catalogue.Nearby(cameraPosition)) {
            const int star=Universe::StarIndex(id);
            const auto metadata=StarSystem::DescribeStar(id,"");
            glUniform1f(glGetUniformLocation(program, "temperature"),static_cast<float>(metadata.temperature));
            drawSphere(catalogue.Positions()[star],static_cast<float>(metadata.radiusSolar*Astronomy::solarRadiusAu), Star, glm::vec3(1),id,static_cast<float>(metadata.temperature));
    }
    const auto observed = ObservedFlight(cameraPosition, seconds);
    if (!observed.captured) {
        glUniform1i(surfaceLocation, 4);
        glUniform3f(colorLocation, 0.22f, 0.65f, 0.82f);
        glUniformMatrix4fv(modelLocation, 1, GL_FALSE, glm::value_ptr(identity));
        glBindVertexArray(craftTrail.vao);
        std::vector<glm::vec3> relativeTrail;
        for(const auto& point:trail)relativeTrail.push_back(glm::vec3(point-cameraPosition));
        glBindBuffer(GL_ARRAY_BUFFER,craftTrail.buffer);
        glBufferData(GL_ARRAY_BUFFER,relativeTrail.size()*sizeof(glm::vec3),relativeTrail.data(),GL_DYNAMIC_DRAW);
        if (trajectories) glDrawArrays(GL_LINE_STRIP, 0, static_cast<int>(trail.size()));

        if(interiorVehicle!=1){
        // Attitude guidance keeps the ring axis radial while the engine thrusts prograde.
        auto craftModel = glm::translate(identity, glm::vec3(observed.state.position-cameraPosition))
            * AttitudeAlong(glm::vec3(observed.state.position));
        craftModel = glm::rotate(craftModel, static_cast<float>(std::remainder(observed.properTime * 0.4, glm::two_pi<double>())),
            glm::vec3(0.0f, 0.0f, 1.0f));
        craftModel=glm::scale(craftModel,glm::vec3(craftScale));
        glUniform1i(surfaceLocation, 5);
        glUniformMatrix4fv(modelLocation, 1, GL_FALSE, glm::value_ptr(craftModel));
        glUniform3f(colorLocation, 0.75f, 0.80f, 0.84f);
        glBindVertexArray(craftRing.vao);
        glDrawArrays(GL_TRIANGLES, 0, craftRing.vertexCount);
        glBindVertexArray(sphere.vao);
        for (int module = 0; module < 12; ++module) {
            const float angle = glm::two_pi<float>() * module / 12.0f;
            auto model = glm::translate(craftModel, glm::vec3(0.045f * std::cos(angle), 0.045f * std::sin(angle), 0.0f));
            model = glm::scale(model, glm::vec3(0.008f, 0.008f, 0.012f));
            glUniformMatrix4fv(modelLocation, 1, GL_FALSE, glm::value_ptr(model));
            glDrawArrays(GL_TRIANGLES, 0, sphere.vertexCount);
        }
        for (int spoke = 0; spoke < 4; ++spoke) {
            auto model = glm::rotate(craftModel, glm::half_pi<float>() * spoke, glm::vec3(0.0f, 0.0f, 1.0f));
            model = glm::translate(model, glm::vec3(0.022f, 0.0f, 0.0f));
            model = glm::scale(model, glm::vec3(0.023f, 0.002f, 0.002f));
            glUniformMatrix4fv(modelLocation, 1, GL_FALSE, glm::value_ptr(model));
            glDrawArrays(GL_TRIANGLES, 0, sphere.vertexCount);
        }
        if (observed.firing) {
            glUniform1i(surfaceLocation, 6);
            const auto direction = glm::normalize(glm::vec3(observed.state.velocity));
            auto exhaust = glm::translate(identity, glm::vec3(observed.state.position-cameraPosition) - 0.075f*craftScale * direction)
                * AttitudeAlong(direction);
            exhaust = glm::scale(exhaust, glm::vec3(0.018f, 0.018f, 0.075f)*craftScale);
            glUniformMatrix4fv(modelLocation, 1, GL_FALSE, glm::value_ptr(exhaust));
            glUniform3f(colorLocation, 0.25f, 0.65f, 1.0f);
            glDrawArrays(GL_TRIANGLES, 0, sphere.vertexCount);
        }
    }
    }
    for(int ranger=0;ranger<2;++ranger){
        if(interiorVehicle==ranger+2)continue;
        const auto state=ObservedRanger(cameraPosition,seconds,ranger);
        auto model=glm::translate(identity,glm::vec3(state.position-cameraPosition));
        model*=(!rangersDetached || observed.time<rangerSeparationTime)?glm::mat4(TargetRotation(9,observed.time))
            :AttitudeAlong(glm::vec3(state.velocity));
        model=glm::scale(model,glm::vec3(1.0f/static_cast<float>(Astronomy::metresPerAu)));
        glUniform1i(surfaceLocation,5);glUniform3f(colorLocation,.82f,.85f,.86f);
        glUniformMatrix4fv(modelLocation,1,GL_FALSE,glm::value_ptr(model));
        glBindVertexArray(rangerMesh.vao);glDrawArrays(GL_TRIANGLES,0,rangerMesh.vertexCount);
        auto cockpit=glm::translate(model,glm::vec3(0,1.2,-1));cockpit=glm::scale(cockpit,glm::vec3(1.2,.5,1.6));
        glUniform3f(colorLocation,.04f,.16f,.22f);glUniformMatrix4fv(modelLocation,1,GL_FALSE,glm::value_ptr(cockpit));
        glBindVertexArray(sphere.vao);glDrawArrays(GL_TRIANGLES,0,sphere.vertexCount);
    }
    glBindVertexArray(0);
    if (trajectories && guides.vao) {
        if(orbitGuides.empty()) {
            orbitGuides.resize(std::size(bodies));
            for(size_t body=0;body<std::size(bodies);++body) {
                const double period=glm::two_pi<double>()/OrbitalMechanics::MeanMotion(gravitationalParameter,bodies[body].orbit.semiMajorAxis);
                for(int point=0;point<=256;++point)orbitGuides[body].push_back(glm::dmat3(diskTilt)*OrbitalMechanics::PositionAtTimeDouble(bodies[body].orbit,gravitationalParameter,period*point/256));
            }
        }
        glUseProgram(program);
        glUniform1i(surfaceLocation,4);
        glUniformMatrix4fv(modelLocation,1,GL_FALSE,glm::value_ptr(identity));
        glUniform3f(colorLocation,.06f,.18f,.26f);
        glBindVertexArray(guides.vao);
        glBindBuffer(GL_ARRAY_BUFFER,guides.buffer);
        for (const auto& orbit:orbitGuides) {
            std::vector<glm::vec3> vertices;
            for(const auto& point:orbit)vertices.push_back(glm::vec3(point-cameraPosition));
            glBufferData(GL_ARRAY_BUFFER,vertices.size()*sizeof(glm::vec3),vertices.data(),GL_DYNAMIC_DRAW);
            glDrawArrays(GL_LINE_STRIP,0,static_cast<int>(vertices.size()));
        }
        glBindVertexArray(0);
    }
    std::array<bool,3> foregroundAtmosphere{};
    const double foregroundEnd=std::max(1000000.0/Astronomy::metresPerAu,
        glm::length(cameraPosition)-3.0*horizonRadius);
    for (int planet = 0; atmospheresEnabled && planet < 3; ++planet){
        const auto center=PlanetPosition(seconds,planet)-cameraPosition;
        const double shellRadius=static_cast<double>(bodies[planet].radius)*1.025;
        const double distance=glm::length(center);
        // Use the same foreground distance as the lens-depth guard, including
        // the entire shell. Protect foreground air from orbit, not just while
        // the camera is inside it. Atmospheres behind the lens remain lensed.
        foregroundAtmosphere[planet]=distance<=shellRadius || distance+shellRadius<foregroundEnd;
        if(!foregroundAtmosphere[planet])
            atmosphere.Render(viewProjection,glm::vec3(0),glm::vec3(center),bodies[planet].radius,true,-cameraFloat);
    }
    if (photonView) {
        packetCache.clear();
        packetCache.reserve(100);
        const auto appendStream = [&](const glm::dvec3& emitter, const glm::vec3& color, bool historical) {
            const double delay = glm::max(0.001, OrbitalMechanics::LightTravelDelay(
                glm::dvec3(emitter), glm::dvec3(photonDetector), gravitationalParameter, horizonRadius));
            for (int packet = 0; packet < 4; ++packet) {
                const double progress = std::fmod(seconds / delay + packet / 4.0, 1.0);
                const auto source = historical ? FlightAt(seconds - progress * delay).state.position : emitter;
                packetCache.push_back({glm::vec3(glm::mix(source, photonDetector, progress)-cameraPosition), color});
            }
        };
        for (int point = 0; point < 8; ++point) {
            const float angle = glm::two_pi<float>() * point / 8.0f;
            appendStream(glm::vec3(DiskOrientation() * glm::vec4(
                GargantuaDiskOuterRadius() * std::cos(angle), GargantuaDiskOuterRadius() * std::sin(angle), 0, 1)),
                glm::vec3(1.0f, 0.7f, 0.2f), false);
        }
        for (int id = 1; id <= 8; ++id)
            appendStream(ResolveTarget(id, seconds).position,
                id <= 3 ? glm::vec3(0.2f, 0.65f, 1.0f) : glm::vec3(1.0f, 0.9f, 0.65f), false);
        appendStream(glm::vec3(spacecraft.position), glm::vec3(0.2f, 0.9f, 1.0f), true);
        RenderBackground(viewProjection,cameraPosition,true,seconds,photonDetector);
        for (const auto& group : GalaxyBackground::Groups())
            appendStream(group.center, glm::vec3(0.7f, 0.55f, 1.0f), false);
        packetCache.push_back({glm::vec3(photonDetector-cameraPosition), glm::vec3(0.4f, 1.0f, 0.5f)});
        glUseProgram(program);
        glUniform1i(surfaceLocation, 8);
        glUniformMatrix4fv(modelLocation, 1, GL_FALSE, glm::value_ptr(identity));
        glBindVertexArray(photonPackets.vao);
        glBindBuffer(GL_ARRAY_BUFFER, photonPackets.buffer);
        glBufferData(GL_ARRAY_BUFFER, packetCache.size() * sizeof(PhotonPacket), packetCache.data(), GL_DYNAMIC_DRAW);
        glPointSize(3.0f);
        glDrawArrays(GL_POINTS, 0, static_cast<int>(packetCache.size() - 1));
        glPointSize(9.0f);
        glDrawArrays(GL_POINTS, static_cast<int>(packetCache.size() - 1), 1);
        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }
    RenderLensing(viewProjection,cameraPosition,seconds,observerBeta,physicalObserverBeta,physicalObserver);
    for(int planet=0;atmospheresEnabled && planet<3;++planet){
        const auto center=PlanetPosition(seconds,planet)-cameraPosition;
        if(foregroundAtmosphere[planet])
            atmosphere.Render(viewProjection,glm::vec3(0),glm::vec3(center),bodies[planet].radius,true,-cameraFloat);
    }
    if (selected>=0 && guides.vao) {
        const auto target=glm::vec3(ResolveTarget(selected,seconds).position-cameraPosition);
        const auto delta=target;
        const float distance=glm::length(delta);
        if (distance>.00001f) {
            const auto forward=delta/distance;
            const auto reference=std::abs(forward.y)<.95f?glm::vec3(0,1,0):glm::vec3(1,0,0);
            const auto right=glm::normalize(glm::cross(forward,reference));
            const auto up=glm::cross(right,forward);
            std::vector<glm::vec3> vertices;
            for (const auto axis:{right,up}) for (int sign:{-1,1}) {
                vertices.push_back(target+axis*(sign*distance*.005f));
                vertices.push_back(target+axis*(sign*distance*.014f));
            }
            glUseProgram(program);
            glUniform1i(surfaceLocation,4);
            glUniform3f(colorLocation,.15f,1,.65f);
            glUniformMatrix4fv(modelLocation,1,GL_FALSE,glm::value_ptr(identity));
            glDisable(GL_DEPTH_TEST);
            glBindVertexArray(guides.vao); glBindBuffer(GL_ARRAY_BUFFER,guides.buffer);
            glBufferData(GL_ARRAY_BUFFER,vertices.size()*sizeof(glm::vec3),vertices.data(),GL_DYNAMIC_DRAW);
            glDrawArrays(GL_LINES,0,static_cast<int>(vertices.size()));
            glBindVertexArray(0); glEnable(GL_DEPTH_TEST);
        }
    }
}

void BlackHole::RenderBackground(const glm::mat4& vp,const glm::dvec3& camera,bool photons,double seconds,const glm::dvec3& detector) const {
    if(!photons)milkyWay.Render(vp,camera);
    const bool blend=glIsEnabled(GL_BLEND);GLint src,dst,srcA,dstA;
    glGetIntegerv(GL_BLEND_SRC_RGB,&src);glGetIntegerv(GL_BLEND_DST_RGB,&dst);
    glGetIntegerv(GL_BLEND_SRC_ALPHA,&srcA);glGetIntegerv(GL_BLEND_DST_ALPHA,&dstA);
    glEnable(GL_BLEND);glBlendFunc(GL_ONE,GL_ONE);
    glUseProgram(program);
    const glm::vec3 high(camera),low(camera-glm::dvec3(high)),detectorHigh(detector),detectorLow(detector-glm::dvec3(detectorHigh));
    glUniformMatrix4fv(glGetUniformLocation(program,"viewProjection"),1,GL_FALSE,glm::value_ptr(vp));
    glUniform1i(glGetUniformLocation(program,"surface"),7);
    glUniform3fv(glGetUniformLocation(program,"cameraPosition"),1,glm::value_ptr(high));
    glUniform3fv(glGetUniformLocation(program,"cameraLow"),1,glm::value_ptr(low));
    glUniform3fv(glGetUniformLocation(program,"detectorHigh"),1,glm::value_ptr(detectorHigh));
    glUniform3fv(glGetUniformLocation(program,"detectorLow"),1,glm::value_ptr(detectorLow));
    glUniform1i(glGetUniformLocation(program,"stellarPackets"),photons);
    glUniform1f(glGetUniformLocation(program,"stellarSeconds"),static_cast<float>(seconds));
    glUniform1f(glGetUniformLocation(program,"limitingMagnitude"),magnitudeLimit);
    stellarActivity.BindCorona(program,3);
    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(backgroundStars.vao);glPointSize(photons?2.f:7.f);
    // Conservative spatial batches: reject entire dim/offscreen regions.
    // Individual apparent-magnitude decisions remain in the vertex shader.
    const glm::dmat4 precise(vp);
    const glm::dvec3 rowX(precise[0][0],precise[1][0],precise[2][0]),rowY(precise[0][1],precise[1][1],precise[2][1]),rowW(precise[0][3],precise[1][3],precise[2][3]);
    const double left=glm::length(rowW+rowX),right=glm::length(rowW-rowX),bottom=glm::length(rowW+rowY),top=glm::length(rowW-rowY);
    int runFirst=-1,runCount=0;lastSubmittedStars=0;
    std::vector<std::pair<int,int>> runs;runs.reserve(starBatches.size()/2);
    const auto flush=[&](){if(runCount)runs.emplace_back(runFirst,runCount);runFirst=-1;runCount=0;};
    for(const auto& batch:starBatches){
        const auto delta=batch.center-camera;const auto clip=precise*glm::dvec4(delta,1);
        const double distance=std::max(3.0,glm::length(delta)-batch.radius);
        const bool visible=StellarPhotometry::Visible(batch.maxLuminosity,distance,magnitudeLimit)
            && (photons || (clip.w+clip.x>=-batch.radius*left && clip.w-clip.x>=-batch.radius*right && clip.w+clip.y>=-batch.radius*bottom && clip.w-clip.y>=-batch.radius*top));
        if(visible){if(runFirst<0)runFirst=batch.first;runCount+=batch.count;}else flush();
    }
    flush();
    // Bound driver/descriptor work even for a dense galactic view or six
    // cubemap faces. Coalesced gaps are rejected by the GPU's magnitude test.
    const size_t stride=std::max<size_t>(1,(runs.size()+63)/64);
    for(size_t i=0;i<runs.size();i+=stride){
        const auto& last=runs[std::min(runs.size()-1,i+stride-1)];
        const int first=runs[i].first,count=last.first+last.second-first;
        if(photons)glDrawArraysInstanced(GL_POINTS,first,count,4);else glDrawArrays(GL_POINTS,first,count);
        lastSubmittedStars+=count;
    }
    // The Sun is the final authored entry, outside the procedural batches.
    if(photons)glDrawArraysInstanced(GL_POINTS,backgroundStars.vertexCount-1,1,4);else glDrawArrays(GL_POINTS,backgroundStars.vertexCount-1,1);
    glUniform1i(glGetUniformLocation(program,"stellarPackets"),false);
    glBindVertexArray(0);
    if(!photons)galaxies.Render(vp,camera,magnitudeLimit);
    glBlendFuncSeparate(src,dst,srcA,dstA);if(!blend)glDisable(GL_BLEND);
}

void BlackHole::BuildSkyCube(const glm::dvec3& origin) const {
    const auto cameraSubmissionCount=lastSubmittedStars;
    skyCubeOrigin=origin;
    skyCubeDirty=false;
    int viewport[4],framebuffer;
    glGetIntegerv(GL_VIEWPORT,viewport);glGetIntegerv(GL_FRAMEBUFFER_BINDING,&framebuffer);
    unsigned int capture;glGenFramebuffers(1,&capture);glBindFramebuffer(GL_FRAMEBUFFER,capture);
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_CUBE_MAP,skyCube);
    if(!skyCubeAllocated){for(int face=0;face<6;++face)glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X+face,0,GL_RGBA16F,1024,1024,0,GL_RGBA,GL_FLOAT,nullptr);skyCubeAllocated=true;}
    glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_WRAP_R,GL_CLAMP_TO_EDGE);
    const glm::vec3 directions[]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    const glm::vec3 ups[]={{0,-1,0},{0,-1,0},{0,0,1},{0,0,-1},{0,-1,0},{0,-1,0}};
    const auto projection=glm::perspective(glm::half_pi<float>(),1.0f,.01f,1e15f);
    glViewport(0,0,1024,1024);glDisable(GL_DEPTH_TEST);
    for(int face=0;face<6;++face) {
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_CUBE_MAP_POSITIVE_X+face,skyCube,0);
        glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT);
        const auto vp=projection*glm::lookAt(glm::vec3(0),directions[face],ups[face]);
        RenderBackground(vp,origin);
    }
    glBindVertexArray(0);glEnable(GL_DEPTH_TEST);glDepthMask(GL_TRUE);
    glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);glDeleteFramebuffers(1,&capture);
    glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);
    lastSubmittedStars=cameraSubmissionCount; // Diagnostics describe the camera, not the last cube face.
}

void BlackHole::RenderLensing(const glm::mat4& vp,const glm::dvec3& camera,double seconds,const glm::vec3& beta,
                             const glm::vec3& physicalBeta,bool physicalObserver,int coreId) const {
    int viewport[4];glGetIntegerv(GL_VIEWPORT,viewport);
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,sourceColor);
    if(sourceWidth!=viewport[2] || sourceHeight!=viewport[3]) {
        sourceWidth=viewport[2];sourceHeight=viewport[3];
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA16F,sourceWidth,sourceHeight,0,GL_RGBA,GL_FLOAT,nullptr);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D,sourceDepth);
        glTexImage2D(GL_TEXTURE_2D,0,GL_DEPTH_COMPONENT24,sourceWidth,sourceHeight,0,GL_DEPTH_COMPONENT,GL_UNSIGNED_INT,nullptr);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    }
    glBindTexture(GL_TEXTURE_2D,sourceColor);glCopyTexSubImage2D(GL_TEXTURE_2D,0,0,0,viewport[0],viewport[1],sourceWidth,sourceHeight);
    glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,sourceDepth);
    glCopyTexSubImage2D(GL_TEXTURE_2D,0,0,0,viewport[0],viewport[1],sourceWidth,sourceHeight);
    glActiveTexture(GL_TEXTURE2);glBindTexture(GL_TEXTURE_CUBE_MAP,skyCube);
    glUseProgram(lensProgram);
    const bool core=GalacticCores::IsCore(coreId);
    const auto preset=core?GalacticCores::Get(coreId):GalacticCores::Get(GalacticCores::firstId);
    const bool hotFlow=core && preset.hotFlow;
    const auto orientation=hotFlow?glm::mat4(GalacticCores::HotFlowFrame()):DiskOrientation();
    const auto inverse=glm::inverse(vp),diskInverse=glm::inverse(orientation);
    const auto cameraFloat=glm::vec3(camera),axis=glm::vec3(orientation*glm::vec4(0,0,1,0));
    glUniformMatrix4fv(glGetUniformLocation(lensProgram,"inverseViewProjection"),1,GL_FALSE,glm::value_ptr(inverse));
    glUniformMatrix4fv(glGetUniformLocation(lensProgram,"viewProjection"),1,GL_FALSE,glm::value_ptr(vp));
    glUniformMatrix4fv(glGetUniformLocation(lensProgram,"inverseDiskOrientation"),1,GL_FALSE,glm::value_ptr(diskInverse));
    glUniform3fv(glGetUniformLocation(lensProgram,"cameraPosition"),1,glm::value_ptr(cameraFloat));
    glUniform3fv(glGetUniformLocation(lensProgram,"observerBeta"),1,glm::value_ptr(beta));
    glUniform3fv(glGetUniformLocation(lensProgram,"physicalObserverBeta"),1,glm::value_ptr(physicalBeta));
    glUniform1i(glGetUniformLocation(lensProgram,"physicalObserver"),physicalObserver);
    glUniform3fv(glGetUniformLocation(lensProgram,"diskAxis"),1,glm::value_ptr(axis));
    glUniform1f(glGetUniformLocation(lensProgram,"horizonRadius"),horizonRadius);
    glUniform1f(glGetUniformLocation(lensProgram,"diskEmission"),core?static_cast<float>(preset.emission):diskBrightness);
    glUniform1f(glGetUniformLocation(lensProgram,"diskTemperature"),core?static_cast<float>(preset.temperature):12000.f);
    glUniform1i(glGetUniformLocation(lensProgram,"quasarJet"),core && preset.quasar);
    glUniform1i(glGetUniformLocation(lensProgram,"accretionModel"),hotFlow?1:0);
    glUniform1i(glGetUniformLocation(lensProgram,"hotFlowDiagnostic"),hotFlowDiagnostic);
    glUniform1f(glGetUniformLocation(lensProgram,"flowTime"),static_cast<float>(seconds*Astronomy::lightSpeedAu/(.5*horizonRadius)));
    const double lensSpin=core?preset.spin:spin;
    const bool spinning=std::abs(lensSpin)>.0001;
    glUniform1f(glGetUniformLocation(lensProgram,"blackHoleSpin"),static_cast<float>(lensSpin));
    glUniform1i(glGetUniformLocation(lensProgram,"rayStepBudget"),hotFlow?std::max(256,rayBudget):rayBudget);
    glUniform1i(glGetUniformLocation(lensProgram,"lensingDiagnostic"),lensingDiagnostic);
    const double horizon=KerrSpacetime::Horizon(horizonRadius,lensSpin);
    glUniform1f(glGetUniformLocation(lensProgram,"captureRadius"),static_cast<float>(horizon));
    // Exterior incoming rays below the innermost photon orbit cannot scatter
    // back to infinity. Stop there before past-directed KS momenta diverge.
    // Near/interior observers retain the actual horizon/inner-limit handling.
    const double photonStop=KerrSpacetime::MinimumPhotonOrbit(horizonRadius,lensSpin)*.99;
    const double observerMetricRadius=KerrSpacetime::Radius(glm::dmat3(diskInverse)*camera,horizonRadius,lensSpin);
    glUniform1f(glGetUniformLocation(lensProgram,"captureStopRadius"),static_cast<float>(
        spinning && observerMetricRadius>photonStop*1.02?std::max(horizon,photonStop):horizon*.9999));
    glUniform2f(glGetUniformLocation(lensProgram,"diskRadii"),spinning?static_cast<float>(KerrSpacetime::ProgradeIsco(horizonRadius,lensSpin)):innerDiskRadius,hotFlow?horizonRadius*24.f:core && preset.quasar?horizonRadius*12.f:core?outerDiskRadius:GargantuaDiskOuterRadius());
    glUniform1f(glGetUniformLocation(lensProgram,"diskPhase"),static_cast<float>(std::remainder(OrbitalMechanics::MeanMotion(gravitationalParameter,core?outerDiskRadius:GargantuaDiskOuterRadius())*seconds,glm::two_pi<double>())));
    glUniform1f(glGetUniformLocation(lensProgram,"plasmaTime"),static_cast<float>(seconds/timeScale));
    glUniform1i(glGetUniformLocation(lensProgram,"sourceScene"),0);glUniform1i(glGetUniformLocation(lensProgram,"sourceDepth"),1);glUniform1i(glGetUniformLocation(lensProgram,"sky"),2);
    const bool blending=glIsEnabled(GL_BLEND);glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);glBindVertexArray(screenVao);glDrawArrays(GL_TRIANGLES,0,3);
    if(blending)glEnable(GL_BLEND);
    glBindVertexArray(0);glEnable(GL_DEPTH_TEST);glActiveTexture(GL_TEXTURE0);
}

void BlackHole::Release() {
    milkyWay.Release();
    stellarActivity.Release();
    if(stellarProperties)glDeleteBuffers(1,&stellarProperties);stellarProperties=0;
    if(stellarResiduals)glDeleteBuffers(1,&stellarResiduals);stellarResiduals=0;
    for(auto texture:{skyCube,sourceColor,sourceDepth})if(texture)glDeleteTextures(1,&texture);
    skyCube=sourceColor=sourceDepth=0;sourceWidth=sourceHeight=0;
    atmosphere.Release();
    galaxies.Release();
    for (auto* mesh : {&sphere, &backgroundStars, &craftRing, &rangerMesh, &craftTrail, &photonPackets, &guides, &waterPatch}) {
        if (mesh->vao)
            glDeleteVertexArrays(1, &mesh->vao);
        if (mesh->buffer)
            glDeleteBuffers(1, &mesh->buffer);
        *mesh = {};
    }
    if (program) {
        glDeleteProgram(program);
        program = 0;
    }
    if (lensProgram) {
        glDeleteProgram(lensProgram);
        lensProgram = 0;
    }
    if (screenVao) {
        glDeleteVertexArrays(1, &screenVao);
        screenVao = 0;
    }
}

void BlackHole::RenderCore(const glm::mat4& vp,const glm::dvec3& localCamera,int coreId,double seconds) const{
    const auto core=GalacticCores::Get(coreId);
    if(!skyRefreshDeferred && (skyCubeDirty || glm::length(skyCubeOrigin-core.position)>1.0))BuildSkyCube(core.position);
    RenderBackground(vp,core.position+localCamera);
    const double scale=Astronomy::gargantuaHorizonAu/core.RadiusAu();
    RenderLensing(vp,localCamera*scale,seconds*scale,glm::vec3(0),glm::vec3(0),false,coreId);
}

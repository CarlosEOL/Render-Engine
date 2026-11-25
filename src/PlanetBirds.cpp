#include "PlanetBirds.h"
#include "Astronomy.h"
#include "FlockSteering.h"
#include "PlanetTerrain.h"
#include "PlanetWeather.h"
#include "glad/glad.h"
#include <glm/gtc/constants.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cmath>

unsigned int compileShaderProgram(const char*, const char*);

namespace {
constexpr double cellSize = 210, viewRange = 585, fixedStep = 1.0 / 60.0;
constexpr std::array<float, 3> speeds{13.f, 11.f, 17.f};
constexpr std::array<float, 3> spans{1.55f, .49f, 1.86f};

std::uint32_t CellKey(unsigned seed, const glm::ivec3& cell) {
    return seed ^ std::uint32_t(cell.x) * 374761393u
        ^ std::uint32_t(cell.y) * 668265263u ^ std::uint32_t(cell.z) * 2246822519u;
}
}

bool PlanetBirds::Initialize() {
    const char* vertex = R"(#version 330 core
layout(location=0) in vec3 point;
layout(location=1) in vec3 pointNormal;
layout(location=2) in vec3 plumage;
layout(location=3) in float wing;
layout(location=4) in vec4 positionScale;
layout(location=5) in vec4 headingBank;
layout(location=6) in vec4 wingPose;
uniform mat4 viewProjection;
uniform vec3 localUp;
out vec3 normal, material, relativeMetres;
void main(){
    vec3 p=point,n=pointNormal;
    if(abs(wing)>.001){
        float side=sign(wing), root=side*.065;
        float angle=side*(wingPose.x+wingPose.y*abs(wing));
        mat2 flap=mat2(cos(angle),-sin(angle),sin(angle),cos(angle));
        p.yz=flap*vec2(p.y,p.z-root)+vec2(0,root);
        n.yz=flap*n.yz;
    }
    vec3 forward=normalize(headingBank.xyz), across=normalize(cross(forward,localUp));
    vec3 up=cross(across,forward);
    float c=cos(headingBank.w),s=sin(headingBank.w);
    mat3 frame=mat3(forward,up*c+across*s,across*c-up*s);
    relativeMetres=positionScale.xyz+frame*p*positionScale.w;
    normal=frame*n;material=plumage*wingPose.z;
    gl_Position=viewProjection*vec4(relativeMetres/149597870700.0,1);
}
)";
    const char* fragment = R"(#version 330 core
in vec3 normal,material,relativeMetres;
uniform vec3 light,localUp;
out vec4 color;
void main(){
    vec3 n=normalize(normal);
    if(!gl_FrontFacing)n=-n;
    float day=smoothstep(-.16,.20,dot(light,localUp));
    float diffuse=max(0.0,dot(n,light));
    color=vec4(material*(.019+day*(.28+.71*diffuse)),0);
    gl_FragDepth=log2(1.0+length(relativeMetres)*(1e12/149597870700.0))/log2(1.0+20000.0*1e12);
}
)";
    program = compileShaderProgram(vertex, fragment);
    GLint linked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) { Release(); return false; }
    std::vector<Vertex> mesh;
    for (unsigned i = 0; i < speciesCount; ++i) {
        const auto part = BuildMesh(static_cast<Species>(i));
        vertexStart[i] = static_cast<int>(mesh.size());
        vertexCount[i] = static_cast<int>(part.size());
        mesh.insert(mesh.end(), part.begin(), part.end());
    }
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vertices);
    glGenBuffers(1, &instances);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vertices);
    glBufferData(GL_ARRAY_BUFFER, mesh.size() * sizeof(Vertex), mesh.data(), GL_STATIC_DRAW);
    static constexpr std::array offsets{offsetof(Vertex, position), offsetof(Vertex, normal),
        offsetof(Vertex, color), offsetof(Vertex, wing)};
    for (unsigned i = 0; i < offsets.size(); ++i) {
        glVertexAttribPointer(i, i == 3 ? 1 : 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
            reinterpret_cast<void*>(offsets[i]));
        glEnableVertexAttribArray(i);
    }
    glBindBuffer(GL_ARRAY_BUFFER, instances);
    for (unsigned i = 0; i < 3; ++i) {
        glVertexAttribPointer(4 + i, 4, GL_FLOAT, GL_FALSE, sizeof(Instance),
            reinterpret_cast<void*>(i * sizeof(glm::vec4)));
        glEnableVertexAttribArray(4 + i);
        glVertexAttribDivisor(4 + i, 1);
    }
    glBindVertexArray(0);
    return true;
}

std::vector<PlanetBirds::Vertex> PlanetBirds::BuildMesh(Species species) {
    const bool gull = species == Species::Gull;
    const bool goose = species == Species::Goose;
    const glm::vec3 breast = gull ? glm::vec3(.91f,.91f,.85f)
        : goose ? glm::vec3(.49f,.39f,.27f) : glm::vec3(.71f,.60f,.41f);
    const glm::vec3 back = gull ? glm::vec3(.45f,.50f,.56f)
        : goose ? glm::vec3(.28f,.23f,.17f) : glm::vec3(.028f,.055f,.092f);
    const glm::vec3 dark = {.021f,.026f,.031f};
    std::vector<Vertex> mesh;
    mesh.reserve(1500);
    const auto ellipsoid = [&](glm::vec3 centre, glm::vec3 size, glm::vec3 upper, glm::vec3 lower,
        int rings, int sides) {
        const auto point = [&](int ring, int side) {
            const float theta = glm::pi<float>() * ring / rings, phi = glm::two_pi<float>() * side / sides;
            const glm::vec3 v(-std::cos(theta), std::sin(theta)*std::cos(phi), std::sin(theta)*std::sin(phi));
            return Vertex{centre+size*v, glm::normalize(v/size), glm::mix(lower,upper,glm::smoothstep(-.45f,.65f,v.y)), 0};
        };
        for (int ring = 0; ring < rings; ++ring) for (int side = 0; side < sides; ++side) {
            const auto a=point(ring,side), b=point(ring+1,side), c=point(ring+1,side+1), d=point(ring,side+1);
            if (ring) mesh.insert(mesh.end(),{a,d,b});
            if (ring+1<rings) mesh.insert(mesh.end(),{d,c,b});
        }
    };
    ellipsoid({-.015f,0,0},{goose?.209f:.158f,goose?.090f:.065f,goose?.083f:.066f},back,breast,8,12);
    const float head = goose ? .330f : gull ? .137f : .117f;
    const float headY = goose ? .062f : .043f;
    const glm::vec3 headUpper = goose ? dark : gull ? breast : back;
    const glm::vec3 throat = gull ? breast : goose ? dark : glm::vec3(.33f,.11f,.043f);
    if (goose) ellipsoid({.228f,.039f,0},{.143f,.043f,.035f},dark,dark,5,9);
    ellipsoid({head,headY,0},{.054f,.043f,.040f},headUpper,throat,6,10);
    const float billLength = gull ? .057f : goose ? .040f : .020f;
    ellipsoid({head+.040f+billLength*.35f,headY-.012f,0},{billLength,.013f,.015f},
        gull?glm::vec3(.82f,.54f,.089f):dark,gull?glm::vec3(.60f,.32f,.049f):dark,4,8);
    for (float side : {-1.f,1.f}) {
        if (goose) ellipsoid({head-.010f,headY-.013f,side*.033f},{.029f,.019f,.008f},
            glm::vec3(.80f,.81f,.73f),glm::vec3(.64f,.67f,.61f),4,8);
        ellipsoid({head+.019f,headY+.013f,side*.034f},{.009f,.008f,.007f},dark,dark,3,7);
    }

    struct WingSection {float span, leading, trailing, rise, camber;};
    const std::array<WingSection,6> gullWing{{
        {.065f,.066f,-.140f,.005f,.024f}, {.17f,.095f,-.146f,.021f,.025f},
        {.29f,.055f,-.131f,.034f,.021f}, {.40f,-.009f,-.140f,.029f,.013f},
        {.475f,-.095f,-.182f,.018f,.007f}, {.50f,-.168f,-.183f,.010f,.002f}}};
    const std::array<WingSection,6> swallowWing{{
        {.065f,.066f,-.112f,.004f,.019f}, {.16f,.071f,-.118f,.021f,.023f},
        {.265f,.018f,-.123f,.033f,.018f}, {.385f,-.074f,-.175f,.029f,.012f},
        {.46f,-.179f,-.248f,.020f,.006f}, {.50f,-.273f,-.288f,.015f,.002f}}};
    const std::array<WingSection,6> gooseWing{{
        {.065f,.068f,-.154f,.007f,.031f}, {.171f,.095f,-.177f,.025f,.032f},
        {.286f,.079f,-.181f,.034f,.027f}, {.390f,.018f,-.190f,.031f,.022f},
        {.466f,-.047f,-.182f,.020f,.014f}, {.50f,-.113f,-.153f,.008f,.004f}}};
    const auto& sections = gull ? gullWing : goose ? gooseWing : swallowWing;
    constexpr std::array<float,5> chord{0.f,.24f,.52f,.81f,1.f};
    const auto triangle = [&](Vertex a, Vertex b, Vertex c, float surfaceSide) {
        auto normal = glm::cross(b.position-a.position,c.position-a.position);
        if (normal.y*surfaceSide < 0) { std::swap(b,c); normal = -normal; }
        if (glm::dot(normal,normal) < 1e-14f) return;
        a.normal = b.normal = c.normal = glm::normalize(normal);
        mesh.insert(mesh.end(),{a,b,c});
    };

    for (float side : {-1.f,1.f}) {
        for (float skin : {-1.f,1.f}) {
            const auto wing = [&](std::size_t station, std::size_t strip) {
                const auto& section=sections[station];
                const float t=chord[strip];
                const float arch=4.f*t*(1.f-t)*section.camber;
                const float sweep=(section.span-.065f)/.435f;
                const float primary=glm::smoothstep(goose?.53f:.65f,.91f,sweep);
                const glm::vec3 coverts=skin>0?back:gull?breast:glm::mix(breast,back,.64f);
                const glm::vec3 flight=glm::mix(back,dark,gull?.65f:.47f);
                glm::vec3 plumage=glm::mix(coverts,flight,glm::smoothstep(.49f,1.f,t));
                plumage=glm::mix(plumage,dark,primary);
                if (gull && station==4 && strip==2) plumage=glm::mix(plumage,breast,.64f);
                return Vertex{{glm::mix(section.leading,section.trailing,t),
                    section.rise+arch*(skin>0?1.f:-.45f),side*section.span},{},plumage,side*(.12f+.88f*sweep)};
            };
            for (std::size_t station=0; station+1<sections.size(); ++station) {
                for (std::size_t strip=0; strip+1<chord.size(); ++strip) {
                    const auto a=wing(station,strip), b=wing(station+1,strip);
                    const auto c=wing(station+1,strip+1), d=wing(station,strip+1);
                    triangle(a,b,d,skin);
                    triangle(d,b,c,skin);
                }
            }
        }
        const auto feather = [&](glm::vec3 p,glm::vec3 color) {return Vertex{p,{},color,0};};
        const glm::vec3 tail=gull?breast:dark;
        const float streamer=gull?-.27f:goose?-.30f:-.385f;
        const float notch=gull?-.265f:goose?-.281f:-.221f;
        triangle(feather({-.102f,.006f,0},back),feather({-.159f,-.002f,side*.049f},back),
            feather({streamer,-.020f,side*(goose?.078f:.083f)},tail),1);
        triangle(feather({-.102f,.006f,0},back),feather({streamer,-.020f,side*(goose?.078f:.083f)},tail),
            feather({notch,-.012f,0},tail),1);
    }
    return mesh;
}

float PlanetBirds::GroundElevation(const Habitat& h, const glm::vec3& normal) {
    return PlanetTerrain::SurfaceElevation(normal,h.seed,0,h.seaLevel,.003f,0,
        false,nullptr,h.earth,0,h.layeredTerrain)*static_cast<float>(h.radiusMetres);
}

void PlanetBirds::Reset() {
    flocks.clear();
    haveCell = haveTime = false;
    stepRemainder = 0;
    ClearCounts();
}

void PlanetBirds::UpdateFlocks(const Habitat& h, const glm::dvec3& cameraLocal) {
    const auto surface = glm::normalize(cameraLocal)*h.radiusMetres;
    const glm::ivec3 current(glm::floor(surface/cellSize));
    if (haveCell && current==cameraCell) return;
    cameraCell=current;
    haveCell=true;
    flocks.erase(std::remove_if(flocks.begin(),flocks.end(),[&](const Flock& flock){
        return glm::length(glm::normalize(flock.origin)*h.radiusMetres-surface)>viewRange+110;
    }),flocks.end());
    for (int z=-3; z<=3; ++z) for (int y=-3; y<=3; ++y) for (int x=-3; x<=3; ++x) {
        if(flocks.size()>=maxFlocks) return;
        const auto cell=current+glm::ivec3(x,y,z);
        const auto key=CellKey(h.seed,cell);
        if(PlanetWeather::Unit(key)<.36f) continue;
        const glm::dvec3 sample=(glm::dvec3(cell)+glm::dvec3(.5))*cellSize;
        if(std::abs(glm::length(sample)-h.radiusMetres)>cellSize*.51 || glm::length(sample-surface)>viewRange-70)continue;
        if(std::any_of(flocks.begin(),flocks.end(),[&](const Flock& flock){return flock.cell==cell;}))continue;
        const auto up=glm::normalize(sample);
        const float elevation=GroundElevation(h,glm::vec3(up));
        const float depth=PlanetTerrain::WaterDepthMetres(glm::vec3(up),h.seed,h.seaLevel,h.radiusMetres,
            h.earth,0,h.layeredTerrain);
        const double air=h.temperature+10.0-38.0*up.y*up.y-std::max(0.f,elevation)*.005;
        if(air<248 || air>322 || depth>190 || elevation>4400)continue;
        const float type=PlanetWeather::Unit(key+1);
        const Species species=depth>4?(type<.70f?Species::Gull:Species::Goose)
            :type<.55f?Species::Swallow:type<.86f?Species::Goose:Species::Gull;
        const auto east=glm::normalize(glm::cross(std::abs(up.y)>.95?glm::dvec3(1,0,0):glm::dvec3(0,1,0),up));
        Flock flock{};
        flock.cell=cell;
        flock.origin=up*(h.radiusMetres+elevation);
        flock.frame=glm::dmat3(east,up,glm::cross(east,up));
        flock.species=species;
        flock.count=species==Species::Swallow?12:7;
        flock.course=glm::two_pi<float>()*PlanetWeather::Unit(key+2);
        flock.flightHeight=(species==Species::Swallow?19.f:33.f)+18.f*PlanetWeather::Unit(key+3);
        const float speed=speeds[static_cast<unsigned>(species)];
        for(std::size_t i=0;i<flock.count;++i){
            const unsigned bird=key+59*static_cast<unsigned>(i);
            const auto heading=flock.course+.55f*(PlanetWeather::Unit(bird+4)-.5f);
            flock.birds[i]={
                {(PlanetWeather::Unit(bird+5)-.5f)*26,flock.flightHeight+(PlanetWeather::Unit(bird+6)-.5f)*7,
                    (PlanetWeather::Unit(bird+7)-.5f)*26},
                speed*glm::vec3(std::cos(heading),0,std::sin(heading)),
                glm::two_pi<float>()*PlanetWeather::Unit(bird+8),.88f+.24f*PlanetWeather::Unit(bird+9)};
        }
        flocks.push_back(flock);
    }
}

void PlanetBirds::Simulate(const Habitat& h, float step, const PlanetWeather::State& weather) {
    using namespace FlockSteering;
    for(auto& flock:flocks){
        const auto species=static_cast<unsigned>(flock.species);
        const float cruise=speeds[species], sight=flock.species==Species::Swallow?23.f:40.f;
        const float spacing=flock.species==Species::Swallow?2.3f:5.7f;
        const auto previous=flock.birds;
        flock.age+=step;
        const float age=static_cast<float>(flock.age);
        const float course=flock.course+age*(flock.species==Species::Goose?.105f:.22f);
        const glm::vec3 heading(std::cos(course),0,std::sin(course));
        const auto radial=glm::vec3(flock.frame[1]);
        const auto wind=Limit(glm::vec3(glm::transpose(flock.frame)*glm::dvec3(PlanetWeather::SurfaceWind(weather,radial)))*.15f,3.f);
        const float originAltitude=static_cast<float>(glm::length(flock.origin)-h.radiusMetres);
        for(std::size_t i=0;i<flock.count;++i){
            const auto& old=previous[i];
            auto& bird=flock.birds[i];
            const glm::vec3 horizontal(old.position.x,0,old.position.z);
            const auto projected=flock.origin+flock.frame*glm::dvec3(horizontal);
            const float ground=GroundElevation(h,glm::vec3(glm::normalize(projected)))-originAltitude;
            const float tether=glm::smoothstep(36.f,120.f,glm::length(horizontal));
            const auto goal=Direction(heading*(1-tether)-Direction(horizontal)*tether);
            glm::vec3 desired=goal*cruise+Neighbors(previous.data(),flock.count,i,sight,spacing,cruise)+wind;
            desired.y+=glm::clamp((ground+flock.flightHeight+2.2f*std::sin(age*.6f+bird.phase)-old.position.y)*.7f,-5.f,5.f);
            glm::vec3 velocity=old.velocity+Limit(desired-old.velocity,flock.species==Species::Goose?8.f:12.f)*step;
            velocity=Direction(velocity)*glm::clamp(glm::length(velocity),cruise*.55f,cruise*1.35f);
            bird.position=old.position+velocity*step;
            bird.position.y=std::max(bird.position.y,ground+8.f);
            bird.velocity=velocity;
            const float turn=glm::cross(Direction(old.velocity),Direction(velocity)).y/step;
            bird.bank=glm::mix(bird.bank,glm::clamp(turn*.60f,-.65f,.65f),step*4.f);
        }
    }
}

void PlanetBirds::Render(const Habitat& h, const glm::dmat3& rotation,
    const glm::dvec3& cameraFromPlanetAu,const glm::mat4& viewProjection,
    const glm::vec3& light,double seconds,const PlanetWeather::State& weather) {
    const auto cameraLocal=glm::transpose(rotation)*cameraFromPlanetAu*Astronomy::metresPerAu;
    const double radius=glm::length(cameraLocal);
    if(radius<h.radiusMetres-550 || radius>h.radiusMetres+5300) return;
    const auto localUp=glm::vec3(cameraLocal/radius);
    if(radius-h.radiusMetres-GroundElevation(h,localUp)>viewRange) return;
    const auto up=glm::vec3(rotation*glm::dvec3(localUp));
    if(glm::dot(up,light)<-.16f) return;
    if(haveTime && (seconds<lastTime || seconds-lastTime>10)) Reset();
    UpdateFlocks(h,cameraLocal);
    stepRemainder+=haveTime?std::min(seconds-lastTime,.25):0;
    haveTime=true;
    lastTime=seconds;
    while(stepRemainder>=fixedStep){
        Simulate(h,static_cast<float>(fixedStep),weather);
        stepRemainder-=fixedStep;
    }
    for(auto& batch:visible)batch.clear();
    for(const auto& flock:flocks){
        const auto index=static_cast<unsigned>(flock.species);
        const glm::dmat3 orientation=rotation*flock.frame;
        const auto centre=rotation*(flock.origin-cameraLocal);
        const float birdTime=static_cast<float>(flock.age+stepRemainder);
        for(std::size_t i=0;i<flock.count;++i){
            const auto& bird=flock.birds[i];
            const auto relative=glm::vec3(centre+orientation*glm::dvec3(bird.position+bird.velocity*static_cast<float>(stepRemainder)));
            if(glm::dot(relative,relative)>viewRange*viewRange)continue;
            const auto clip=viewProjection*glm::vec4(relative/static_cast<float>(Astronomy::metresPerAu),1);
            if(clip.w<=0 || std::abs(clip.x)>clip.w*1.06f || std::abs(clip.y)>clip.w*1.06f)continue;
            const float flutter=index==1?8.f:index==2?3.1f:3.7f;
            const float glide=index==1?.9f:glm::smoothstep(-.60f,.4f,std::sin(birdTime*1.05f+bird.phase));
            const float stroke=std::sin(birdTime*flutter*glm::two_pi<float>()+bird.phase);
            visible[index].push_back({glm::vec4(relative,spans[index]*bird.scale),
                glm::vec4(glm::vec3(orientation*glm::dvec3(FlockSteering::Direction(bird.velocity))),bird.bank),
                {stroke*.46f*glide+.08f,stroke*.30f*glide,.86f+.14f*bird.scale,0}});
        }
    }
    if(std::all_of(visible.begin(),visible.end(),[](const auto& batch){return batch.empty();}))return;
    const bool blend=glIsEnabled(GL_BLEND),cull=glIsEnabled(GL_CULL_FACE);
    glDisable(GL_BLEND);glDisable(GL_CULL_FACE);
    glUseProgram(program);
    glUniformMatrix4fv(glGetUniformLocation(program,"viewProjection"),1,GL_FALSE,glm::value_ptr(viewProjection));
    glUniform3fv(glGetUniformLocation(program,"light"),1,glm::value_ptr(light));
    glUniform3fv(glGetUniformLocation(program,"localUp"),1,glm::value_ptr(up));
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER,instances);
    for(unsigned i=0;i<speciesCount;++i){
        if(visible[i].empty())continue;
        glBufferData(GL_ARRAY_BUFFER,visible[i].size()*sizeof(Instance),visible[i].data(),GL_DYNAMIC_DRAW);
        glDrawArraysInstanced(GL_TRIANGLES,vertexStart[i],vertexCount[i],static_cast<int>(visible[i].size()));
        visibleCount+=visible[i].size();
        visibleSpecies|=1u<<i;
    }
    glBindVertexArray(0);
    if(blend)glEnable(GL_BLEND);
    if(cull)glEnable(GL_CULL_FACE);
}

void PlanetBirds::Release() {
    if(program)glDeleteProgram(program);
    if(vao)glDeleteVertexArrays(1,&vao);
    if(vertices)glDeleteBuffers(1,&vertices);
    if(instances)glDeleteBuffers(1,&instances);
    program=vao=vertices=instances=0;
    Reset();
}

#include <glad/glad.h>
#include "EarthWonders.h"
#include "Astronomy.h"
#include "PlanetTerrain.h"
#ifdef RENDER_ENGINE_DX12
#include "dx12/Dx12SceneBridge.h"
#endif

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <string_view>
#include <vector>

unsigned int compileShaderProgram(const char*,const char*);

namespace EarthWonders {

glm::dmat3 Site::Frame() const {
    const auto lat=glm::radians(latitude),lon=glm::radians(longitude);
    const glm::dvec3 up(std::cos(lat)*std::cos(lon),std::sin(lat),-std::cos(lat)*std::sin(lon));
    const glm::dvec3 east(-std::sin(lon),0,-std::cos(lon));
    return {east,up,glm::cross(east,up)};
}

double Site::Elevation() const {
    const double terrain=PlanetTerrain::EarthGeography(glm::vec3(Direction()),!submerged).x;
    return submerged?terrain:std::max(0.0,terrain);
}

namespace {
using V=glm::vec3;
using Paint=glm::vec4;
struct Vertex { V position,normal; Paint paint; };

const Paint limestone(.67f,.48f,.29f,0), paleStone(.79f,.69f,.51f,0);
const Paint sandstone(.53f,.37f,.23f,0), mortar(.35f,.29f,.22f,0);
const Paint marble(.92f,.91f,.83f,0), darkStone(.14f,.15f,.13f,0);
const Paint bronze(.62f,.42f,.15f,0), oldStone(.49f,.53f,.47f,0);
const Paint bathStone(.32f,.46f,.43f,0), inlay(.10f,.70f,.60f,.72f);

struct Builder {
    const Site& site;
    const glm::dmat3 frame=site.Frame();
    const double elevation=site.Elevation();
    std::vector<Vertex> vertices;

    float Ground(float x,float z) const {
        const auto n=glm::normalize(frame[1]+(frame[0]*double(x)+frame[2]*double(z))/6371008.4);
        return static_cast<float>(PlanetTerrain::EarthGeography(V(n),!site.submerged).x-elevation);
    }

    void Triangle(V a,V b,V c,Paint paint,V outward) {
        auto normal=glm::cross(b-a,c-a);
        const float area=glm::length(normal);
        if(area<.00001f)return;
        normal/=area;
        if(glm::dot(normal,outward)<0){std::swap(b,c);normal=-normal;}
        vertices.insert(vertices.end(),{{a,normal,paint},{b,normal,paint},{c,normal,paint}});
    }

    void Quad(V a,V b,V c,V d,Paint paint,V outward) {
        Triangle(a,b,c,paint,outward);
        Triangle(a,c,d,paint,outward);
    }

    void SquareStage(V base,float lowWidth,float highWidth,float height,Paint paint) {
        const float lo=lowWidth*.5f,hi=highWidth*.5f;
        const V lower[]={{-lo,0,-lo},{lo,0,-lo},{lo,0,lo},{-lo,0,lo}};
        const V upper[]={{-hi,height,-hi},{hi,height,-hi},{hi,height,hi},{-hi,height,hi}};
        for(int i=0;i<4;++i){
            const int j=(i+1)%4;
            Quad(base+lower[i],base+lower[j],base+upper[j],base+upper[i],paint,lower[i]+lower[j]);
        }
        Quad(base+upper[0],base+upper[1],base+upper[2],base+upper[3],paint,{0,1,0});
    }

    void Box(V bottom,V size,Paint paint,V east={1,0,0}) {
        const V up(0,size.y,0),south=glm::cross(east,V(0,1,0));
        const V x=east*size.x*.5f,z=south*size.z*.5f;
        const V corners[]={bottom-x-z,bottom+x-z,bottom+x+z,bottom-x+z};
        for(int i=0;i<4;++i){
            const int j=(i+1)%4;
            Quad(corners[i],corners[j],corners[j]+up,corners[i]+up,paint,corners[i]+corners[j]-2.f*bottom);
        }
        Quad(corners[0]+up,corners[1]+up,corners[2]+up,corners[3]+up,paint,{0,1,0});
        Quad(corners[0],corners[1],corners[2],corners[3],paint,{0,-1,0});
    }

    void Round(V base,float low,float high,float height,Paint paint,int sides=32) {
        for(int i=0;i<sides;++i){
            const float a=glm::two_pi<float>()*i/sides,b=glm::two_pi<float>()*(i+1)/sides;
            const V r(std::cos(a),0,std::sin(a)),s(std::cos(b),0,std::sin(b)),top(0,height,0);
            Quad(base+r*low,base+s*low,base+s*high+top,base+r*high+top,paint,r+s);
            Triangle(base+top,base+r*high+top,base+s*high+top,paint,{0,1,0});
        }
    }

    // Open masonry arch. The piers and voussoirs have their own depth.
    void Arch(V origin,V along,float radius,float spring,float thickness,float depth,Paint paint) {
        const V depthAxis=glm::cross(along,V(0,1,0))*depth*.5f;
        for(int side:{-1,1})Box(origin+along*((radius+thickness*.5f)*side),
            {thickness,spring,depth},paint,along);
        const auto point=[&](float angle,float r){return origin+along*(std::cos(angle)*r)+V(0,spring+std::sin(angle)*r,0);};
        for(int i=0;i<10;++i){
            const float a=glm::pi<float>()*i/10,b=glm::pi<float>()*(i+1)/10;
            const V innerA=point(a,radius),innerB=point(b,radius);
            const V outerA=point(a,radius+thickness),outerB=point(b,radius+thickness);
            const Paint stone=glm::mix(paint,Paint(.9f,.83f,.67f,paint.a),.06f*(i%3));
            for(int side:{-1,1}){
                const V edge=depthAxis*float(side);
                Quad(innerA+edge,innerB+edge,outerB+edge,outerA+edge,stone,edge);
            }
            Quad(innerA-depthAxis,innerB-depthAxis,innerB+depthAxis,innerA+depthAxis,stone,origin-innerA);
            Quad(outerA-depthAxis,outerB-depthAxis,outerB+depthAxis,outerA+depthAxis,stone,outerA-origin);
        }
    }
};

void Giza(Builder& mesh) {
    struct Pyramid { float x,z,width,height; int courses; };
    const Pyramid pyramids[]={{328,-349,230,139,88},{0,0,215,136,84},{-246,400,103,61,45},
        {-170,480,29,18,18},{-221,495,27,15,16},{-268,509,27,12,13}};
    for(const auto& p:pyramids){
        const float y=mesh.Ground(p.x,p.z);
        mesh.Box({p.x,y-18,p.z},{p.width+3,18,p.width+3},sandstone);
        const float step=p.height/p.courses;
        for(int i=0;i<p.courses;++i){
            const float progress=float(i)/p.courses;
            const float width=glm::mix(p.width,4.f,progress);
            Paint stone=glm::mix(limestone,paleStone,.13f*float((i*7)%5));
            if(p.x==0 && progress>.70f)stone=glm::mix(paleStone,marble,.30f);
            if(p.x==-246 && i<6)stone=Paint(.53f,.32f,.26f,0);
            mesh.SquareStage({p.x,y+i*step,p.z},width,std::max(2.f,width-(p.width-4)/p.courses*.82f),step,stone);
        }
    }
    // Mortuary causeway and temple at the foot of Khafre's pyramid.
    const float ground=mesh.Ground(151,24);
    mesh.Box({151,ground-5,24},{48,9,39},sandstone);
    for(int i=0;i<6;++i)mesh.Box({151+i*43.f,mesh.Ground(151+i*43.f,29+i*12.f)-7,29+i*12.f},
        {49,8,8},limestone,glm::normalize(V(43,0,12)));
}

void Castillo(Builder& mesh) {
    mesh.Box({0,-6,0},{56,6,56},sandstone);
    for(int i=0;i<9;++i){
        const float width=55-i*5.35f;
        mesh.SquareStage({0,i*2.5f,0},width,width-1.45f,2.50f,i%2?paleStone:limestone);
    }
    for(const V forward:{V(0,0,1),V(0,0,-1),V(1,0,0),V(-1,0,0)}){
        const auto right=glm::cross(V(0,1,0),forward);
        for(int i=0;i<70;++i){
            const V base=forward*(29-i*.35f)+V(0,i*.318f,0);
            mesh.Box(base,{5.7f,.34f,.37f},paleStone,right);
            for(int side:{-1,1})mesh.Box(base+right*(3.3f*side),{.90f,.56f,.37f},limestone,right);
        }
    }
    for(int side:{-1,1}){
        mesh.Box({4.9f*side,22.5f,0},{1.7f,5,10},limestone);
        for(int post:{-1,1})mesh.Box({2.8f*post,22.5f,4.6f*side},{2.3f,5,1.4f},paleStone);
    }
    mesh.Box({0,27.5f,0},{12,2.2f,11.7f},paleStone);
    mesh.Box({0,30,0},{8,.9f,8},limestone);
}

void Colosseum(Builder& mesh) {
    constexpr int bays=72;
    const auto ring=[](float angle,float inset){return V((94-inset)*std::cos(angle),0,(78-inset)*std::sin(angle));};
    for(int i=0;i<bays;++i){
        const float angle=glm::two_pi<float>()*i/bays,next=glm::two_pi<float>()*(i+1)/bays;
        const V a=ring(angle,0),b=ring(next,0),east=glm::normalize(b-a);
        const float span=glm::length(b-a);
        const V centre=(a+b)*.5f;
        const int floors=i<37?4:(i<48 || i>66?3:2);
        mesh.Box(centre+V(0,-8,0),{span+.2f,8,5},sandstone,east);
        for(int level=0;level<floors;++level){
            const V base=centre+V(0,level*12,0);
            if(level<3)mesh.Arch(base,east,span*.32f,6.0f,span*.17f,3.7f,paleStone);
            else mesh.Box(base,{span+.2f,9.2f,3.4f},limestone,east);
            mesh.Box(base+V(0,10.6f,0),{span+.35f,1.6f,4.6f},paleStone,east);
        }
        const V inner=(ring(angle,15)+ring(next,15))*.5f;
        mesh.Arch(inner,east,span*.22f,5.4f,span*.17f,2.4f,limestone);
        for(int step=0;step<12;++step){
            const float inset=19+step*2.7f;
            const V seat=(ring(angle,inset)+ring(next,inset))*.5f;
            mesh.Box(seat+V(0,11-step*.84f,0),{span*(1-inset/104)+.4f,1.6f,3.4f},sandstone,east);
        }
    }
    for(int i=-4;i<=4;++i)mesh.Box({i*7.f,-3,0},{.85f,5,52.f-float(std::abs(i))*3},mortar);
}

void TajMahal(Builder& mesh) {
    mesh.Box({0,-5,0},{95,11,95},marble);
    mesh.Box({0,6,0},{48,21,48},marble);
    mesh.Box({0,27,0},{45,4,45},paleStone);
    for(int side:{-1,1}){
        // Inset entrances read as dark niches in the marble façades.
        mesh.Box({0,6,24.f*side},{13.2f,19,.20f},darkStone);
        mesh.Arch({0,6,24.4f*side},{1,0,0},5.6f,11.5f,1.1f,.9f,marble);
        mesh.Box({24.f*side,6,0},{.20f,19,13.2f},darkStone);
        mesh.Arch({24.4f*side,6,0},{0,0,1},5.6f,11.5f,1.1f,.9f,marble);
        for(int flank:{-1,1}){
            mesh.Box({16.f*flank,11,24.2f*side},{5,9,.25f},sandstone);
            mesh.Arch({16.f*flank,9,24.5f*side},{1,0,0},2.2f,6.2f,.70f,.8f,marble);
        }
    }
    mesh.Round({0,31,0},11,11,8,marble);
    const float radii[]={10.8f,13,14.3f,14.8f,14.2f,12.9f,10.8f,8.5f,6.6f,4.9f,3.3f,1.9f,.7f};
    for(int i=0;i<12;++i)mesh.Round({0,39+2.2f*i,0},radii[i],radii[i+1],2.2f,marble,48);
    mesh.Round({0,65.4f,0},.8f,.17f,6,bronze);
    for(int x:{-1,1})for(int z:{-1,1}){
        mesh.Round({17.f*x,31,17.f*z},3.8f,3.8f,5,marble);
        mesh.Round({17.f*x,36,17.f*z},4.7f,.6f,5,marble);
        const V tower(42.f*x,6,42.f*z);
        mesh.Round(tower,2.4f,1.6f,33,marble);
        for(float y:{12.f,23.f,32.f})mesh.Round(tower+V(0,y,0),2.7f,2.7f,.8f,marble);
        mesh.Round(tower+V(0,33,0),1.9f,2.55f,3,marble);
        mesh.Round(tower+V(0,36,0),2.65f,.24f,3.3f,marble);
    }
    for(int i=0;i<10;++i){
        const float z=53+i*8.f,y=mesh.Ground(0,z);
        mesh.Box({0,y-.7f,z},{10,1,8.05f},paleStone);
        mesh.Box({0,y+.3f,z},{3.4f,.15f,8.08f},Paint(.09f,.26f,.24f,0));
    }
}

constexpr double earthMetres=6371008.4;
constexpr double microDegree=.000001;
constexpr double wallCellMidpoint=.00625;
enum class WallStyle : std::uint8_t { Masonry, Weathered, Earth, Trench, Trace, Natural, Watchtower, Beacon };

struct WallTile {
    std::int32_t latitude,longitude;
    std::uint32_t first,count;
};
struct WallSegment {
    std::int16_t lonA,latA,lonB,latB;
    WallStyle style;
    std::uint8_t reserved;
};
static_assert(sizeof(WallTile)==16 && sizeof(WallSegment)==10);

struct WallSurvey {
    std::vector<WallTile> tiles;
    std::vector<WallSegment> segments;

    WallSurvey() {
        std::ifstream input(PlanetTerrain::EarthAssetPath("../great-wall/routes.bin"),std::ios::binary|std::ios::ate);
        if(!input)return;
        const auto length=input.tellg();
        input.seekg(0);
        char signature[4];
        std::uint32_t tileCount=0,segmentCount=0;
        if(!input.read(signature,4) || std::string_view(signature,4)!="GW01" ||
           !input.read(reinterpret_cast<char*>(&tileCount),4) ||
           !input.read(reinterpret_cast<char*>(&segmentCount),4) || tileCount>100000 ||
           length!=std::streamoff(12+sizeof(WallTile)*tileCount+sizeof(WallSegment)*segmentCount))return;
        tiles.resize(tileCount);
        segments.resize(segmentCount);
        if(!input.read(reinterpret_cast<char*>(tiles.data()),sizeof(WallTile)*tiles.size()) ||
           !input.read(reinterpret_cast<char*>(segments.data()),sizeof(WallSegment)*segments.size())){
            tiles.clear();
            return;
        }
        std::erase_if(tiles,[&](const WallTile& tile){
            if(tile.first>segments.size() || tile.count>segments.size()-tile.first)return true;
            return std::none_of(segments.begin()+tile.first,segments.begin()+tile.first+tile.count,
                [](const WallSegment& segment){return segment.style!=WallStyle::Natural && segment.style<=WallStyle::Beacon;});
        });
    }
};

void BuildWallTile(Builder& mesh,const WallSurvey& survey,const WallTile& tile,bool detailed) {
    const auto transform=glm::transpose(mesh.frame);
    const auto toLocal=[&](int latitude,int longitude){
        const Site location{nullptr,nullptr,nullptr,nullptr,latitude*microDegree,longitude*microDegree,{}, {},0};
        return V(transform*(location.Direction()-mesh.frame[1])*earthMetres);
    };
    for(unsigned i=tile.first;i<tile.first+tile.count;++i){
        const auto& segment=survey.segments[i];
        if(segment.style==WallStyle::Natural || segment.style>WallStyle::Beacon)continue;
        const V a=toLocal(tile.latitude+segment.latA,tile.longitude+segment.lonA);
        if(segment.style>=WallStyle::Watchtower){
            const bool watchtower=segment.style==WallStyle::Watchtower;
            const float ground=a.y+mesh.Ground(a.x,a.z);
            const V base(a.x,ground,a.z);
            const float width=watchtower?12.f:8.2f;
            const float height=watchtower?8.5f:4.2f;
            const Paint stone=watchtower?oldStone:limestone;
            mesh.SquareStage(base-V(0,3,0),width+1.f,width,height*.14f+3.f,stone);
            mesh.SquareStage(base+V(0,height*.14f,0),width,width*.86f,height*.86f,stone);
            if(watchtower && detailed){
                mesh.Box(base+V(0,height,0),{width*.91f,.58f,width*.91f},paleStone);
                const float wall=width*.41f;
                for(int side:{-1,1})for(int post=-2;post<=2;++post){
                    mesh.Box(base+V(post*width*.18f,height+.58f,wall*side),{1.22f,.94f,.68f},stone);
                    mesh.Box(base+V(wall*side,height+.58f,post*width*.18f),{.68f,.94f,1.22f},stone);
                }
                for(int side:{-1,1}){
                    const float inset=glm::mix(width*.5f,width*.43f,.57f)+.02f;
                    mesh.Box(base+V(0,height*.60f,inset*side),{1.05f,.94f,.04f},darkStone);
                    mesh.Box(base+V(inset*side,height*.60f,0),{.04f,.94f,1.05f},darkStone);
                }
            }
            continue;
        }
        const V b=toLocal(tile.latitude+segment.latB,tile.longitude+segment.lonB);
        const V delta(b.x-a.x,0,b.z-a.z);
        const float length=glm::length(delta);
        if(length<.02f)continue;
        const V east=delta/length,across=glm::cross(east,V(0,1,0));
        const bool intact=segment.style==WallStyle::Masonry;
        const bool earth=segment.style==WallStyle::Earth;
        const bool trench=segment.style==WallStyle::Trench;
        const bool trace=segment.style==WallStyle::Trace;
        const float width=intact?3.15f:earth?2.3f:trench?2.1f:trace?1.15f:2.35f;
        const float height=intact?6.2f:earth?2.45f:trench?.20f:trace?.26f:2.5f;
        const float crown=intact?.88f:earth?.48f:trench?.67f:trace?.98f:.73f;
        const Paint sides=intact?oldStone:earth?limestone:trench?mortar:trace?mortar:Paint(.49f,.43f,.32f,0);
        const Paint top=intact?Paint(.66f,.65f,.55f,0):earth?Paint(.60f,.46f,.31f,0):sides;
        const int divisions=std::max(1,static_cast<int>(std::ceil(length/(detailed?12.f:28.f))));
        const float overlap=std::min(width*.11f,.34f);
        V previous=a-east*overlap;
        previous.y+=mesh.Ground(previous.x,previous.z);
        for(int step=1;step<=divisions;++step){
            V next=glm::mix(a,b,float(step)/divisions);
            if(step==divisions)next+=east*overlap;
            next.y+=mesh.Ground(next.x,next.z);
            const V base(0,trace || trench?-.25f:-9.f,0),rise(0,height,0);
            const V lower=across*width,upper=lower*crown;
            for(float side:{-1.f,1.f})
                mesh.Quad(previous+lower*side+base,next+lower*side+base,
                          next+upper*side+rise,previous+upper*side+rise,sides,across*side);
            mesh.Quad(previous-upper+rise,next-upper+rise,next+upper+rise,previous+upper+rise,top,{0,1,0});
            if(step==1)mesh.Quad(previous-lower+base,previous+lower+base,previous+upper+rise,previous-upper+rise,sides,-east);
            if(step==divisions)mesh.Quad(next-lower+base,next+lower+base,next+upper+rise,next-upper+rise,sides,east);
            if(intact && detailed){
                const float span=glm::dot(next-previous,east);
                const int merlons=static_cast<int>(span/2.6f);
                for(int merlon=0;merlon<merlons;++merlon)
                    for(float side:{-1.f,1.f})
                        mesh.Box(glm::mix(previous,next,(merlon+.5f)/merlons)+upper*(.88f*side)+rise,
                                 {1.4f,1.0f,.63f},oldStone,east);
            }
            previous=next;
        }
    }
}

void Atlantis(Builder& mesh) {
    // Original ruins laid out as two concentric colonnades around a drowned shrine.
    const auto circle=[](float angle,float r){return V(r*std::cos(angle),0,r*std::sin(angle));};
    for(float radius:{26.f,46.f})for(int i=0;i<28;++i){
        const float angle=glm::two_pi<float>()*i/28,next=glm::two_pi<float>()*(i+1)/28;
        V a=circle(angle,radius),b=circle(next,radius);
        a.y=mesh.Ground(a.x,a.z);b.y=mesh.Ground(b.x,b.z);
        const V along=glm::normalize(V(b.x-a.x,0,b.z-a.z));
        const V midpoint=(a+b)*.5f;
        mesh.Box(midpoint+V(0,-4,0),{glm::length(b-a)+.20f,5,4.2f},bathStone,along);
        const float height=i%7==0?3.0f:i%5==0?5.1f:9.0f;
        mesh.Round(a+V(0,-.75f,0),2.1f,2.1f,1.8f,oldStone,12);
        mesh.Round(a+V(0,1,0),1.25f,.95f,height,bathStone,12);
        mesh.Round(a+V(0,1+height,0),1.9f,1.9f,.8f,oldStone,12);
        if(height>8 && i%3!=0)mesh.Box(midpoint+V(0,10.65f,0),{glm::length(b-a)+.7f,1.55f,2.1f},oldStone,along);
        if(i%4==0)mesh.Round(a+V(0,1.7f,0),1.37f,1.34f,.22f,inlay,12);
    }
    mesh.Box({0,-5,1},{29,7,31},bathStone);
    mesh.Box({0,2,0},{22,2.4f,23},oldStone);
    for(int x:{-1,1})for(float z:{-8.f,0.f,8.f}){
        mesh.Round({8.f*x,4.4f,z},1.2f,1.f,8.2f,bathStone,14);
        mesh.Round({8.f*x,12.6f,z},1.8f,1.65f,1.4f,oldStone,14);
    }
    mesh.Box({0,14,0},{21,1.9f,20},oldStone);
    mesh.SquareStage({0,15.9f,0},20,9,3.1f,bathStone);
    mesh.Round({0,19,0},3.4f,.5f,5.f,bathStone,14);
    for(float z:{24.f,41.f}){
        const float ground=mesh.Ground(0,z);
        const V base(0,ground+1,z);
        mesh.Arch(base,{1,0,0},4.6f,5.7f,1.35f,2.8f,oldStone);
        for(float x:{-5.3f,5.3f})mesh.Box(base+V(x,1,1.43f),{.42f,4.2f,.17f},inlay);
    }
    for(int i=0;i<38;++i){
        const V debris=circle(i*2.39996f,17.f+float((i*13)%35));
        const float scale=.6f+(i%5)*.29f;
        mesh.Box({debris.x,mesh.Ground(debris.x,debris.z)-.5f,debris.z},{2.4f*scale,1.3f*scale,1.2f*scale},
            bathStone,glm::normalize(V(std::cos(float(i)),0,std::sin(float(i)))));
    }
}

using Model=void(*)(Builder&);
constexpr unsigned mutianyu=4;
constexpr Model models[]={Giza,Castillo,Colosseum,TajMahal,nullptr,Atlantis};
static_assert(std::size(models)==sites.size());

#include "EarthLandmarkModels.inl"

const char* vertex=R"(#version 330 core
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec4 paint;
uniform mat4 viewProjection;
uniform mat3 siteFrame;
uniform vec3 originMetres;
out vec3 relativeMetres,localPoint,worldNormal;
out vec4 masonry;
void main(){
    relativeMetres=originMetres+siteFrame*position;
    localPoint=position;worldNormal=siteFrame*normal;masonry=paint;
    gl_Position=viewProjection*vec4(relativeMetres/149597870700.0,1);
}
)";
const char* fragment=R"(#version 330 core
in vec3 relativeMetres,localPoint,worldNormal;
in vec4 masonry;
uniform vec3 sun;
uniform bool underwater;
out vec4 color;
float grain(vec3 p){return fract(sin(dot(p,vec3(47.3,19.7,77.1)))*43758.5);}
void main(){
    float metreDetail=1.0-smoothstep(.12,.85,length(fwidth(localPoint)));
    vec2 blocks=vec2(dot(localPoint.xz,vec2(.57,.82))*.63,localPoint.y*1.45);
    blocks.x+=.5*mod(floor(blocks.y),2.0);
    vec2 edge=abs(fract(blocks)-.5);
    float joints=1.0-smoothstep(.465,.495,max(edge.x,edge.y))*.20*metreDetail;
    float stone=mix(.94,.82+.25*grain(floor(blocks.xyy)),metreDetail)*joints;
    float lit=max(0.0,dot(normalize(worldNormal),sun));
    vec3 shading=underwater?vec3(.25,.47,.52)*(.12+.48*lit):vec3(.20+.80*lit);
    color=vec4(masonry.rgb*(stone*shading+masonry.a),0);
    gl_FragDepth=log2(1.0+length(relativeMetres)*(1e12/149597870700.0))/log2(1.0+20000.0*1e12);
}
)";
}

void Renderer::Render(const glm::mat4& vp,const glm::dmat3& rotation,
                      const glm::dvec3& centreRelativeAu,double radiusAu,
                      const glm::vec3& light,bool underwater) {
    const double metres=Astronomy::metresPerAu;
    const glm::dvec3 centre=centreRelativeAu*metres;
    const double radius=radiusAu*metres;
    const double centreDistance=glm::length(centre);
    const double altitude=centreDistance-radius;
    if(std::abs(altitude)>28000 || centreDistance==0)return;
    const auto cameraDirection=glm::transpose(rotation)*(-centre/centreDistance);
    bool drawing=false,blend=false;
    const auto draw=[&](const Mesh& mesh,const glm::dmat3& frame){
        if(!mesh.vertices)return false;
        if(!program){
            program=compileShaderProgram(vertex,fragment);
            GLint linked=0;glGetProgramiv(program,GL_LINK_STATUS,&linked);
            if(!linked){glDeleteProgram(program);program=0;return false;}
        }
        if(!drawing){
            drawing=true;
            blend=glIsEnabled(GL_BLEND);glDisable(GL_BLEND);
            glUseProgram(program);
            glUniformMatrix4fv(glGetUniformLocation(program,"viewProjection"),1,GL_FALSE,glm::value_ptr(vp));
            glUniform3fv(glGetUniformLocation(program,"sun"),1,glm::value_ptr(light));
            glUniform1i(glGetUniformLocation(program,"underwater"),underwater);
        }
        const auto orientation=glm::mat3(rotation*frame);
        const auto origin=V(centre+rotation*frame[1]*(radius+mesh.elevation));
        glUniformMatrix3fv(glGetUniformLocation(program,"siteFrame"),1,GL_FALSE,glm::value_ptr(orientation));
        glUniform3fv(glGetUniformLocation(program,"originMetres"),1,glm::value_ptr(origin));
        glBindVertexArray(mesh.vao);
#ifdef RENDER_ENGINE_DX12
        NativeDX12::SetRayMaterial({underwater?NativeDX12::RayKind::Submerged:NativeDX12::RayKind::Solid,
            NativeDX12::RaySpace::RelativeMetres,{.59f,.49f,.36f},.04f});
#endif
        glDrawArrays(GL_TRIANGLES,0,mesh.vertices);
#ifdef RENDER_ENGINE_DX12
        NativeDX12::SetRayMaterial({});
#endif
        return true;
    };
    for(unsigned i=0;i<sites.size();++i){
        const auto& site=sites[i];
        if(!models[i] || site.submerged!=underwater)continue;
        const auto frame=site.Frame();
        if(glm::length(centre+rotation*frame[1]*radius)>std::max(2500.0,site.extent*38))continue;
        auto& mesh=meshes[i];
        if(!mesh.vao){
            Builder builder{site};
            models[i](builder);
            Upload(mesh,builder.vertices.data(),static_cast<unsigned>(builder.vertices.size()),builder.elevation);
        }
        if(draw(mesh,frame))visibleSites|=1u<<i;
    }
    if(!underwater){
        static const auto directions=[]{
            std::array<glm::dvec3,std::size(landmarks)> result;
            for(size_t i=0;i<result.size();++i)result[i]=landmarks[i].site.Direction();
            return result;
        }();
        int closest=-1;
        double distance=28000;
        for(unsigned i=0;i<directions.size();++i){
            const auto& site=landmarks[i].site;
            const double maximum=std::min(distance,std::max(1600.0,site.extent*35));
            const double arc=glm::length(cameraDirection-directions[i])*radius;
            if(arc>=maximum)continue;
            const double elevation=landmarkIndex==static_cast<int>(i)?landmarkMesh.elevation:site.Elevation();
            const double separation=std::hypot(altitude-elevation,arc);
            if(separation<maximum){
                closest=static_cast<int>(i);
                distance=separation;
            }
        }
        if(closest>=0){
            const auto& landmark=landmarks[closest];
            if(landmarkIndex!=closest){
                Free(landmarkMesh);
                Builder builder{landmark.site};
                BuildLandmark(builder,landmark.form,landmark.width,landmark.height,landmark.depth);
                Upload(landmarkMesh,builder.vertices.data(),static_cast<unsigned>(builder.vertices.size()),builder.elevation);
                landmarkIndex=closest;
            }
            if(draw(landmarkMesh,landmark.site.Frame()))++visibleLandmarks;
        }

        const double latitude=glm::degrees(std::asin(std::clamp(cameraDirection.y,-1.0,1.0)));
        const double longitude=glm::degrees(std::atan2(-cameraDirection.z,cameraDirection.x));
        const double longitudeScale=std::cos(glm::radians(latitude));
        const double groundRange=std::max(0.0,12000.0-std::abs(altitude)*.55);
        if(latitude>=32.2 && latitude<=50.6 && longitude>=75 && longitude<=131 && groundRange>1000){
            static const WallSurvey survey;
            const double latitudeWindow=glm::degrees(groundRange/radius)+.0125;
            const double longitudeWindow=latitudeWindow/longitudeScale;
            const auto begin=std::lower_bound(survey.tiles.begin(),survey.tiles.end(),latitude-latitudeWindow,
                [](const WallTile& tile,double value){return tile.latitude*microDegree<value;});
            std::vector<std::pair<double,unsigned>> selected;
            for(auto it=begin;it!=survey.tiles.end() && it->latitude*microDegree<=latitude+latitudeWindow;++it){
                const double lon=it->longitude*microDegree+wallCellMidpoint;
                if(std::abs(lon-longitude)>longitudeWindow)continue;
                const double lat=it->latitude*microDegree+wallCellMidpoint;
                const double distance=glm::radians(std::hypot(lat-latitude,(lon-longitude)*longitudeScale))*radius;
                if(distance<groundRange)selected.emplace_back(distance,static_cast<unsigned>(it-survey.tiles.begin()));
            }
            std::sort(selected.begin(),selected.end());
            constexpr size_t maxTiles=35;
            if(selected.size()>maxTiles)selected.resize(maxTiles);
            // Keep the nearby wall meshes resident as the camera crosses tile boundaries.
            std::erase_if(wallMeshes,[&](WallTileMesh& entry){
                if(std::any_of(selected.begin(),selected.end(),[&](const auto& item){return item.second==entry.tile;}))return false;
                const auto& tile=survey.tiles[entry.tile];
                if(std::abs(tile.latitude*microDegree+wallCellMidpoint-latitude)<=latitudeWindow*1.25 &&
                   std::abs(tile.longitude*microDegree+wallCellMidpoint-longitude)<=longitudeWindow*1.25 &&
                   wallMeshes.size()<maxTiles*2)return false;
                Free(entry.mesh);
                return true;
            });
            unsigned uploads=0;
            for(const auto& [distance,id]:selected){
                const auto& tile=survey.tiles[id];
                const double lat=tile.latitude*microDegree+wallCellMidpoint;
                const double lon=tile.longitude*microDegree+wallCellMidpoint;
                const Site site{nullptr,nullptr,nullptr,nullptr,lat,lon,{}, {},0};
                const auto found=std::find_if(wallMeshes.begin(),wallMeshes.end(),[&](const auto& entry){return entry.tile==id;});
                const bool detailed=distance<(found!=wallMeshes.end() && found->detailed?2300:1800);
                if(found!=wallMeshes.end() && found->detailed==detailed){
                    visibleWallTiles+=draw(found->mesh,found->frame);
                    continue;
                }
                if(uploads>=5){
                    pendingWallTiles=true;
                    if(found!=wallMeshes.end())visibleWallTiles+=draw(found->mesh,found->frame);
                    continue;
                }
                Builder builder{site};
                BuildWallTile(builder,survey,tile,detailed);
                if(found!=wallMeshes.end())Free(found->mesh);
                auto& entry=found==wallMeshes.end()?wallMeshes.emplace_back():*found;
                entry={id,builder.frame,{},detailed};
                Upload(entry.mesh,builder.vertices.data(),static_cast<unsigned>(builder.vertices.size()),builder.elevation);
                visibleWallTiles+=draw(entry.mesh,entry.frame);
                ++uploads;
            }
            if(visibleWallTiles && glm::length(cameraDirection-sites[mutianyu].Direction())*radius<2500)
                visibleSites|=1u<<mutianyu;
        }
    }
    if(drawing){
        glBindVertexArray(0);
        if(blend)glEnable(GL_BLEND);
    }
}

void Renderer::Upload(Mesh& mesh,const void* vertices,unsigned count,double elevation) {
    if(!count)return;
    mesh.elevation=elevation;
    mesh.vertices=static_cast<int>(count);
    glGenVertexArrays(1,&mesh.vao);glBindVertexArray(mesh.vao);
    glGenBuffers(1,&mesh.buffer);glBindBuffer(GL_ARRAY_BUFFER,mesh.buffer);
    glBufferData(GL_ARRAY_BUFFER,count*sizeof(Vertex),vertices,GL_STATIC_DRAW);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<void*>(offsetof(Vertex,position)));
    glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<void*>(offsetof(Vertex,normal)));
    glVertexAttribPointer(2,4,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<void*>(offsetof(Vertex,paint)));
    for(unsigned attribute=0;attribute<3;++attribute)glEnableVertexAttribArray(attribute);
}

void Renderer::Free(Mesh& mesh) {
    if(mesh.buffer)glDeleteBuffers(1,&mesh.buffer);
    if(mesh.vao)glDeleteVertexArrays(1,&mesh.vao);
    mesh={};
}

void Renderer::Release() {
    for(auto& mesh:meshes)Free(mesh);
    for(auto& entry:wallMeshes)Free(entry.mesh);
    wallMeshes.clear();
    Free(landmarkMesh);
    landmarkIndex=-1;
    if(program)glDeleteProgram(program);
    program=0;
    ResetVisibility();
}

}

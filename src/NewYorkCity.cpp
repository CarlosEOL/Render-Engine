#include <glad/glad.h>
#include "NewYorkCity.h"
#include "Astronomy.h"
#include "PlanetTerrain.h"
#include "stb_image.h"
#ifdef RENDER_ENGINE_DX12
#include "dx12/Dx12SceneBridge.h"
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstddef>
#include <memory>
#include <numeric>
#include <string_view>
#include <glm/gtc/type_ptr.hpp>

unsigned int compileShaderProgram(const char*,const char*);

namespace NewYorkCity {
namespace {

constexpr float tileMetres=256.f;
constexpr double earthMetres=6371000.0;

std::uint16_t Read16(const unsigned char* bytes) {
    return std::uint16_t(bytes[0]|(unsigned(bytes[1])<<8));
}

std::uint32_t Read32(const unsigned char* bytes) {
    return std::uint32_t(Read16(bytes))|(std::uint32_t(Read16(bytes+2))<<16);
}

float Signed16(const unsigned char* bytes) {
    const auto value=Read16(bytes);
    return static_cast<float>(value<32768?int(value):int(value)-65536);
}

using Point=glm::vec2;
struct Vertex {
    glm::vec3 position,normal;
    glm::vec4 building; // East and south centre, ground and roof level.
    glm::vec2 material; // Face type and stable building tint.
};

float Cross(Point a,Point b) { return a.x*b.y-a.y*b.x; }

std::vector<unsigned> Roof(const std::vector<Point>& outline) {
    std::vector<unsigned> ring(outline.size()),roof;
    std::iota(ring.begin(),ring.end(),0u);
    while(ring.size()>3){
        bool found=false;
        for(std::size_t i=0;i<ring.size();++i){
            const unsigned a=ring[(i+ring.size()-1)%ring.size()],b=ring[i],c=ring[(i+1)%ring.size()];
            const auto p=outline[a],q=outline[b],r=outline[c];
            if(Cross(q-p,r-p)<=.0001f)continue;
            bool occupied=false;
            for(const auto index:ring){
                if(index==a || index==b || index==c)continue;
                const auto point=outline[index];
                if(Cross(q-p,point-p)>.0001f && Cross(r-q,point-q)>.0001f && Cross(p-r,point-r)>.0001f){
                    occupied=true;break;
                }
            }
            if(occupied)continue;
            roof.insert(roof.end(),{a,c,b});ring.erase(ring.begin()+i);found=true;break;
        }
        if(!found)break;
    }
    if(ring.size()==3)roof.insert(roof.end(),{ring[0],ring[2],ring[1]});
    return roof;
}

struct MeshBuilder {
    Point origin;
    std::vector<Vertex> vertices;

    glm::vec3 Position(Point p,float elevation) const {
        const glm::dvec2 offset=glm::dvec2(origin)+glm::dvec2(p);
        const double squared=glm::dot(offset,offset);
        const double sag=-squared/(earthMetres+std::sqrt(earthMetres*earthMetres-squared));
        return {p.x,static_cast<float>(elevation+sag),p.y};
    }

    void Triangle(glm::vec3 a,glm::vec3 b,glm::vec3 c,glm::vec3 normal,glm::vec4 building,glm::vec2 material) {
        vertices.insert(vertices.end(),{{a,normal,building,material},{b,normal,building,material},{c,normal,building,material}});
    }

    void Extrude(const std::vector<Point>& outline,float ground,float height,glm::vec4 building,float shade,bool detailed) {
        const float roof=ground+height;
        for(std::size_t i=0;i<outline.size();++i){
            const auto a=outline[i],b=outline[(i+1)%outline.size()];
            const float span=glm::length(b-a);
            if(span<.05f)continue;
            const glm::vec3 normal((b.y-a.y)/span,0,(a.x-b.x)/span);
            const auto bottomA=Position(a,ground-1.4f),bottomB=Position(b,ground-1.4f);
            const auto topA=Position(a,roof),topB=Position(b,roof);
            Triangle(bottomA,topA,topB,normal,building,{0,shade});
            Triangle(bottomA,topB,bottomB,normal,building,{0,shade});
        }
        const auto triangles=detailed?Roof(outline):std::vector<unsigned>{0,2,1,0,3,2};
        for(std::size_t i=0;i+2<triangles.size();i+=3)
            Triangle(Position(outline[triangles[i]],roof),Position(outline[triangles[i+1]],roof),
                Position(outline[triangles[i+2]],roof),{0,1,0},building,{1,shade});
    }

    void Street(Point a,Point b,float start,float end) {
        const auto edge=b-a;
        if(glm::dot(edge,edge)<.01f)return;
        const auto side=glm::normalize(Point(-edge.y,edge.x))*4.0f;
        const auto first=Position(a+side,start),second=Position(b+side,end);
        const auto third=Position(b-side,end),fourth=Position(a-side,start);
        const glm::vec3 normal(0,1,0);
        Triangle(first,second,third,normal,{},{2,.40f});
        Triangle(first,third,fourth,normal,{},{2,.40f});
    }
};

std::vector<Point> Envelope(const std::vector<Point>& outline){
    Point tangent(1,0);
    float longest=0;
    for(std::size_t i=0;i<outline.size();++i){
        const auto side=outline[(i+1)%outline.size()]-outline[i];
        const float length=glm::dot(side,side);
        if(length>longest){longest=length;tangent=glm::normalize(side);}
    }
    const Point perpendicular(-tangent.y,tangent.x);
    Point low(1e5f),high(-1e5f);
    for(const auto p:outline){
        const Point coordinates(glm::dot(p,tangent),glm::dot(p,perpendicular));
        low=glm::min(low,coordinates);high=glm::max(high,coordinates);
    }
    return {tangent*low.x+perpendicular*low.y,tangent*high.x+perpendicular*low.y,
        tangent*high.x+perpendicular*high.y,tangent*low.x+perpendicular*high.y};
}

const char* vertex=R"(#version 330 core
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec4 building;
layout(location=3) in vec2 material;
uniform mat4 viewProjection;
uniform mat3 cityFrame;
uniform vec3 originMetres,cameraMetres;
uniform vec2 tileOffset;
uniform int lod;
uniform float lodDistanceSquared;
out vec3 relativeMetres,worldNormal,facadePoint;
out vec2 paint;
void main(){
    vec2 horizontal=cameraMetres.xz-(tileOffset+building.xy);
    float vertical=cameraMetres.y-clamp(cameraMetres.y,building.z,building.w);
    bool close=dot(horizontal,horizontal)+vertical*vertical<lodDistanceSquared;
    bool visible=lod>=2 || close==(lod==1);
    paint=material;
    facadePoint=position+vec3(tileOffset.x,0,tileOffset.y);
    worldNormal=cityFrame*normal;
    relativeMetres=visible?originMetres+cityFrame*position:vec3(0);
    gl_Position=visible?viewProjection*vec4(relativeMetres/149597870700.0,1):vec4(3,3,3,1);
}
)";

const char* fragment=R"(#version 330 core
in vec3 relativeMetres,worldNormal,facadePoint;
in vec2 paint;
uniform vec3 sun,up;
uniform int lod;
out vec4 color;
void main(){
    vec3 n=normalize(worldNormal);
    float daylight=smoothstep(-.13,.10,dot(up,sun));
    vec3 stone=mix(vec3(.27,.22,.17),vec3(.64,.64,.60),paint.y);
    stone=mix(stone,vec3(.18,.25,.30),smoothstep(.72,1.0,paint.y)*.56);
    vec3 material=paint.x>.5?stone*.77:stone;
    if(paint.x>1.5)material=vec3(.094,.088,.082);
    float emission=0.0;
    if(lod==1 && paint.x<.5){
        vec2 grid=vec2((facadePoint.x+facadePoint.z)*.48,facadePoint.y*.31);
        vec2 panel=abs(fract(grid)-.5);
        float windows=(1.0-smoothstep(.34,.41,panel.x))*(1.0-smoothstep(.22,.32,panel.y));
        windows*=1.0-smoothstep(.25,.95,length(fwidth(grid)));
        material=mix(material,vec3(.068,.102,.133),windows*.77);
        float occupied=step(.57,fract(sin(dot(floor(grid),vec2(27.2,21.4))+paint.y*87.0)*14752.13));
        emission=windows*occupied*(1.0-daylight)*.45;
    }
    float direct=max(0.0,dot(n,sun));
    color=vec4(material*(.014+daylight*(.29+.71*direct))+vec3(1.0,.65,.31)*emission,0);
    gl_FragDepth=log2(1.0+length(relativeMetres)*(1e12/149597870700.0))/log2(1.0+20000.0*1e12);
}
)";
}

glm::dmat3 Frame() {
    const auto lat=glm::radians(latitude),lon=glm::radians(longitude);
    const glm::dvec3 up(std::cos(lat)*std::cos(lon),std::sin(lat),-std::cos(lat)*std::sin(lon));
    const glm::dvec3 east(-std::sin(lon),0,-std::cos(lon));
    return {east,up,glm::cross(east,up)};
}

std::filesystem::path AssetPath(const char* name) {
    return PlanetTerrain::EarthAssetPath((std::string("../nyc/")+name).c_str());
}

bool HasCity() {
    static const bool present=std::filesystem::is_regular_file(AssetPath("city.bin"));
    return present;
}

bool Renderer::Open() {
    attempted=true;
    archive.open(AssetPath("city.bin"),std::ios::binary|std::ios::ate);
    if(!archive)return false;
    const auto fileSize=static_cast<std::uint64_t>(archive.tellg());
    archive.seekg(0);
    std::array<unsigned char,12> header{};
    if(!archive.read(reinterpret_cast<char*>(header.data()),header.size()) ||
        std::string_view(reinterpret_cast<const char*>(header.data()),4)!="NYC1" || Read32(header.data()+8)!=tileMetres)return false;
    const unsigned count=Read32(header.data()+4);
    if(!count || count>30000 || fileSize<12u+20u*count)return false;
    std::vector<Tile> entries;
    entries.reserve(count);
    for(unsigned i=0;i<count;++i){
        std::array<unsigned char,20> record{};
        if(!archive.read(reinterpret_cast<char*>(record.data()),record.size()))return false;
        const auto* p=record.data();
        const Tile tile{int(Signed16(p)),int(Signed16(p+2)),Read32(p+4),Read32(p+8),Read32(p+12),Read32(p+16)};
        if(!tile.bytes || tile.bytes>2000000 || tile.offset<12u+20u*count || std::uint64_t(tile.offset)+tile.bytes>fileSize ||
            tile.buildings>6000 || tile.roads>5000)return false;
        footprintCount+=tile.buildings;
        entries.push_back(tile);
    }
    program=compileShaderProgram(vertex,fragment);
    GLint linked=0;
    glGetProgramiv(program,GL_LINK_STATUS,&linked);
    if(!linked){if(program)glDeleteProgram(program);program=0;return false;}
    tiles=std::move(entries);
    return true;
}

void Renderer::Build(const Tile& tile,TileMeshes& meshes,bool detail) {
    const bool onlyDetail=meshes.ready;
    meshes.ready=true;
    meshes.nearReady=detail;
    archive.clear();
    archive.seekg(tile.offset);
    std::vector<char> compressed(tile.bytes);
    if(!archive.read(compressed.data(),compressed.size()))return;
    std::vector<char> bytes(1024*1024);
    const int length=stbi_zlib_decode_buffer(bytes.data(),static_cast<int>(bytes.size()),compressed.data(),static_cast<int>(compressed.size()));
    if(length<0)return;
    const auto* cursor=reinterpret_cast<const unsigned char*>(bytes.data());
    const auto* finish=cursor+length;
    const Point origin(tile.column*tileMetres,tile.row*tileMetres);
    MeshBuilder block{origin},fine{origin},roads{origin};
    for(unsigned i=0;i<tile.buildings;++i){
        if(finish-cursor<10)return;
        const auto key=Read32(cursor);
        const float ground=Signed16(cursor+4)*.1f,height=Read16(cursor+6)*.01f;
        const unsigned count=Read16(cursor+8);
        cursor+=10;
        if(count<3 || count>2048 || finish-cursor<4*static_cast<ptrdiff_t>(count))return;
        std::vector<Point> polygon;
        polygon.reserve(count);
        Point center(0);
        for(unsigned j=0;j<count;++j){
            const Point point(Signed16(cursor)*.125f,Signed16(cursor+2)*.125f);
            polygon.push_back(point);center+=point;cursor+=4;
        }
        center/=float(count);
        float area=0;
        for(unsigned j=0;j<count;++j)area+=Cross(polygon[j]-center,polygon[(j+1)%count]-center);
        if(std::abs(area)<.04f)continue;
        if(area<0)std::reverse(polygon.begin(),polygon.end());
        const glm::vec4 building(center,ground,ground+height);
        meshes.highest=std::max(meshes.highest,ground+height);
        const float shade=float(((key+tile.row*733u)*1664525u+1013904223u)>>24)/255.f;
        if(!onlyDetail)block.Extrude(Envelope(polygon),ground,height,building,shade,false);
        if(detail)fine.Extrude(polygon,ground,height,building,shade,true);
    }
    if(!onlyDetail)for(unsigned i=0;i<tile.roads;++i){
        if(finish-cursor<12)return;
        roads.Street({Signed16(cursor)*.125f,Signed16(cursor+2)*.125f},{Signed16(cursor+4)*.125f,Signed16(cursor+6)*.125f},
            Signed16(cursor+8)*.25f+.48f,Signed16(cursor+10)*.25f+.48f);
        cursor+=12;
    }
    const auto upload=[](Mesh& mesh,const std::vector<Vertex>& data){
        if(data.empty())return;
        glGenVertexArrays(1,&mesh.vao);glBindVertexArray(mesh.vao);
        glGenBuffers(1,&mesh.buffer);glBindBuffer(GL_ARRAY_BUFFER,mesh.buffer);
        glBufferData(GL_ARRAY_BUFFER,data.size()*sizeof(Vertex),data.data(),GL_STATIC_DRAW);
        glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<const void*>(offsetof(Vertex,position)));
        glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<const void*>(offsetof(Vertex,normal)));
        glVertexAttribPointer(2,4,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<const void*>(offsetof(Vertex,building)));
        glVertexAttribPointer(3,2,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<const void*>(offsetof(Vertex,material)));
        for(unsigned attribute=0;attribute<4;++attribute)glEnableVertexAttribArray(attribute);
        mesh.vertices=static_cast<int>(data.size());
    };
    if(!onlyDetail){upload(meshes.block,block.vertices);upload(meshes.roads,roads.vertices);}
    if(detail)upload(meshes.detail,fine.vertices);
}

void Renderer::Render(const glm::mat4& viewProjection,const glm::dmat3& rotation,
    const glm::dvec3& centreRelativeAu,double radiusAu,const glm::vec3& light) {
    const auto basis=rotation*Frame();
    const auto centre=centreRelativeAu*Astronomy::metresPerAu;
    const auto origin=centre+basis[1]*(radiusAu*Astronomy::metresPerAu);
    if(glm::length(origin)>48000)return;
    const auto camera=glm::vec3(glm::transpose(basis)*-origin);
    if(camera.y>19500)return;
    if(!attempted && !Open())return;
    if(!program)return;
    ++frame;
    struct Candidate { std::size_t index;float distance; };
    std::vector<Candidate> candidates;
    const float range=2500.f+std::clamp(camera.y,0.f,3000.f)*1.3f;
    const auto frameMatrix=glm::mat3(basis);
    const auto worldOrigin=glm::vec3(origin);
    const auto clipScale=glm::length(glm::vec3(viewProjection[0][1],viewProjection[1][1],viewProjection[2][1]));
    for(std::size_t i=0;i<tiles.size();++i){
        const auto& tile=tiles[i];
        const auto offset=Point(tile.column+.5f,tile.row+.5f)*tileMetres;
        const float distance=glm::length(offset-Point(camera.x,camera.z));
        if(distance>range)continue;
        const auto position=worldOrigin+frameMatrix*glm::vec3(offset.x,60,offset.y);
        const auto projected=viewProjection*glm::vec4(position/float(Astronomy::metresPerAu),1);
        const float margin=clipScale*(490.f/float(Astronomy::metresPerAu));
        if(projected.w< -margin || std::abs(projected.x)>projected.w+margin || std::abs(projected.y)>projected.w+margin)continue;
        candidates.push_back({i,distance});
    }
    std::sort(candidates.begin(),candidates.end(),[](const auto& a,const auto& b){return a.distance<b.distance;});
    if(candidates.size()>220)candidates.resize(220);
    glUseProgram(program);
    glUniformMatrix4fv(glGetUniformLocation(program,"viewProjection"),1,GL_FALSE,glm::value_ptr(viewProjection));
    glUniformMatrix3fv(glGetUniformLocation(program,"cityFrame"),1,GL_FALSE,glm::value_ptr(frameMatrix));
    glUniform3fv(glGetUniformLocation(program,"cameraMetres"),1,glm::value_ptr(camera));
    glUniform3fv(glGetUniformLocation(program,"sun"),1,glm::value_ptr(light));
    glUniform3fv(glGetUniformLocation(program,"up"),1,glm::value_ptr(frameMatrix[1]));
    glUniform1f(glGetUniformLocation(program,"lodDistanceSquared"),detailMetres*detailMetres);
    const int lod=glGetUniformLocation(program,"lod");
    const int position=glGetUniformLocation(program,"originMetres");
    const int tileOffset=glGetUniformLocation(program,"tileOffset");
    int loads=0;
    for(const auto& entry:candidates){
        const auto& tile=tiles[entry.index];
        auto& meshes=resident[entry.index];
        meshes.seen=frame;
        const Point offset(tile.column*tileMetres,tile.row*tileMetres);
        const Point eye(camera.x,camera.z);
        const float distance=glm::length(eye-glm::clamp(eye,offset,offset+Point(tileMetres)));
        const bool wantDetail=distance<detailMetres+45.f && camera.y<(meshes.ready?meshes.highest:640.f)+detailMetres;
        if(!meshes.ready || (wantDetail && !meshes.nearReady)){
            if(loads<8){Build(tile,meshes,wantDetail);++loads;}
            else streaming=true;
        }
        if(!meshes.ready)continue;
        const auto world=worldOrigin+frameMatrix*glm::vec3(offset.x,0,offset.y);
        glUniform2f(tileOffset,offset.x,offset.y);
        glUniform3fv(position,1,glm::value_ptr(world));
        const auto draw=[&](const Mesh& mesh,int level){
            if(!mesh.vertices)return;
            glUniform1i(lod,level);glBindVertexArray(mesh.vao);glDrawArrays(GL_TRIANGLES,0,mesh.vertices);
        };
#ifdef RENDER_ENGINE_DX12
        if(distance<220 && NativeDX12::HardwareRaysEnabled())
            NativeDX12::SetRayMaterial({NativeDX12::RayKind::Solid,NativeDX12::RaySpace::RelativeMetres,{.31f,.30f,.28f},.025f});
#endif
        if(distance<1800)draw(meshes.roads,2);
        const bool drawDetail=wantDetail && meshes.nearReady && meshes.detail.vertices;
        draw(meshes.block,drawDetail?0:3);
        if(drawDetail){draw(meshes.detail,1);++detailedTiles;}
#ifdef RENDER_ENGINE_DX12
        if(distance<220 && NativeDX12::HardwareRaysEnabled())NativeDX12::SetRayMaterial({});
#endif
        ++drawnTiles;
    }
    glBindVertexArray(0);
    for(auto it=resident.begin();it!=resident.end();){
        if(frame-it->second.seen>72){
            ReleaseMesh(it->second.block);ReleaseMesh(it->second.detail);ReleaseMesh(it->second.roads);
            it=resident.erase(it);
        }else ++it;
    }
}

void Renderer::ReleaseMesh(Mesh& mesh) {
    if(mesh.vao)glDeleteVertexArrays(1,&mesh.vao);
    if(mesh.buffer)glDeleteBuffers(1,&mesh.buffer);
    mesh={};
}

void Renderer::Release() {
    for(auto& [key,meshes]:resident){
        ReleaseMesh(meshes.block);ReleaseMesh(meshes.detail);ReleaseMesh(meshes.roads);
    }
    resident.clear();tiles.clear();archive.close();attempted=false;
    if(program)glDeleteProgram(program);
    program=frame=drawnTiles=detailedTiles=0;footprintCount=0;streaming=false;
}

}

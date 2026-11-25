#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace MarineAnimalGeometry {

struct Vertex { glm::vec3 position, normal, color; };

namespace Detail {

// Sections run from the caudal peduncle to the tip of the snout.
struct Section {
    float x;
    glm::vec3 shape; // vertical centre, vertical radius, lateral radius
};

inline constexpr std::array dolphinBody{
    Section{-.480f,{-.011f,.003f,.004f}}, Section{-.433f,{-.005f,.014f,.015f}},
    Section{-.360f,{.005f,.024f,.022f}}, Section{-.262f,{.007f,.047f,.036f}},
    Section{-.135f,{.004f,.072f,.054f}}, Section{.000f,{.000f,.091f,.078f}},
    Section{.134f,{.003f,.110f,.086f}}, Section{.241f,{.008f,.103f,.080f}},
    Section{.319f,{.014f,.092f,.067f}}, Section{.359f,{.016f,.070f,.058f}},
    Section{.388f,{-.011f,.038f,.043f}}, Section{.439f,{-.018f,.022f,.034f}},
    Section{.495f,{-.017f,.011f,.022f}}, Section{.515f,{-.016f,0,0}}
};
inline constexpr std::array sharkBody{
    Section{-.478f,{.002f,.003f,.004f}}, Section{-.432f,{.002f,.013f,.017f}},
    Section{-.355f,{.003f,.024f,.025f}}, Section{-.251f,{.003f,.044f,.038f}},
    Section{-.118f,{-.004f,.080f,.063f}}, Section{.014f,{-.008f,.113f,.081f}},
    Section{.163f,{-.008f,.118f,.085f}}, Section{.274f,{-.002f,.096f,.078f}},
    Section{.353f,{.005f,.067f,.065f}}, Section{.432f,{.009f,.042f,.040f}},
    Section{.494f,{.012f,.009f,.012f}}, Section{.512f,{.012f,0,0}}
};

inline glm::vec3 Slope(const Section& left, const Section& right) {
    return (right.shape - left.shape) / (right.x-left.x);
}

// Limiting each tangent to adjacent slopes keeps the narrow tips from bulging.
inline glm::vec3 Tangent(const Section& left, const Section& middle, const Section& right) {
    const auto before = Slope(left,middle), after = Slope(middle,right);
    glm::vec3 tangent(0);
    for (int axis=0; axis<3; ++axis)
        if (before[axis]*after[axis]>0)
            tangent[axis]=2*before[axis]*after[axis]/(before[axis]+after[axis]);
    return tangent;
}

template<std::size_t Count>
glm::vec3 CrossSection(const std::array<Section,Count>& sections, float x) {
    const auto last = sections.size()-1;
    const auto high=std::upper_bound(sections.begin(),sections.end(),x,
        [](float coordinate,const Section& section){ return coordinate<section.x; });
    const auto after=std::clamp<std::size_t>(high-sections.begin(),1,last);
    const auto before=after-1;
    const auto& first=sections[before];
    const auto& second=sections[after];
    const float length=second.x-first.x;
    const float s=glm::clamp((x-first.x)/length,0.f,1.f);
    const float s2=s*s, s3=s2*s;
    const auto firstSlope=before?Tangent(sections[before-1],first,second):Slope(first,second);
    const auto secondSlope=after<last?Tangent(first,second,sections[after+1]):Slope(first,second);
    return first.shape*(2*s3-3*s2+1) + firstSlope*((s3-2*s2+s)*length)
        + second.shape*(-2*s3+3*s2) + secondSlope*((s3-s2)*length);
}

inline glm::vec3 Shape(bool dolphin, float x) {
    return dolphin?CrossSection(dolphinBody,x):CrossSection(sharkBody,x);
}

inline glm::vec3 Coating(bool dolphin, float x, float dorsal) {
    const glm::vec3 dark=dolphin?glm::vec3(.055f,.103f,.146f):glm::vec3(.103f,.138f,.154f);
    const glm::vec3 flank=dolphin?glm::vec3(.260f,.354f,.411f):glm::vec3(.207f,.280f,.302f);
    const glm::vec3 belly=dolphin?glm::vec3(.628f,.723f,.750f):glm::vec3(.676f,.719f,.655f);
    const float line=dolphin?-.36f-.22f*std::exp(-30.f*(x-.12f)*(x-.12f)):-.18f;
    const float countershade=glm::smoothstep(line-.18f,line+.12f,dorsal);
    const float back=glm::smoothstep(-.08f,.88f,dorsal);
    return glm::mix(belly,glm::mix(flank,dark,back),countershade);
}

inline void Face(std::vector<Vertex>& mesh, const Vertex& a,const Vertex& b,const Vertex& c) {
    const auto cross=glm::cross(b.position-a.position,c.position-a.position);
    if (glm::dot(cross,cross)<1.e-20f) return;
    if (glm::dot(cross,a.normal+b.normal+c.normal)>0)
        mesh.insert(mesh.end(),{a,b,c});
    else
        mesh.insert(mesh.end(),{a,c,b});
}

inline void Body(std::vector<Vertex>& mesh, bool dolphin) {
    constexpr int steps=64, sides=24;
    const float minimum=dolphin?dolphinBody.front().x:sharkBody.front().x;
    const float maximum=dolphin?dolphinBody.back().x:sharkBody.back().x;
    const auto vertex=[&](int station,int side){
        const float x=glm::mix(minimum,maximum,static_cast<float>(station)/steps);
        const auto section=Shape(dolphin,x);
        const float axis=glm::two_pi<float>()*static_cast<float>(side%sides)/sides;
        const float c=std::cos(axis),s=std::sin(axis);
        const auto slope=(Shape(dolphin,x+.001f)-Shape(dolphin,x-.001f))
            / (station==0||station==steps?.001f:.002f);
        const glm::vec3 along(1,slope.x+slope.y*c,slope.z*s);
        const glm::vec3 around(0,-std::max(section.y,.0003f)*s,std::max(section.z,.0003f)*c);
        const auto n=glm::normalize(glm::cross(around,along));
        return Vertex{{x,section.x+section.y*c,section.z*s},n,Coating(dolphin,x,c)};
    };
    for (int x=0;x<steps;++x) for (int i=0;i<sides;++i) {
        const auto a=vertex(x,i), b=vertex(x+1,i), c=vertex(x+1,i+1),d=vertex(x,i+1);
        Face(mesh,a,d,b);
        Face(mesh,d,c,b);
    }
}

// A foil section tapers towards both edges, keeping the fins readable at grazing angles.
struct FinSection { float y,z, leading,trailing, halfThickness; };

template<std::size_t Count>
void Fin(std::vector<Vertex>& mesh, const std::array<FinSection,Count>& sections,
         const glm::vec3& thicknessAxis, const glm::vec3& upper, const glm::vec3& lower) {
    constexpr int sides=12;
    const auto point=[&](std::size_t index,float angle){
        const auto& foil=sections[index];
        const float along=std::cos(angle);
        const float thickness=std::sin(angle)*(1+.32f*along)*foil.halfThickness;
        return glm::vec3(glm::mix(foil.trailing,foil.leading,.5f+.5f*along),foil.y,foil.z)+thicknessAxis*thickness;
    };
    const auto vertex=[&](std::size_t row,int side){
        const float angle=glm::two_pi<float>()*static_cast<float>(side%sides)/sides;
        const auto origin=point(row,angle);
        const auto adjacent=point(std::min(row+1,Count-1),angle)-point(row>0?row-1:0,angle);
        auto around=point(row,angle+.03f)-point(row,angle-.03f);
        if (glm::dot(around,around)<1.e-12f)
            around=point(row-1,angle+.03f)-point(row-1,angle-.03f);
        auto n=glm::normalize(glm::cross(around,adjacent));
        const auto expected=glm::vec3(std::cos(angle),0,0)+thicknessAxis*std::sin(angle);
        if (glm::dot(n,expected)<0) n=-n;
        return Vertex{origin,n,glm::mix(lower,upper,glm::smoothstep(-.48f,.52f,std::sin(angle)))};
    };
    for (std::size_t row=0;row+1<Count;++row) for(int i=0;i<sides;++i) {
        const auto a=vertex(row,i),b=vertex(row+1,i),c=vertex(row+1,i+1),d=vertex(row,i+1);
        Face(mesh,a,b,d);
        Face(mesh,d,b,c);
    }
}

inline void Fins(std::vector<Vertex>& mesh, bool dolphin) {
    const glm::vec3 upper=dolphin?glm::vec3(.082f,.147f,.184f):glm::vec3(.106f,.150f,.170f);
    const glm::vec3 lower=dolphin?glm::vec3(.233f,.344f,.394f):glm::vec3(.368f,.459f,.463f);
    const auto dorsal=dolphin?std::array{
        FinSection{.078f,0,.067f,-.182f,.019f},FinSection{.107f,0,.031f,-.135f,.014f},
        FinSection{.159f,0,.008f,-.107f,.008f},FinSection{.194f,0,-.047f,-.085f,.003f},
        FinSection{.202f,0,-.091f,-.091f,0}}:std::array{
        FinSection{.084f,0,.066f,-.173f,.020f},FinSection{.129f,0,.021f,-.145f,.013f},
        FinSection{.188f,0,-.016f,-.094f,.008f},FinSection{.227f,0,-.057f,-.082f,.003f},
        FinSection{.236f,0,-.080f,-.080f,0}};
    Fin(mesh,dorsal,{0,0,1},upper,upper);
    for(float side:{-1.f,1.f}){
        const auto pectoral=dolphin?std::array{
            FinSection{-.034f,side*.051f,.213f,.087f,.016f},FinSection{-.066f,side*.095f,.154f,.045f,.012f},
            FinSection{-.103f,side*.160f,.097f,.013f,.007f},FinSection{-.119f,side*.188f,.047f,.008f,.004f},
            FinSection{-.126f,side*.201f,.006f,.006f,0}}:std::array{
            FinSection{-.031f,side*.063f,.166f,.013f,.017f},FinSection{-.056f,side*.130f,.123f,-.032f,.012f},
            FinSection{-.083f,side*.212f,.064f,-.069f,.007f},FinSection{-.098f,side*.257f,.005f,-.079f,.003f},
            FinSection{-.105f,side*.279f,-.059f,-.059f,0}};
        Fin(mesh,pectoral,{0,1,0},upper,lower);
        if(dolphin){
            const std::array fluke{
                FinSection{-.008f,side*.001f,-.409f,-.493f,.015f},FinSection{-.009f,side*.044f,-.431f,-.515f,.012f},
                FinSection{-.008f,side*.101f,-.461f,-.526f,.008f},FinSection{-.003f,side*.155f,-.505f,-.542f,.004f},
                FinSection{.006f,side*.196f,-.552f,-.552f,0}};
            Fin(mesh,fluke,{0,1,0},upper,lower);
        }else{
            const std::array pelvic{
                FinSection{-.026f,side*.022f,-.228f,-.314f,.012f},FinSection{-.054f,side*.061f,-.260f,-.331f,.007f},
                FinSection{-.070f,side*.093f,-.325f,-.325f,0}};
            Fin(mesh,pelvic,{0,1,0},upper,lower);
        }
    }
    if(!dolphin){
        const std::array upperTail{
            FinSection{-.001f,0,-.414f,-.483f,.018f},FinSection{.039f,0,-.426f,-.505f,.013f},
            FinSection{.094f,0,-.446f,-.528f,.008f},FinSection{.145f,0,-.481f,-.550f,.004f},
            FinSection{.182f,0,-.559f,-.559f,0}};
        const std::array lowerTail{
            FinSection{.001f,0,-.417f,-.482f,.018f},FinSection{-.037f,0,-.437f,-.510f,.012f},
            FinSection{-.085f,0,-.472f,-.540f,.007f},FinSection{-.117f,0,-.533f,-.567f,.003f},
            FinSection{-.135f,0,-.574f,-.574f,0}};
        Fin(mesh,upperTail,{0,0,1},upper,upper);
        Fin(mesh,lowerTail,{0,0,1},upper,upper);
        const std::array dorsalRear{
            FinSection{.026f,0,-.259f,-.334f,.009f},FinSection{.063f,0,-.306f,-.340f,.005f},
            FinSection{.078f,0,-.341f,-.341f,0}};
        Fin(mesh,dorsalRear,{0,0,1},upper,upper);
    }
}

inline void Oval(std::vector<Vertex>& mesh, glm::vec3 centre,glm::vec3 size,glm::vec3 color) {
    constexpr int rings=6,sides=10;
    const auto vertex=[&](int row,int side){
        const float theta=glm::pi<float>()*row/rings, phi=glm::two_pi<float>()*(side%sides)/sides;
        const glm::vec3 v(std::cos(theta),std::sin(theta)*std::cos(phi),std::sin(theta)*std::sin(phi));
        return Vertex{centre+v*size,glm::normalize(v/size),color};
    };
    for (int row=0;row<rings;++row) for (int i=0;i<sides;++i) {
        const auto a=vertex(row,i),b=vertex(row+1,i),c=vertex(row+1,i+1),d=vertex(row,i+1);
        Face(mesh,a,b,d);
        Face(mesh,d,b,c);
    }
}

inline glm::vec3 Cheek(bool dolphin,float x,float y,float side) {
    const auto section=Shape(dolphin,x);
    const float h=(y-section.x)/std::max(section.y,.001f);
    const float lateral=section.z*std::sqrt(std::max(0.f,1-h*h));
    return {x,y,side*(lateral+.0011f)};
}

template<class Line>
void Mark(std::vector<Vertex>& mesh,bool dolphin,float side,glm::vec3 color,const Line& line) {
    const auto vertex=[&](float t,float spread){
        const auto point=line(t,spread*std::sin(t*glm::pi<float>()));
        return Vertex{Cheek(dolphin,point.x,point.y,side),{0,0,side},color};
    };
    constexpr int steps=12;
    for (int i=0;i<steps;++i) {
        const float begin=static_cast<float>(i)/steps,end=static_cast<float>(i+1)/steps;
        const auto a=vertex(begin,-1),b=vertex(end,-1),c=vertex(end,1),d=vertex(begin,1);
        Face(mesh,a,b,d);
        Face(mesh,d,b,c);
    }
}

inline void Features(std::vector<Vertex>& mesh,bool dolphin) {
    for (float side:{-1.f,1.f}) {
        const auto eye=Cheek(dolphin,dolphin?.334f:.365f,dolphin?.018f:.032f,side);
        Oval(mesh,eye,{.013f,.009f,.005f},dolphin?glm::vec3(.091f,.128f,.143f):glm::vec3(.120f,.150f,.157f));
        Oval(mesh,eye+glm::vec3(.002f,0,side*.0032f),{.0079f,.0065f,.004f},{.008f,.013f,.016f});
        Oval(mesh,eye+glm::vec3(.0042f,.0034f,side*.006f),{.0026f,.0019f,.0014f},{.44f,.57f,.62f});
        if(dolphin){
            Mark(mesh,true,side,{.089f,.146f,.178f},[](float t,float spread){
                return glm::vec2(glm::mix(.313f,.500f,t),-.036f+.011f*t-.006f*std::sin(t*glm::pi<float>())+spread*.0023f);
            });
        }else{
            Mark(mesh,false,side,{.101f,.148f,.152f},[](float t,float spread){
                return glm::vec2(glm::mix(.270f,.449f,t),glm::mix(-.048f,-.017f,t)-.015f*std::sin(t*glm::pi<float>())+spread*.0026f);
            });
            for(int gill=0;gill<5;++gill){
                Mark(mesh,false,side,{.075f,.110f,.120f},[gill](float t,float spread){
                    return glm::vec2(.192f-.017f*gill-.024f*t+.008f*std::sin(t*glm::pi<float>())+spread*.0026f,
                        glm::mix(.043f,-.063f,t));
                });
            }
        }
    }
    if(dolphin) {
        const auto section=Shape(true,.258f);
        Oval(mesh,{.258f,section.x+section.y*.993f,0},{.016f,.0031f,.012f},{.061f,.117f,.144f});
    }
}

}

inline std::vector<Vertex> Build(bool dolphin) {
    std::vector<Vertex> mesh;
    mesh.reserve(12500);
    Detail::Body(mesh,dolphin);
    Detail::Fins(mesh,dolphin);
    Detail::Features(mesh,dolphin);
    return mesh;
}

}

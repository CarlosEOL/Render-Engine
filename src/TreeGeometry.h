#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace TreeGeometry {
struct Vertex {
    glm::vec3 p,color;
    glm::vec2 uv;
    float card;
    glm::vec3 normal{};
};

inline float Random(std::uint32_t value) {
    value=(value^(value>>16))*0x7feb352du;
    value=(value^(value>>15))*0x846ca68bu;
    return ((value^(value>>16))&0xffffffu)/16777215.f;
}

inline glm::vec3 Bezier(glm::vec3 start,glm::vec3 bend,glm::vec3 end,float t) {
    return glm::mix(glm::mix(start,bend,t),glm::mix(bend,end,t),t);
}

class Builder {
public:
    std::vector<Vertex> vertices;

    void Branch(glm::vec3 start,glm::vec3 bend,glm::vec3 end,float base,float tip,
        int sections,int sides,glm::vec3 bark) {
        Tube([&](float t){return Bezier(start,bend,end,t);},base,tip,sections,sides,bark);
    }

    template<class Curve>
    void Tube(const Curve& curve,float base,float tip,int sections,int sides,glm::vec3 bark) {
        const auto axisReference=glm::normalize(curve(1)-curve(0));
        const auto reference=std::abs(axisReference.y)>.85f?glm::vec3(1,0,0):glm::vec3(0,1,0);
        const float length=glm::length(curve(1)-curve(0));
        const auto point=[&](int section,int side){
            const float t=float(section)/sections,u=float(side)/sides,angle=u*glm::two_pi<float>();
            const auto axis=glm::normalize(curve(std::min(1.f,t+.002f))-curve(std::max(0.f,t-.002f)));
            const auto right=glm::normalize(glm::cross(axis,reference));
            const auto normal=std::cos(angle)*right+std::sin(angle)*glm::cross(axis,right);
            const float ridge=.93f+.07f*std::cos(5*angle+t*.7f);
            const float radius=glm::mix(tip,base,std::pow(1-t,1.34f))*ridge;
            return Vertex{curve(t)+normal*radius,bark*(.87f+.13f*ridge),{u,t*length},0,normal};
        };
        for(int row=0;row<sections;++row)for(int side=0;side<sides;++side){
            const auto a=point(row,side),b=point(row,side+1),c=point(row+1,side+1),d=point(row+1,side);
            Quad(a,b,c,d);
        }
    }

    // A bent and folded blade keeps a rounded silhouette and catches light
    // along the midrib from either side.
    void Leaf(glm::vec3 base,glm::vec3 tip,glm::vec3 across,float width,float fold,glm::vec3 color) {
        const auto normal=glm::normalize(glm::cross(tip-base,across));
        const auto vertex=[&](float t,float side,float breadth){
            auto p=glm::mix(base,tip,t)+across*(width*breadth*side);
            p+=normal*(fold*breadth*(.7f-.65f*std::abs(side)));
            return Vertex{p,color*glm::mix(.72f,1.12f,t),{side*.5f+.5f,t},4,
                glm::normalize(normal+across*side*.4f)};
        };
        const auto a=vertex(0,0,0),b=vertex(.34f,-1,1),c=vertex(.34f,0,1),d=vertex(.34f,1,1);
        const auto e=vertex(.72f,-1,.64f),f=vertex(.72f,0,.64f),g=vertex(.72f,1,.64f),h=vertex(1,0,0);
        Triangle(a,b,c);Triangle(a,c,d);
        Quad(b,e,f,c);Quad(c,f,g,d);
        Triangle(e,h,f);Triangle(f,h,g);
    }

    void Spray(glm::vec3 base,glm::vec3 axis,float length,float width,glm::vec3 color,int planes,float roll) {
        const auto along=glm::normalize(axis);
        const auto reference=std::abs(along.y)>.9f?glm::vec3(1,0,0):glm::vec3(0,1,0);
        const auto across=glm::normalize(glm::cross(along,reference));
        const auto cross=glm::cross(along,across);
        for(int i=0;i<planes;++i){
            const float angle=roll+i*glm::pi<float>()/planes;
            const auto side=across*std::cos(angle)+cross*std::sin(angle);
            const auto normal=glm::normalize(glm::cross(side,along));
            const auto tip=base+along*length;
            Quad({base-side*width,color,{0,0},3,normal},{base+side*width,color,{1,0},3,normal},
                {tip+side*width,color,{1,1},3,normal},{tip-side*width,color,{0,1},3,normal});
        }
    }

private:
    void Triangle(const Vertex& a,const Vertex& b,const Vertex& c) {
        vertices.push_back(a);vertices.push_back(b);vertices.push_back(c);
    }
    void Quad(const Vertex& a,const Vertex& b,const Vertex& c,const Vertex& d) {
        Triangle(a,b,c);Triangle(a,c,d);
    }
};

inline glm::vec3 Radial(float angle) {return {std::cos(angle),0,std::sin(angle)};}

inline void Broadleaf(Builder& tree,bool detailed) {
    const glm::vec3 bark(.225f,.14f,.086f),dark(.094f,.218f,.047f),light(.203f,.369f,.071f);
    const glm::vec3 lightTop(.255f,.424f,.105f);
    const auto trunk=[](float t){return Bezier({0,-.12f,0},{-.56f,5.0f,.19f},{.28f,10.8f,.12f},t);};
    tree.Tube(trunk,.52f,.045f,detailed?10:5,detailed?16:8,bark);
    if(detailed)for(int root=0;root<5;++root){
        const auto out=Radial(root*glm::two_pi<float>()/5+.2f);
        tree.Branch({0,.6f,0},out*.62f+glm::vec3(0,.16f,0),out*1.02f-glm::vec3(0,.14f,0),.21f,.018f,4,7,bark);
    }
    for(int bough=0;bough<13;++bough){
        const auto seed=static_cast<unsigned>(bough)*127u+102u;
        const float level=bough/12.f,angle=bough*2.39996f+Random(seed)*.54f;
        const auto out=Radial(angle);
        const auto start=trunk(.33f+.53f*level);
        const float reach=glm::mix(3.95f,1.3f,level)*(.83f+.34f*Random(seed+1));
        const auto bend=start+out*(reach*.49f)+glm::vec3(0,.58f,0);
        const auto end=start+out*reach+glm::vec3(0,2.03f-level*.66f,0);
        tree.Branch(start,bend,end,glm::mix(.23f,.078f,level),.014f,detailed?6:3,detailed?9:6,bark);
        const int forkCount=detailed?6:4;
        for(int fork=0;fork<forkCount;++fork){
            const auto key=seed+static_cast<unsigned>(fork)*813u;
            const float t=.46f+.49f*fork/(forkCount-1);
            const auto node=Bezier(start,bend,end,t);
            const float side=fork%2?1.f:-1.f;
            const auto heading=Radial(angle+side*(.57f+.55f*Random(key+2)));
            const auto branchTip=node+heading*(.67f+.80f*Random(key+3))+glm::vec3(0,.45f+.55f*Random(key+4),0);
            tree.Branch(node,glm::mix(node,branchTip,.47f)-glm::vec3(0,.09f,0),branchTip,.057f,.009f,detailed?3:1,5,bark);
            const auto color=glm::mix(dark,light,.2f+.65f*Random(key+5));
            for(int cluster=0;cluster<(detailed?4:3);++cluster){
                const float yaw=angle+(cluster-1.5f)*.79f+Random(key+18u+cluster)*.44f;
                const auto direction=Radial(yaw)+glm::vec3(0,.30f+.45f*Random(key+31u+cluster),0);
                const auto root=glm::mix(node,branchTip,.61f+.13f*cluster);
                tree.Spray(root,direction,1.36f+.57f*Random(key+42u+cluster),.55f+.20f*Random(key+56u+cluster),color,
                    detailed?3:2,Random(key+66u+cluster)*glm::pi<float>());
                if(!detailed)continue;
                for(int leaf=0;leaf<6;++leaf){
                    const float tLeaf=.44f+.20f*(leaf/2),sign=leaf%2?-1.f:1.f;
                    const auto leafBase=root+direction*tLeaf;
                    const auto leafDir=Radial(yaw+sign*(.75f+.15f*leaf));
                    const auto tip=leafBase+leafDir*(.24f+.19f*Random(key+105u+leaf))+glm::vec3(0,.12f,0);
                    tree.Leaf(leafBase,tip,Radial(yaw+sign*(.75f+.15f*leaf)+glm::half_pi<float>()),.095f,.055f,color*1.1f);
                }
            }
        }
    }
    for(int tuft=0;tuft<9;++tuft){
        const auto direction=Radial(tuft*2.39996f)*.72f+glm::vec3(0,.65f,0);
        tree.Spray(trunk(.94f),direction,1.7f,.55f,lightTop,2,.4f*tuft);
    }
}

inline void Conifer(Builder& tree,bool detailed) {
    const glm::vec3 bark(.203f,.123f,.077f),deep(.047f,.146f,.080f),tip(.144f,.286f,.132f);
    const auto trunk=[](float t){return Bezier({0,-.1f,0},{-.18f,6.9f,.17f},{.12f,14.9f,.09f},t);};
    tree.Tube(trunk,.38f,.016f,detailed?12:6,detailed?14:8,bark);
    const int levels=detailed?14:11;
    for(int tier=0;tier<levels;++tier){
        const float t=float(tier)/(levels-1);
        const int branches=detailed?7:5;
        for(int b=0;b<branches;++b){
            const unsigned seed=static_cast<unsigned>(tier*37+b*197+71);
            const float angle=tier*.89f+b*glm::two_pi<float>()/branches+.30f*Random(seed);
            const auto out=Radial(angle);
            const float reach=(.15f+3.4f*std::pow(1-t,.82f))*(.80f+.37f*Random(seed+1));
            const auto base=trunk(.132f+.804f*t+.033f*Random(seed+2));
            const auto bend=base+out*reach*.59f-glm::vec3(0,.33f*(1-t),0);
            const auto end=base+out*reach+glm::vec3(0,.13f+.35f*Random(seed+64)-.22f*(1-t),0);
            tree.Branch(base,bend,end,.018f+reach*.021f,.006f,detailed?4:2,detailed?6:4,bark);
            const int sprays=detailed?7:4;
            for(int twig=0;twig<sprays;++twig){
                const float branchT=.34f+twig*.65f/(sprays-1),side=twig%2?1.f:-1.f;
                const auto root=Bezier(base,bend,end,branchT);
                const auto axis=Radial(angle+side*(.46f+.23f*Random(seed+23+twig)))+
                    glm::vec3(0,.36f+.39f*Random(seed+5+twig),0);
                const auto color=glm::mix(deep,tip,.20f+.64f*Random(seed+72+twig));
                tree.Spray(root,axis,.32f+reach*.45f,.10f+reach*.16f,color,detailed?3:2,.34f*side+.5f*Random(seed+92+twig));
            }
        }
    }
    for(int top=0;top<4;++top)
        tree.Spray(trunk(.959f),Radial(top*glm::half_pi<float>())*.20f+glm::vec3(0,1,0),1.18f,.17f,tip,2,.43f);
}

inline void Palm(Builder& tree,bool detailed) {
    const glm::vec3 base(0,-.12f,0),bend(.2f,5.1f,-.4f),crown(1.03f,10.8f,-.31f);
    const glm::vec3 bark(.262f,.194f,.127f),deep(.104f,.237f,.046f),light(.256f,.393f,.079f);
    tree.Branch(base,bend,crown,.35f,.145f,detailed?17:8,detailed?14:8,bark);
    const int fronds=detailed?16:12;
    for(int frond=0;frond<fronds;++frond){
        const auto seed=static_cast<unsigned>(frond)*193u+773u;
        const float angle=frond*2.39996f,reach=3.0f+2.2f*Random(seed),lift=1.34f+1.62f*Random(seed+1);
        const auto out=Radial(angle),across=Radial(angle+glm::half_pi<float>());
        const auto curve=[&](float t){return crown+out*(reach*t)+glm::vec3(0,lift*std::sin(t*glm::pi<float>())-1.65f*t*t,0);};
        tree.Tube(curve,.071f,.008f,detailed?20:12,6,light*.75f);
        const int pairs=detailed?23:13;
        for(int i=0;i<pairs;++i){
            const float t=.14f+.84f*i/(pairs-1);
            const float span=(.14f+.86f*std::pow(std::sin(t*glm::pi<float>()),.67f))*(.78f+.38f*Random(seed+41u+i));
            for(float side:{-1.f,1.f}){
                const auto p=curve(t);
                const auto q=p+across*(side*span)+out*(.30f+.52f*t)-glm::vec3(0,span*.48f,0);
                const float width=(detailed?.099f:.141f)*(1-.58f*t)*(.85f+.30f*Random(seed+65u+i));
                tree.Leaf(p,q,out,width,.081f*span,glm::mix(deep,light,.24f+.67f*Random(seed+97u+i)));
            }
        }
    }
}

inline std::vector<Vertex> Make(int species,bool detailed) {
    Builder tree;
    if(species==0)Broadleaf(tree,detailed);
    else if(species==1)Conifer(tree,detailed);
    else Palm(tree,detailed);
    return std::move(tree.vertices);
}
}

#pragma once
#include "glad/glad.h"
#include "TesseractSpace.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <vector>
#include <cmath>

unsigned int compileShaderProgram(const char*,const char*);

// An original child's bedroom, repeated in the interior lattice at metre scale.
class TesseractScene {
    struct Vertex {glm::vec3 position;glm::vec4 surface;};
    unsigned program=0,backdrop=0,gridProgram=0,vao=0,buffer=0,instances=0;
    int count=0,instanceCount=0;
public:
    bool Initialize(){
        const char* vertex=R"(#version 330 core
layout(location=0)in vec3 position;layout(location=1)in vec4 surface;
layout(location=2)in vec3 roomOffset;
uniform mat4 vp;out vec3 point,roomPoint;out vec4 material;
void main(){point=position+roomOffset;roomPoint=position;material=surface;gl_Position=vp*vec4(point,1);})";
        const char* fragment=R"(#version 330 core
in vec3 point,roomPoint;in vec4 material;uniform vec3 camera;uniform float time,fade;out vec4 result;
void main(){
 vec3 n=cross(dFdx(point),dFdy(point));
 vec3 normal=n*inversesqrt(max(dot(n,n),1e-18));
 vec3 lamp=vec3(1.78,1.21,-1.45)-roomPoint;
 float light=.38+.52*abs(dot(normal,normalize(vec3(-.5,.8,.4))));
 float pool=.65*abs(dot(normal,normalize(lamp+vec3(0,.001,0))))/(1.0+3.0*dot(lamp,lamp));
 float glow=material.a*(.9+.1*sin(time*.35+point.y*.14-point.z*.10));
 float fog=1.0-smoothstep(4.5,13.0,length(point-camera));
 if(fog<=0.0)discard;
 result=vec4(material.rgb*(light+glow+vec3(1.0,.7,.38)*pool),fade*fog);})";
        program=compileShaderProgram(vertex,fragment);int linked=0;glGetProgramiv(program,GL_LINK_STATUS,&linked);
        if(!linked){Release();return false;}
        const char* fullscreen=R"(#version 330 core
out vec2 screenPosition;
void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);screenPosition=p*2-1;gl_Position=vec4(screenPosition,0,1);})";
        backdrop=compileShaderProgram(fullscreen,
            R"(#version 330 core
uniform float fade;out vec4 result;void main(){result=vec4(0,0,0,fade);})");
        glGetProgramiv(backdrop,GL_LINK_STATUS,&linked);if(!linked){Release();return false;}
        // Evaluate projected slices through a 192 x 192 x 192 grid. The near
        // rooms use the same origin and pitch, so both views share parallax.
        gridProgram=compileShaderProgram(fullscreen,R"(#version 330 core
in vec2 screenPosition;
uniform vec3 camera,forward,right,up,pitch;
uniform vec2 lens;
uniform float pixelAngle,nearDistance,brightness,approach;
out vec4 result;

vec3 roomSlices(vec3 eye,vec3 ray,vec3 spacing){
    float facing=abs(ray.z);
    if(facing<.18)return vec3(0);
    float direction=sign(ray.z);
    float base=eye.z+nearDistance*ray.z;
    float plane=direction*ceil(direction*base/spacing.z);
    vec3 radiance=vec3(0);
    for(int layer=0;layer<10;++layer){
        float index=plane+float(layer)*direction;
        float distance=(index*spacing.z-eye.z)/ray.z;
        vec2 tile=(eye.xy+ray.xy*distance)/spacing.xy;
        vec2 room=floor(tile+.5);
        vec3 roomIndex=vec3(room,index);
        if(any(lessThan(roomIndex,vec3(-96))) || any(greaterThanEqual(roomIndex,vec3(96))))continue;
        vec2 cell=tile-room;
        vec2 filterWidth=clamp(vec2(distance*pixelAngle)/spacing.xy,vec2(.006),vec2(.18));
        vec2 aperture=mix(vec2(.074,.045),vec2(.103,.07),approach);
        vec2 windowShape=1.0-smoothstep(max(vec2(0),aperture-filterWidth),aperture+filterWidth,abs(cell-vec2(.14,.02)));
        float windowLight=windowShape.x*windowShape.y;
        vec2 border=abs(abs(cell)-vec2(.35,.29));
        vec2 frame=1.0-smoothstep(vec2(0),filterWidth+vec2(.009),border);
        float beams=max(frame.x*(1.0-smoothstep(.29,.32,abs(cell.y))),frame.y*(1.0-smoothstep(.35,.38,abs(cell.x))));
        float variation=fract(sin(dot(roomIndex,vec3(1.71,7.13,2.23)))*4375.15);
        float occupied=smoothstep(.57,.82,variation);
        float layerFade=smoothstep(0.0,spacing.z*(1.4/facing),distance-nearDistance)
            *(1.0-smoothstep(5.0,8.8,(distance-nearDistance)*facing/spacing.z));
        vec3 windowTint=mix(vec3(.40,.68,.84),vec3(.96,.68,.36),approach*.6+variation*.26);
        float distantFalloff=1.0/(1.0+float(layer)*mix(.33,.12,approach));
        float frameStrength=mix(.0025,.037,smoothstep(.35,.9,approach));
        radiance+=(windowLight*windowTint*occupied*.65+beams*frameStrength*vec3(.80,.92,1.0))*layerFade*distantFalloff;
    }
    return radiance*pow(facing,3.6);
}
void main(){
    vec3 ray=normalize(forward+right*screenPosition.x*lens.x+up*screenPosition.y*lens.y);
    vec3 grid=roomSlices(camera,ray,pitch)
        +roomSlices(camera.yzx,ray.yzx,pitch.yzx)
        +roomSlices(camera.zxy,ray.zxy,pitch.zxy);
    result=vec4(grid*brightness,0);
})");
        glGetProgramiv(gridProgram,GL_LINK_STATUS,&linked);if(!linked){Release();return false;}
        std::vector<Vertex> vertices;
        const auto box=[&](glm::vec3 c,glm::vec3 size,glm::vec3 color,float glow=0){
            const auto a=c-size*.5f,b=c+size*.5f;
            const glm::vec3 p[8]={{a.x,a.y,a.z},{b.x,a.y,a.z},{b.x,b.y,a.z},{a.x,b.y,a.z},
                {a.x,a.y,b.z},{b.x,a.y,b.z},{b.x,b.y,b.z},{a.x,b.y,b.z}};
            const int faces[36]={0,2,1,0,3,2,4,5,6,4,6,7,0,1,5,0,5,4,3,7,6,3,6,2,0,4,7,0,7,3,1,2,6,1,6,5};
            for(int i:faces)vertices.push_back({p[i],glm::vec4(color,glow)});
        };
        const auto oval=[&](glm::vec3 center,glm::vec3 radius,glm::vec3 color){
            const auto point=[&](int row,int col){
                const float lat=(row/6.f-.5f)*glm::pi<float>(),lon=col*glm::two_pi<float>()/10.f;
                return center+radius*glm::vec3(std::cos(lat)*std::cos(lon),std::sin(lat),std::cos(lat)*std::sin(lon));
            };
            for(int row=0;row<6;++row)for(int col=0;col<10;++col){
                const auto a=point(row,col),b=point(row+1,col),c=point(row+1,col+1),d=point(row,col+1);
                for(const auto& p:{a,b,c,a,c,d})vertices.push_back({p,glm::vec4(color,0)});
            }
        };
        const glm::vec3 wood(.28f,.135f,.064f),trim(.47f,.27f,.13f),wall(.48f,.40f,.29f),linen(.77f,.67f,.51f);
        const glm::vec3 lake(.11f,.27f,.28f),rose(.52f,.21f,.17f),gold(.70f,.30f,.07f);
        const float halfWidth=float(TesseractSpace::roomHalfSize.x),halfDepth=float(TesseractSpace::roomHalfSize.y);
        const float height=float(TesseractSpace::ceilingHeight);

        // Open wall and ceiling sections keep the adjacent rooms in view.
        box({0,-.094f,0},{2*halfWidth,.176f,2*halfDepth},wood);
        for(int plank=0;plank<18;++plank){
            const float shade=.86f+.055f*float((plank*7)%5);
            box({0,-.003f,-halfDepth+.1f+.2f*plank},{2*halfWidth,.006f,.197f},wood*shade);
        }
        box({0,height*.5f,-1.76f},{4.4f,height,.08f},wall);
        box({-2.16f,.57f,-.64f},{.08f,1.14f,2.24f},wall);
        box({-2.16f,2.41f,-.64f},{.08f,.48f,2.24f},wall);
        for(float z:{-1.58f,.29f})box({-2.16f,1.65f,z},{.08f,1.02f,.37f},wall);
        box({0,.065f,-1.69f},{4.36f,.13f,.05f},trim);
        box({-2.1f,.065f,-.63f},{.05f,.13f,2.18f},trim);
        box({0,2.58f,-1.54f},{4.4f,.14f,.52f},trim);

        // Cool window light, timber casing and folded curtains.
        box({-2.18f,1.65f,-.645f},{.035f,1.04f,1.53f},{.26f,.43f,.52f},.45f);
        for(float z:{-1.37f,-.645f,.08f})box({-2.12f,1.65f,z},{.06f,1.10f,.055f},trim);
        for(float y:{1.12f,1.65f,2.18f})box({-2.12f,y,-.645f},{.06f,.055f,1.50f},trim);
        box({-2.06f,1.09f,-.645f},{.22f,.055f,1.59f},trim);
        for(float side:{-1.f,1.f})for(int fold=0;fold<5;++fold){
            const float z=-.645f+side*(.49f+fold*.061f);
            box({-2.02f+.022f*(fold%2),1.69f,z},{.06f,1.17f,.065f},lake*(.84f+.07f*fold));
        }
        for(int fin=0;fin<11;++fin)box({-2.05f,.32f,-1.14f+.095f*fin},{.12f,.55f,.049f},linen*.83f);

        // A single bed with a pillow, patchwork quilt and aged metal footboard.
        box({-1.42f,.24f,-.44f},{1.04f,.21f,2.03f},wood);
        box({-1.42f,.52f,-1.46f},{1.10f,1.04f,.09f},trim);
        box({-1.42f,.39f,-.43f},{.98f,.14f,1.95f},linen);
        oval({-1.42f,.51f,-1.15f},{.34f,.075f,.19f},linen);
        box({-1.42f,.47f,-.19f},{1.03f,.08f,1.49f},lake);
        for(int row=0;row<7;++row)for(int col=0;col<5;++col){
            const auto color=(row+col)%3==0?rose:(row+col)%3==1?lake:linen*.77f;
            box({-1.42f+(col-2)*.20f,.513f,-.19f+(row-3)*.205f},{.193f,.015f,.197f},color);
        }
        box({-1.42f,.40f,.52f},{1.02f,.15f,.06f},rose);
        for(float x:{-1.84f,-1.0f})for(float z:{-1.34f,.46f})box({x,.095f,z},{.085f,.19f,.085f},wood);
        const glm::vec3 brass(.42f,.32f,.18f);
        for(float x:{-1.93f,-.91f}){
            box({x,.41f,.56f},{.042f,.82f,.042f},brass,.08f);
            oval({x,.825f,.56f},{.033f,.032f,.033f},brass);
        }
        for(float y:{.32f,.71f})box({-1.42f,y,.56f},{1.0f,.031f,.031f},brass,.08f);
        for(int rail=0;rail<6;++rail)box({-1.77f+rail*.14f,.515f,.56f},{.019f,.39f,.019f},brass);

        // Stuffed bear and a patterned rug give the room a child's scale.
        const glm::vec3 bear(.46f,.25f,.104f),nose(.10f,.043f,.02f);
        oval({-1.11f,.61f,-.81f},{.085f,.12f,.067f},bear);
        oval({-1.11f,.77f,-.81f},{.09f,.082f,.075f},bear);
        for(float side:{-1.f,1.f}){
            oval({-1.11f+side*.068f,.838f,-.816f},{.039f,.042f,.025f},bear);
            oval({-1.11f+side*.035f,.787f,-.745f},{.009f,.011f,.009f},nose);
            oval({-1.11f+side*.082f,.584f,-.76f},{.037f,.064f,.046f},bear);
        }
        oval({-1.11f,.748f,-.737f},{.044f,.031f,.025f},linen);
        oval({-1.11f,.756f,-.714f},{.015f,.010f,.010f},nose);
        box({.06f,.011f,.39f},{1.42f,.022f,1.60f},rose*.67f);
        for(float z:{-.31f,1.09f})box({.06f,.025f,z},{1.31f,.008f,.035f},linen*.78f);
        for(float x:{-.57f,.69f})box({x,.025f,.39f},{.035f,.008f,1.42f},linen*.78f);

        // Tall bookcase, study desk and drawings pinned to a corkboard.
        for(float x:{-2.08f,-.43f,.75f})box({x,1.15f,-1.46f},{.063f,2.3f,.41f},wood);
        box({-.665f,1.15f,-1.65f},{2.83f,2.3f,.04f},wood*.66f);
        box({-.665f,2.285f,-1.45f},{2.83f,.07f,.44f},trim);
        for(int shelf=0;shelf<=5;++shelf){
            const float y=.05f+.369f*shelf;
            box({-.665f,y,-1.45f},{2.83f,.043f,.44f},trim);
            for(int book=0;book<25;++book){
                if((book+shelf*3)%11==5 || book==14)continue;
                const float bookHeight=.17f+.024f*((book*3+shelf*5)%7);
                const float x=-1.98f+.108f*book;
                const auto color=(book+shelf)%3==0?rose:(book+shelf)%3==1?lake:linen*.56f;
                box({x,y+.03f+bookHeight*.5f,-1.425f},{.089f,bookHeight,.29f},color);
                box({x,y+.05f+bookHeight*.68f,-1.277f},{.05f,.012f,.006f},linen*.78f);
            }
        }
        box({.84f,.029f,.27f},{.18f,.058f,.26f},rose);
        box({.84f,.030f,.278f},{.16f,.038f,.25f},linen);
        box({.97f,.019f,.49f},{.19f,.038f,.24f},lake);
        box({1.48f,.76f,-1.36f},{1.19f,.065f,.64f},trim);
        for(float x:{.97f,1.98f})for(float z:{-1.10f,-1.60f})box({x,.36f,z},{.065f,.73f,.065f},wood);
        box({1.79f,.642f,-1.31f},{.53f,.16f,.53f},wood);
        box({1.79f,.64f,-1.04f},{.16f,.026f,.025f},gold);
        box({1.78f,.81f,-1.45f},{.21f,.035f,.18f},wood);
        box({1.78f,.98f,-1.45f},{.028f,.33f,.028f},trim);
        box({1.78f,1.19f,-1.45f},{.23f,.14f,.21f},linen,.38f);
        box({1.24f,.803f,-1.32f},{.25f,.015f,.19f},linen);
        box({1.36f,.816f,-1.29f},{.012f,.013f,.145f},rose);
        box({1.41f,.42f,-.78f},{.44f,.075f,.43f},lake);
        box({1.41f,.63f,-.57f},{.44f,.42f,.055f},wood);
        for(float x:{1.23f,1.59f})for(float z:{-.61f,-.94f})box({x,.20f,z},{.049f,.40f,.049f},wood);
        box({1.47f,1.75f,-1.70f},{.91f,.61f,.043f},trim);
        box({1.47f,1.75f,-1.673f},{.85f,.55f,.017f},{.40f,.24f,.13f});
        for(int note=0;note<3;++note){
            const float x=1.21f+.255f*note,y=1.73f+.09f*(note%2);
            box({x,y,-1.661f},{.203f,.279f,.009f},linen);
            box({x,y+.078f,-1.654f},{.10f,.044f,.004f},lake);
            box({x,y-.018f,-1.654f},{.064f,.10f,.004f},rose);
            box({x+.041f,y+.127f,-1.651f},{.024f,.024f,.012f},gold);
        }

        const glm::vec3 pitch(TesseractSpace::roomPitch);
        for(float side:{-1.f,1.f}){
            box({side*halfWidth,height,-halfDepth},{.022f,.022f,pitch.z},gold,.35f);
            box({side*halfWidth,height*.5f,halfDepth},{.021f,pitch.y,.021f},gold,.35f);
            box({0,height,side*halfDepth},{pitch.x,.018f,.018f},gold,.20f);
        }
        std::vector<glm::vec3> offsets;
        for(int z=-3;z<=3;++z)for(int y=-3;y<=3;++y)for(int x=-3;x<=3;++x)
            offsets.push_back(pitch*glm::vec3(x,y,z));
        count=static_cast<int>(vertices.size());instanceCount=static_cast<int>(offsets.size());
        glGenVertexArrays(1,&vao);glGenBuffers(1,&buffer);glGenBuffers(1,&instances);
        glBindVertexArray(vao);glBindBuffer(GL_ARRAY_BUFFER,buffer);
        glBufferData(GL_ARRAY_BUFFER,vertices.size()*sizeof(Vertex),vertices.data(),GL_STATIC_DRAW);
        glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(Vertex),nullptr);glEnableVertexAttribArray(0);
        glVertexAttribPointer(1,4,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<void*>(sizeof(glm::vec3)));glEnableVertexAttribArray(1);
        glBindBuffer(GL_ARRAY_BUFFER,instances);
        glBufferData(GL_ARRAY_BUFFER,offsets.size()*sizeof(glm::vec3),offsets.data(),GL_STATIC_DRAW);
        glVertexAttribPointer(2,3,GL_FLOAT,GL_FALSE,sizeof(glm::vec3),nullptr);glEnableVertexAttribArray(2);glVertexAttribDivisor(2,1);
        glBindVertexArray(0);return true;
    }
    void Render(const glm::dvec3& position,const glm::vec3& front,const glm::vec3& up,float aspect,float fov,
        double seconds,float gridFade,float approach,float opacity)const{
        if(gridFade<=0)return;
        const auto pitch=TesseractSpace::roomPitch;
        const glm::vec3 camera(glm::mod(position+pitch*.5,pitch)-pitch*.5);
        const auto vp=glm::perspective(glm::radians(fov),aspect,.04f,120.f)*glm::lookAt(camera,camera+front,up);
        glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);glBindVertexArray(vao);
        glEnable(GL_BLEND);glBlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ZERO,GL_ZERO);
        glDisable(GL_DEPTH_TEST);glDepthMask(GL_FALSE);glUseProgram(backdrop);
        glUniform1f(glGetUniformLocation(backdrop,"fade"),gridFade);
        glDrawArrays(GL_TRIANGLES,0,3);
        glUseProgram(gridProgram);
        glBlendFuncSeparate(GL_ONE,GL_ONE,GL_ZERO,GL_ZERO);
        const auto right=glm::normalize(glm::cross(front,up));
        const auto vertical=glm::cross(right,front);
        glUniform3fv(glGetUniformLocation(gridProgram,"camera"),1,glm::value_ptr(glm::vec3(position)));
        glUniform3fv(glGetUniformLocation(gridProgram,"pitch"),1,glm::value_ptr(glm::vec3(pitch)));
        glUniform3fv(glGetUniformLocation(gridProgram,"forward"),1,glm::value_ptr(front));
        glUniform3fv(glGetUniformLocation(gridProgram,"right"),1,glm::value_ptr(right));
        glUniform3fv(glGetUniformLocation(gridProgram,"up"),1,glm::value_ptr(vertical));
        const float halfHeight=std::tan(glm::radians(fov)*.5f);
        int viewport[4];glGetIntegerv(GL_VIEWPORT,viewport);
        glUniform2f(glGetUniformLocation(gridProgram,"lens"),halfHeight*aspect,halfHeight);
        glUniform1f(glGetUniformLocation(gridProgram,"pixelAngle"),2*halfHeight/std::max(1,viewport[3]));
        glUniform1f(glGetUniformLocation(gridProgram,"nearDistance"),glm::mix(250.f,13.f,approach));
        glUniform1f(glGetUniformLocation(gridProgram,"approach"),approach);
        glUniform1f(glGetUniformLocation(gridProgram,"brightness"),gridFade*glm::mix(1.f,.22f,opacity));
        glDrawArrays(GL_TRIANGLES,0,3);
        glEnable(GL_DEPTH_TEST);glDepthMask(GL_TRUE);glClear(GL_DEPTH_BUFFER_BIT);
        if(opacity<=0){glBindVertexArray(0);glDisable(GL_BLEND);return;}
        glBlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ZERO,GL_ZERO);
        glUseProgram(program);glUniformMatrix4fv(glGetUniformLocation(program,"vp"),1,GL_FALSE,glm::value_ptr(vp));
        glUniform3fv(glGetUniformLocation(program,"camera"),1,glm::value_ptr(camera));
        glUniform1f(glGetUniformLocation(program,"time"),float(std::fmod(seconds,3600.0)));
        glUniform1f(glGetUniformLocation(program,"fade"),opacity);
        glDrawArraysInstanced(GL_TRIANGLES,0,count,instanceCount);glBindVertexArray(0);glDisable(GL_BLEND);
    }
    void Release(){
        if(program)glDeleteProgram(program);
        if(backdrop)glDeleteProgram(backdrop);
        if(gridProgram)glDeleteProgram(gridProgram);
        if(vao)glDeleteVertexArrays(1,&vao);
        if(buffer)glDeleteBuffers(1,&buffer);
        if(instances)glDeleteBuffers(1,&instances);
        program=backdrop=gridProgram=vao=buffer=instances=0;
        count=instanceCount=0;
    }
};

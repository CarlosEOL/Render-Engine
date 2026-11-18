#pragma once
#include "glad/glad.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <vector>
#include <cmath>

unsigned int compileShaderProgram(const char*,const char*);

// Original cinematic bookshelf lattice. All scene coordinates are metres;
// bounded repetition and distance fog suggest an extended space in one draw.
class TesseractScene {
    struct Vertex {glm::vec3 position,color;};
    unsigned program=0,backdrop=0,vao=0,buffer=0;int count=0;
public:
    bool Initialize(){
        const char* vertex=R"(#version 330 core
layout(location=0)in vec3 position;layout(location=1)in vec3 color;
uniform mat4 vp;uniform vec3 camera;out vec3 material,point;out float distanceFromCamera;
void main(){point=position;material=color;distanceFromCamera=length(position-camera);gl_Position=vp*vec4(position,1);})";
        const char* fragment=R"(#version 330 core
in vec3 material,point;in float distanceFromCamera;uniform float time,fade;out vec4 result;
void main(){
 vec3 normal=normalize(cross(dFdx(point),dFdy(point)));
 float light=.30+.70*abs(dot(normal,normalize(vec3(.3,.8,.5))));
 float pulse=.90+.10*sin(time*.35+point.y*.14-point.z*.10);
 float fog=exp(-pow(distanceFromCamera/22.0,2.0));
 result=vec4(material*light*pulse*fog,fade);})";
        program=compileShaderProgram(vertex,fragment);int linked=0;glGetProgramiv(program,GL_LINK_STATUS,&linked);
        if(!linked){Release();return false;}
        backdrop=compileShaderProgram(R"(#version 330 core
void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2-1,0,1);})",
            R"(#version 330 core
uniform float fade;out vec4 result;void main(){result=vec4(0,0,0,fade);})");
        glGetProgramiv(backdrop,GL_LINK_STATUS,&linked);if(!linked){Release();return false;}
        std::vector<Vertex> vertices;vertices.reserve(400000);
        const auto box=[&](glm::vec3 c,glm::vec3 size,glm::vec3 color){
            const auto a=c-size*.5f,b=c+size*.5f;
            const glm::vec3 p[8]={{a.x,a.y,a.z},{b.x,a.y,a.z},{b.x,b.y,a.z},{a.x,b.y,a.z},
                {a.x,a.y,b.z},{b.x,a.y,b.z},{b.x,b.y,b.z},{a.x,b.y,b.z}};
            const int faces[36]={0,2,1,0,3,2,4,5,6,4,6,7,0,1,5,0,5,4,3,7,6,3,6,2,0,4,7,0,7,3,1,2,6,1,6,5};
            for(int i:faces)vertices.push_back({p[i],color});
        };
        for(int z=-3;z<=3;++z)for(int y=-3;y<=3;++y)for(int x=-3;x<=3;++x){
            const glm::vec3 c(x*12.f,y*10.f,z*12.f);
            const glm::vec3 gold(.62f,.25f,.045f),wood(.16f,.065f,.024f);
            box(c+glm::vec3(0,-4.8f,0),{8.4f,.12f,9.8f},wood);
            for(float a:{-1.f,1.f})for(float b:{-1.f,1.f}){
                box(c+glm::vec3(a*5.8f,b*4.8f,0),{.055f,.055f,12},gold);
                box(c+glm::vec3(a*5.8f,0,b*5.8f),{.055f,10,.055f},gold);
                box(c+glm::vec3(0,a*4.8f,b*5.8f),{12,.055f,.055f},gold);
            }
            for(int row=0;row<4;++row){
                const float shelf=-3.8f+row*2.2f;
                box(c+glm::vec3(0,shelf,-5.8f),{8,.15f,.7f},wood);
                for(int book=0;book<10;++book){
                    const unsigned h=unsigned(((x+4)%2)*7919+((y+4)%2)*131+((z+4)%2)*37+row*997+book*17);
                    const float height=.8f+.8f*((h*1664525u+1013904223u)%97)/97.f;
                    const glm::vec3 colors[4]={{.28f,.09f,.032f},{.07f,.15f,.16f},{.28f,.22f,.09f},{.12f,.075f,.05f}};
                    const auto centre=c+glm::vec3(-3.6f+book*.76f,shelf+height*.5f+.1f,-5.8f);
                    box(centre,{.52f,height,.5f},colors[h%4]);
                    box(centre+glm::vec3(0,height*.32f,.26f),{.40f,.035f,.015f},gold);
                }
            }
            for(float side:{-1.f,1.f})box(c+glm::vec3(side*4.2f,.1f,-5.8f),{.17f,8.8f,.8f},wood);
            // Repeated room windows offer a contrasting view beyond the shelves.
            box(c+glm::vec3(5.8f,.2f,1.3f),{.04f,4.2f,3.1f},{.23f,.32f,.36f});
            box(c+glm::vec3(5.75f,.2f,1.3f),{.08f,4.4f,.09f},gold);
            box(c+glm::vec3(5.75f,.2f,1.3f),{.08f,.09f,3.3f},gold);
        }
        count=static_cast<int>(vertices.size());glGenVertexArrays(1,&vao);glGenBuffers(1,&buffer);
        glBindVertexArray(vao);glBindBuffer(GL_ARRAY_BUFFER,buffer);
        glBufferData(GL_ARRAY_BUFFER,vertices.size()*sizeof(Vertex),vertices.data(),GL_STATIC_DRAW);
        glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(Vertex),nullptr);glEnableVertexAttribArray(0);
        glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<void*>(sizeof(glm::vec3)));glEnableVertexAttribArray(1);
        glBindVertexArray(0);return true;
    }
    void Render(const glm::dvec3& position,const glm::vec3& front,const glm::vec3& up,float aspect,float fov,double seconds,float opacity)const{
        if(opacity<=0)return;
        const glm::vec3 camera(glm::mod(position+glm::dvec3(12,10,12),glm::dvec3(24,20,24))-glm::dvec3(12,10,12));
        const auto vp=glm::perspective(glm::radians(fov),aspect,.04f,120.f)*glm::lookAt(camera,camera+front,up);
        glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);glBindVertexArray(vao);
        glEnable(GL_BLEND);glBlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ZERO,GL_ZERO);
        glDisable(GL_DEPTH_TEST);glDepthMask(GL_FALSE);glUseProgram(backdrop);
        glUniform1f(glGetUniformLocation(backdrop,"fade"),opacity);glDrawArrays(GL_TRIANGLES,0,3);
        glEnable(GL_DEPTH_TEST);glDepthMask(GL_TRUE);glClear(GL_DEPTH_BUFFER_BIT);
        glUseProgram(program);glUniformMatrix4fv(glGetUniformLocation(program,"vp"),1,GL_FALSE,glm::value_ptr(vp));
        glUniform3fv(glGetUniformLocation(program,"camera"),1,glm::value_ptr(camera));
        glUniform1f(glGetUniformLocation(program,"time"),float(std::fmod(seconds,3600.0)));
        glUniform1f(glGetUniformLocation(program,"fade"),opacity);
        glDrawArrays(GL_TRIANGLES,0,count);glBindVertexArray(0);glDisable(GL_BLEND);
    }
    void Release(){if(program)glDeleteProgram(program);if(backdrop)glDeleteProgram(backdrop);if(vao)glDeleteVertexArrays(1,&vao);if(buffer)glDeleteBuffers(1,&buffer);program=backdrop=vao=buffer=0;count=0;}
};

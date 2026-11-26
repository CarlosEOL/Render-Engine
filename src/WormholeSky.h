#pragma once
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <limits>
#include <initializer_list>
#include <utility>

template<int size> class SkyCubeCapture {
public:
    template<class Draw> unsigned Capture(Draw draw) {
        GLint viewport[4],drawFbo,readFbo,renderbuffer,program,vao,active,depthFunc;
        GLfloat clear[4];GLboolean depthMask;
        glGetIntegerv(GL_VIEWPORT,viewport);glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&drawFbo);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&readFbo);glGetIntegerv(GL_RENDERBUFFER_BINDING,&renderbuffer);
        glGetIntegerv(GL_CURRENT_PROGRAM,&program);glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&vao);
        glGetIntegerv(GL_ACTIVE_TEXTURE,&active);glGetIntegerv(GL_DEPTH_FUNC,&depthFunc);
        GLint textures[8][2];
        for(int unit=0;unit<8;++unit){glActiveTexture(GL_TEXTURE0+unit);glGetIntegerv(GL_TEXTURE_BINDING_2D,&textures[unit][0]);glGetIntegerv(GL_TEXTURE_BINDING_CUBE_MAP,&textures[unit][1]);}
        glActiveTexture(GL_TEXTURE0);
        glGetFloatv(GL_COLOR_CLEAR_VALUE,clear);glGetBooleanv(GL_DEPTH_WRITEMASK,&depthMask);
        const bool depth=glIsEnabled(GL_DEPTH_TEST),blend=glIsEnabled(GL_BLEND),scissor=glIsEnabled(GL_SCISSOR_TEST);
        if(!texture){
            glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_CUBE_MAP,texture);
            for(int face=0;face<6;++face)glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X+face,0,GL_RGBA16F,size,size,0,GL_RGBA,GL_FLOAT,nullptr);
            glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
            for(auto axis:{GL_TEXTURE_WRAP_S,GL_TEXTURE_WRAP_T,GL_TEXTURE_WRAP_R})glTexParameteri(GL_TEXTURE_CUBE_MAP,axis,GL_CLAMP_TO_EDGE);
            glGenFramebuffers(1,&fbo);glGenRenderbuffers(1,&zbuffer);glBindRenderbuffer(GL_RENDERBUFFER,zbuffer);
            glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT24,size,size);
        }
        glBindFramebuffer(GL_FRAMEBUFFER,fbo);glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,zbuffer);
        glViewport(0,0,size,size);glDisable(GL_SCISSOR_TEST);glDisable(GL_BLEND);glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LESS);glDepthMask(GL_TRUE);
        const glm::vec3 dirs[]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
        const glm::vec3 ups[]={{0,-1,0},{0,-1,0},{0,0,1},{0,0,-1},{0,-1,0},{0,-1,0}};
        bool complete=true;
        for(int face=0;face<6;++face){
            glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_CUBE_MAP_POSITIVE_X+face,texture,0);
            if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE){complete=false;break;}
            glDepthMask(GL_TRUE);glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
            draw(dirs[face],ups[face]);
        }
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,drawFbo);glBindFramebuffer(GL_READ_FRAMEBUFFER,readFbo);
        glBindRenderbuffer(GL_RENDERBUFFER,renderbuffer);glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);
        glDepthFunc(depthFunc);glDepthMask(depthMask);if(depth)glEnable(GL_DEPTH_TEST);else glDisable(GL_DEPTH_TEST);
        if(blend)glEnable(GL_BLEND);else glDisable(GL_BLEND);if(scissor)glEnable(GL_SCISSOR_TEST);else glDisable(GL_SCISSOR_TEST);
        glClearColor(clear[0],clear[1],clear[2],clear[3]);glUseProgram(program);glBindVertexArray(vao);
        for(int unit=0;unit<8;++unit){glActiveTexture(GL_TEXTURE0+unit);glBindTexture(GL_TEXTURE_2D,textures[unit][0]);glBindTexture(GL_TEXTURE_CUBE_MAP,textures[unit][1]);}
        glActiveTexture(active);
        return complete?texture:0;
    }
    void Release(){if(texture)glDeleteTextures(1,&texture);if(fbo)glDeleteFramebuffers(1,&fbo);if(zbuffer)glDeleteRenderbuffers(1,&zbuffer);texture=fbo=zbuffer=0;}
private:
    unsigned texture=0,fbo=0,zbuffer=0;
};

// Capture the destination once per simulated hour without rendering either mouth recursively.
class WormholeSkyCache {
public:
    template<class Draw> unsigned Capture(double seconds, Draw draw) {
        const double next=std::floor(seconds/3600.0);
        if(texture && next==bucket)return texture;
        texture=cube.Capture(std::move(draw));
        if(texture)bucket=next;
        return texture;
    }
    void Release(){cube.Release();texture=0;}
private:
    SkyCubeCapture<512> cube;
    unsigned texture=0;
    double bucket=std::numeric_limits<double>::quiet_NaN();
};

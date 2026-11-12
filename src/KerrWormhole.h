#pragma once
#include <glm/glm.hpp>

class KerrWormhole {
public:
    bool Initialize();
    void Release();
    void SetEnabled(bool value) {enabled=value;}
    void SetDiagnostic(bool value) {diagnostic=value;}
    void Render(const glm::mat4& vp,const glm::dvec3& centerFromCamera,double radiusAu,
                unsigned int skyCube) const;
private:
    unsigned int program=0,vao=0,color=0,depth=0;
    mutable int width=0,height=0;
    bool enabled=true,diagnostic=false;
};

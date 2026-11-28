#pragma once
#include <glm/glm.hpp>
#include "PlanetWeather.h"

class Atmosphere {
public:
    enum class Scattering { Standard, Earth, DetailedEarth };
    static constexpr Scattering EarthScattering(double cameraDistanceAu) {
        return cameraDistanceAu <= .5 ? Scattering::DetailedEarth : Scattering::Earth;
    }

    bool Initialize();
    void Render(const glm::mat4& viewProjection, const glm::vec3& camera,
                const glm::vec3& center, float radius, bool logarithmicDepth = false,
                const glm::vec3& lightPosition = glm::vec3(0),bool earthClouds=false,
                double seconds=0,const glm::mat3& rotation=glm::mat3(1),bool oceanReflections=false,float waterSpecularScale=1,
                const PlanetWeather::State* weather=nullptr,
                glm::vec2 marineHazeLight=glm::vec2(0),bool captureDiskForeground=false,bool renderAir=true,
                float depthBelowWater=0,bool reflectionCapture=false,Scattering scattering=Scattering::Standard) const;
    void ResetDiskForeground() const { diskForegroundIndex=-1; }
    void ResetLocalThunder() const { thunderTime=-1; }
    double LocalThunder() const { return thunderTime; }
    unsigned int DiskForegroundTexture() const {
        return diskForegroundIndex<0?0:diskForeground[diskForegroundIndex].texture;
    }
    void Release();
private:
    unsigned int program = 0;
    unsigned int vao = 0;
    struct SceneCopy {
        unsigned color=0,depth=0;
        int width=0,height=0;
    };
    mutable SceneCopy cameraCopy,reflectionCopy;
    unsigned int diskForegroundFramebuffer=0;
    struct ForegroundLayer {
        unsigned int texture=0;
        int width=0,height=0;
    };
    mutable ForegroundLayer diskForeground[2];
    mutable int diskForegroundIndex=-1;
    mutable double thunderTime=-1;
};

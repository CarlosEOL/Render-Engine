#pragma once

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <vector>
#include <glm/glm.hpp>

namespace NewYorkCity {

inline constexpr double latitude = 40.7128;
inline constexpr double longitude = -74.006;
inline constexpr float detailMetres = 100.f;
inline constexpr glm::dvec4 reliefBounds(-74.30,40.455,-73.65,40.955);

glm::dmat3 Frame();
std::filesystem::path AssetPath(const char* name);
bool HasCity();

class Renderer {
public:
    void Render(const glm::mat4& viewProjection,const glm::dmat3& rotation,
        const glm::dvec3& centreRelativeAu,double radiusAu,const glm::vec3& light);
    void Release();
    void ResetVisibility() { drawnTiles=detailedTiles=0; streaming=false; }
    unsigned VisibleTiles() const { return drawnTiles; }
    unsigned DetailedTiles() const { return detailedTiles; }
    bool NeedsFrame() const { return streaming; }
    std::size_t BuildingFootprints() const { return footprintCount; }

private:
    struct Mesh { unsigned vao=0,buffer=0; int vertices=0; };
    struct Tile {
        int column=0,row=0;
        unsigned buildings=0,roads=0,offset=0,bytes=0;
    };
    struct TileMeshes {
        Mesh block,detail,roads;
        unsigned seen=0;
        float highest=0;
        bool nearReady=false,ready=false;
    };
    bool Open();
    void Build(const Tile& tile,TileMeshes& meshes,bool detail);
    static void ReleaseMesh(Mesh& mesh);

    std::ifstream archive;
    std::vector<Tile> tiles;
    std::unordered_map<std::size_t,TileMeshes> resident;
    unsigned program=0,frame=0,drawnTiles=0,detailedTiles=0;
    bool attempted=false,streaming=false;
    std::size_t footprintCount=0;
};

}

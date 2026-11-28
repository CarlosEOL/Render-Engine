#pragma once

#include <algorithm>
#include <array>
#include <iterator>
#include <vector>
#include <glm/glm.hpp>

namespace EarthWonders {

struct Site {
    const char* key;
    const char* name;
    const char* aliases;
    const char* description;
    double latitude, longitude;
    glm::dvec3 viewpoint, focus; // East, up, south in metres from the site.
    double extent;
    bool submerged = false;

    glm::dmat3 Frame() const;
    glm::dvec3 Direction() const { return Frame()[1]; }
    double Elevation() const;
};

inline const std::array<Site,6> sites{{
    {"giza","Giza Pyramids","pyramid pyrimids khufu cheops khafre menkaure","Ancient Egyptian pyramid complex",
        29.9761,31.1308,{800,205,220},{0,60,0},690},
    {"el-castillo","Chichen Itza (El Castillo)","chichen itza chichén itzá kukulkan maya pyramid","Maya temple pyramid",
        20.6829,-88.5686,{66,24,85},{0,13,0},75},
    {"colosseum","Colosseum","coliseum rome flavian amphitheatre","Roman amphitheatre",
        41.8902,12.4923,{160,76,135},{0,17,0},115},
    {"taj-mahal","Taj Mahal","tajmahal agra","Mughal mausoleum",
        27.1751,78.0421,{95,38,165},{0,29,0},150},
    {"mutianyu","Great Wall (Mutianyu)","great wall of china mutianyu","Great Wall of China / Mutianyu section",
        40.4314,116.5644,{65,145,250},{0,15,0},300},
    {"atlantis","Atlantis","lost city sunken city ruins","Fictional underwater city",
        27.1,-79.0,{4,9,64},{0,10,33},58,true}
}};

enum class LandmarkForm {
    Tower, Arch, Cathedral, Mosque, Palace, Fortress, Temple, Skyscraper,
    Monument, House, Bridge, Ruin, Eiffel, Liberty, Opera, Pagoda, Windmill,
    Atomium, Sagrada, Christ, Pyramid, Obelisk, Dome, StBasils, Petronas,
    BurjKhalifa, BigBen, LeaningTower, Kaaba
};

struct Landmark {
    Site site;
    LandmarkForm form;
    float width, height, depth;
};

inline const Landmark landmarks[]{
#define LANDMARK(code, country, name, lat, lon, form, width, height, depth) \
    {{"landmark-" #code, name, country, "Earth landmark / procedural model", lat, lon, \
        {std::max(18.0,width*.89),std::max(16.0,height*.65+width*.55), \
            (LandmarkForm::form==LandmarkForm::Opera?-1:1)*std::max(21.0,depth*.92+height*.94)}, \
        {0,height*.46,0}, std::max<double>(width,std::max<double>(height,depth))}, \
        LandmarkForm::form, static_cast<float>(width), static_cast<float>(height), static_cast<float>(depth)},
#include "EarthLandmarkSites.inl"
#undef LANDMARK
};

inline const std::array<Site,5> wallViews{{
    {"dunhuang-wall","Great Wall (Dunhuang)","great wall china han dunhuang yumen","Great Wall of China / Han earthen wall",
        40.350556,93.864354,{35,42,88},{0,2,0},125},
    {"jiayuguan-wall","Great Wall (Jiayuguan)","great wall china jiayuguan gansu","Great Wall of China / western Ming wall",
        39.800333,98.215600,{60,70,145},{0,3,0},200},
    {"simatai-wall","Great Wall (Simatai foothills)","great wall china simatai beijing","Great Wall of China / Ming mountain wall",
        40.661322,117.272670,{45,113,126},{0,3,0},200},
    {"shanhaiguan-wall","Great Wall (Shanhaiguan)","great wall china shanhaiguan hebei laolongtou","Great Wall of China / eastern Ming wall",
        40.032212,119.766662,{90,110,157},{0,3,0},220},
    {"jin-border-trench","Great Wall (Jin Border Trench)","great wall china jin border trench inner mongolia","Great Wall of China / Jin defensive earthworks",
        42.554725,114.450017,{28,27,72},{0,0,0},125}
}};

inline const Site& Destination(unsigned index) {
    if(index<sites.size())return sites.at(index);
    index-=static_cast<unsigned>(sites.size());
    if(index<wallViews.size())return wallViews.at(index);
    return landmarks[index-wallViews.size()].site;
}

inline constexpr unsigned destinationCount=static_cast<unsigned>(sites.size()+wallViews.size()+std::size(landmarks));

class Renderer {
public:
    void Render(const glm::mat4& viewProjection, const glm::dmat3& rotation,
                const glm::dvec3& centreRelativeAu, double radiusAu,
                const glm::vec3& light, bool underwater);
    unsigned VisibleSites() const { return visibleSites; }
    unsigned VisibleLandmarks() const { return visibleLandmarks; }
    unsigned VisibleWallTiles() const { return visibleWallTiles; }
    bool NeedsFrame() const { return pendingWallTiles; }
    void ResetVisibility() { visibleSites = visibleLandmarks = visibleWallTiles = 0; pendingWallTiles=false; }
    void Release();

private:
    struct Mesh { unsigned vao=0, buffer=0; int vertices=0; double elevation=0; };
    struct WallTileMesh {
        unsigned tile=0;
        glm::dmat3 frame{1};
        Mesh mesh;
        bool detailed=false;
    };
    static void Upload(Mesh& mesh,const void* vertices,unsigned count,double elevation);
    static void Free(Mesh& mesh);
    std::array<Mesh,sites.size()> meshes{};
    std::vector<WallTileMesh> wallMeshes;
    Mesh landmarkMesh;
    int landmarkIndex=-1;
    bool pendingWallTiles=false;
    unsigned program=0, visibleSites=0, visibleLandmarks=0, visibleWallTiles=0;
};

}

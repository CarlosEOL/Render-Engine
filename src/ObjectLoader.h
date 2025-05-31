#pragma once
#include <string>
#include <vector>
#include <glm/vec3.hpp>

class ObjectLoader {
public:
    glm::vec3 center;
    static bool LoadOBJ(const std::string& path, std::vector<float>& outVertices, glm::vec3& center);
    static bool LoadFBX(const std::string& path, std::vector<float>& outVertices, glm::vec3& center);
};


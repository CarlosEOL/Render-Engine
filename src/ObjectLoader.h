#pragma once
#include <string>
#include <vector>

class ObjectLoader {
public:
    static bool LoadOBJ(const std::string& path, std::vector<float>& outVertices);
    static bool LoadFBX(const std::string& path, std::vector<float>& outVertices);
};


#include "ObjectLoader.h"

// ModelLoader.cpp
#include <iostream>
#include <vector>

// TinyObjLoader.CC (OBJ) // https://github.com/tinyobjloader/tinyobjloader/blob/release/tiny_obj_loader.cc
#define TINYOBJLOADER_IMPLEMENTATION
#define TINYOBJLOADER_USE_STREAM
#include "tiny_obj_loader.h"

// Assimp (FBX)
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

using namespace tinyobj;

bool LoadObj(
    attrib_t* attrib,
    std::vector<shape_t>* shapes,
    std::vector<material_t>* materials,
    std::string& warn,
    std::string& err,
    const char* filename,
    const char* mtl_basedir = nullptr,
    bool triangulate = true
);

using namespace std;

bool ObjectLoader::LoadOBJ(const std::string& path, std::vector<float>& outVertices, glm::vec3& center) 
{
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;

    std::ifstream objFile(path);
    if (!objFile.is_open()) 
    {
        std::cerr << "Failed to open file: " << path << std::endl;
        return false;
    }
    
    bool ret = tinyobj::LoadObj(
        &attrib,
        &shapes,
        &materials,
        &warn,
        &err,
        path.c_str()
    );

    if (!warn.empty()) std::cout << "[tinyobj warning] " << warn << std::endl;
    if (!err.empty()) std::cerr << "[tinyobj error] " << err << std::endl;
    if (!ret) return false;

    glm::vec3 min(FLT_MAX), max(-FLT_MAX);

    for (const auto& shape : shapes) 
    {
        for (const auto& index : shape.mesh.indices) 
        {
            float vx = attrib.vertices[3 * index.vertex_index + 0];
            float vy = attrib.vertices[3 * index.vertex_index + 1];
            float vz = attrib.vertices[3 * index.vertex_index + 2];

            min.x = std::min(min.x, vx);
            min.y = std::min(min.y, vy);
            min.z = std::min(min.z, vz);

            max.x = std::max(max.x, vx);
            max.y = std::max(max.y, vy);
            max.z = std::max(max.z, vz);

            outVertices.push_back(vx);
            outVertices.push_back(vy);
            outVertices.push_back(vz);
        }
    }

    center = (min + max) * 0.5f;
    return true;
}



bool ObjectLoader::LoadFBX(const std::string& path, std::vector<float>& outVertices, glm::vec3& center) 
{
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(path, aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_FlipUVs);

    if (!scene || !scene->HasMeshes()) 
    {
        std::cerr << "[Assimp error] Failed to load: " << path << std::endl;
        return false;
    }

    const aiMesh* mesh = scene->mMeshes[0];

    glm::vec3 min(FLT_MAX), max(-FLT_MAX);

    for (unsigned int i = 0; i < mesh->mNumVertices; i++) 
    {
        aiVector3D pos = mesh->mVertices[i];

        min.x = std::min(min.x, pos.x);
        min.y = std::min(min.y, pos.y);
        min.z = std::min(min.z, pos.z);

        max.x = std::max(max.x, pos.x);
        max.y = std::max(max.y, pos.y);
        max.z = std::max(max.z, pos.z);

        outVertices.push_back(pos.x);
        outVertices.push_back(pos.y);
        outVertices.push_back(pos.z);
    }

    center = (min + max) * 0.5f;
    return true;
}
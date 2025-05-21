#include "ObjectLoader.h"

// ModelLoader.cpp
#include <iostream>
#include <vector>

// TinyObjLoader.CC (OBJ) // https://github.com/tinyobjloader/tinyobjloader/blob/release/tiny_obj_loader.cc
#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

// Assimp (FBX)
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

using namespace std;

bool ObjectLoader::LoadOBJ(const std::string& path, std::vector<float>& outVertices) {
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;

    std::ifstream objFile(path);
    if (!objFile.is_open()) {
        std::cerr << "Failed to open file: " << path << std::endl;
        return false;
    }

    bool ret = tinyobj::LoadObj(
        &attrib,
        &shapes,
        &materials,
        &warn,
        &objFile
    );
    
    if (!warn.empty()) std::cout << "[tinyobj warning] " << warn << std::endl;
    if (!err.empty()) std::cerr << "[tinyobj error] " << err << std::endl;
    if (!ret) return false;

    for (const auto& shape : shapes) {
        for (const auto& index : shape.mesh.indices) {
            float vx = attrib.vertices[3 * index.vertex_index + 0];
            float vy = attrib.vertices[3 * index.vertex_index + 1];
            float vz = attrib.vertices[3 * index.vertex_index + 2];
            outVertices.push_back(vx);
            outVertices.push_back(vy);
            outVertices.push_back(vz);
        }
    }
    return true;
}

bool ObjectLoader::LoadFBX(const std::string& path, std::vector<float>& outVertices) {
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(path, aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_FlipUVs);

    if (!scene || !scene->HasMeshes()) {
        std::cerr << "[Assimp error] Failed to load: " << path << std::endl;
        return false;
    }

    const aiMesh* mesh = scene->mMeshes[0];
    for (unsigned int i = 0; i < mesh->mNumVertices; i++) {
        aiVector3D pos = mesh->mVertices[i];
        outVertices.push_back(pos.x);
        outVertices.push_back(pos.y);
        outVertices.push_back(pos.z);
    }

    return true;
}

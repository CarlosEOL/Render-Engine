#include "ObjectLoader.h"

#include <cfloat>
#include <iostream>
#include <vector>
#include <glm/glm.hpp>

// TinyObjLoader.CC (OBJ) // https://github.com/tinyobjloader/tinyobjloader/blob/release/tiny_obj_loader.cc
#include <filesystem>
#include "tiny_obj_loader.h"

// Assimp (FBX)
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

bool ObjectLoader::LoadOBJ(const std::string& path, std::vector<float>& outVertices, glm::vec3& center) 
{
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;

    const std::string baseDir = std::filesystem::path(path).parent_path().string();
    bool ret = tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, path.c_str(), baseDir.c_str());

    if (!warn.empty()) std::cout << "[tinyobj warning] " << warn << std::endl;
    if (!err.empty()) std::cerr << "[tinyobj error] " << err << std::endl;
    if (!ret) return false;

    glm::vec3 min(FLT_MAX), max(-FLT_MAX);
    outVertices.clear();

    const auto validVertex = [&](int i) { return i >= 0 && 3 * static_cast<size_t>(i) + 2 < attrib.vertices.size(); };
    const auto validNormal = [&](int i) { return i >= 0 && 3 * static_cast<size_t>(i) + 2 < attrib.normals.size(); };

    for (const auto& shape : shapes)
    {
        // LoadObj triangulates, so every three indices form one face.
        const auto& indices = shape.mesh.indices;
        for (size_t first = 0; first + 2 < indices.size(); first += 3)
        {
            glm::vec3 corner[3];
            bool valid = true;
            for (int k = 0; k < 3; ++k)
            {
                const int v = indices[first + k].vertex_index;
                if (!validVertex(v))
                {
                    valid = false;
                    break;
                }
                corner[k] = glm::vec3(attrib.vertices[3 * v], attrib.vertices[3 * v + 1], attrib.vertices[3 * v + 2]);
            }
            if (!valid)
                continue;

            // Files without normals get a flat face normal; an all-zero normal turns into NaN when normalized in the shader.
            glm::vec3 flat = glm::cross(corner[1] - corner[0], corner[2] - corner[0]);
            flat = glm::length(flat) > 1e-20f ? glm::normalize(flat) : glm::vec3(0, 1, 0);

            for (int k = 0; k < 3; ++k)
            {
                const int n = indices[first + k].normal_index;
                glm::vec3 normal = flat;
                if (validNormal(n))
                    normal = glm::vec3(attrib.normals[3 * n], attrib.normals[3 * n + 1], attrib.normals[3 * n + 2]);
                if (glm::length(normal) < 1e-6f)
                    normal = flat;

                min = glm::min(min, corner[k]);
                max = glm::max(max, corner[k]);

                outVertices.insert(outVertices.end(), {corner[k].x, corner[k].y, corner[k].z, normal.x, normal.y, normal.z});
            }
        }
    }

    if (outVertices.empty())
    {
        std::cerr << "[OBJ error] No triangles in: " << path << std::endl;
        return false;
    }

    center = (min + max) * 0.5f;

    return true;
}



bool ObjectLoader::LoadFBX(const std::string& path, std::vector<float>& outVertices, glm::vec3& center) 
{
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile
    (
        path,
        aiProcess_Triangulate |
        aiProcess_GenSmoothNormals |
        aiProcess_JoinIdenticalVertices |
        aiProcess_CalcTangentSpace |
        aiProcess_FlipUVs
    );

    if (!scene || !scene->HasMeshes()) 
    {
        std::cerr << "[Assimp error] Failed to load: " << path << std::endl;
        return false;
    }

    const aiMesh* mesh = scene->mMeshes[0];

    glm::vec3 min(FLT_MAX), max(-FLT_MAX);

    for (unsigned int i = 0; i < mesh->mNumVertices; i++) 
    {
        const aiFace& face = mesh->mFaces[i];
        for (unsigned int j = 0; j < face.mNumIndices; ++j) {
            unsigned int index = face.mIndices[j];
            aiVector3D pos = mesh->mVertices[index];
            aiVector3D normal = mesh->HasNormals() ? mesh->mNormals[index] : aiVector3D(0, 1, 0);

            if (normal.Length() < 1e-3f)
                normal = aiVector3D(0, 1, 0);
            else
                normal = normal.NormalizeSafe(); // or manually normalize

            outVertices.push_back(pos.x);
            outVertices.push_back(pos.y);
            outVertices.push_back(pos.z);
            outVertices.push_back(normal.x);
            outVertices.push_back(normal.y);
            outVertices.push_back(normal.z);
        }
    }

    center = (min + max) * 0.5f;
    return true;
}
#include "ObjectLoader.h"

#include <iostream>
#include <vector>
#include <memory>
#include <functional>
#include <cmath>
#include <glm/common.hpp>

// TinyObjLoader.CC (OBJ) //
// https://github.com/tinyobjloader/tinyobjloader/blob/release/tiny_obj_loader.cc
#include <fstream>
#include "tiny_obj_loader.h"

// Assimp (FBX)
#include <assimp/cimport.h>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

bool ObjectLoader::LoadOBJ(const std::string &path,
                           std::vector<float> &outVertices, glm::vec3 &center) {
  tinyobj::attrib_t attrib;
  std::vector<tinyobj::shape_t> shapes;
  std::vector<tinyobj::material_t> materials;
  std::string warn, err;

  std::ifstream objFile(path);
  if (!objFile.is_open()) {
    std::cerr << "Failed to open file: " << path << std::endl;
    return false;
  }

  tinyobj::MaterialFileReader reader(".");

  bool ret =
      tinyobj::LoadObj(&attrib, &shapes, &materials, &err, &objFile, &reader);

  if (!warn.empty())
    std::cout << "[tinyobj warning] " << warn << std::endl;
  if (!err.empty())
    std::cerr << "[tinyobj error] " << err << std::endl;
  if (!ret)
    return false;

  glm::vec3 min(FLT_MAX), max(-FLT_MAX);

  for (const auto &shape : shapes) {
    for (const auto &index : shape.mesh.indices) {
      float vx = attrib.vertices[3 * index.vertex_index + 0];
      float vy = attrib.vertices[3 * index.vertex_index + 1];
      float vz = attrib.vertices[3 * index.vertex_index + 2];

      float nx = 0, ny = 0, nz = 0;
      if (index.normal_index >= 0) {
        nx = attrib.normals[3 * index.normal_index + 0];
        ny = attrib.normals[3 * index.normal_index + 1];
        nz = attrib.normals[3 * index.normal_index + 2];
      }

      min.x = std::min(min.x, vx);
      min.y = std::min(min.y, vy);
      min.z = std::min(min.z, vz);

      max.x = std::max(max.x, vx);
      max.y = std::max(max.y, vy);
      max.z = std::max(max.z, vz);

      outVertices.push_back(vx);
      outVertices.push_back(vy);
      outVertices.push_back(vz);
      outVertices.push_back(nx);
      outVertices.push_back(ny);
      outVertices.push_back(nz);
    }
  }

  center = (min + max) * 0.5f;

  return true;
}

bool ObjectLoader::LoadFBX(const std::string &path,
                           std::vector<float> &outVertices, glm::vec3 &center) {
  // The C API keeps the vendor DLL's C++ runtime/STL ABI private. This also
  // allows the optimized engine to use the installed Debug Assimp DLL safely.
  std::unique_ptr<const aiScene, decltype(&aiReleaseImport)> imported(
      aiImportFile(path.c_str(),
                   aiProcess_Triangulate | aiProcess_GenSmoothNormals |
                       aiProcess_JoinIdenticalVertices |
                       aiProcess_CalcTangentSpace | aiProcess_FlipUVs),
      &aiReleaseImport);
  const aiScene *scene = imported.get();

  if (!scene || !scene->HasMeshes() || !scene->mRootNode) {
    std::cerr << "[Assimp error] Failed to load: " << path << std::endl;
    return false;
  }

  glm::vec3 min(FLT_MAX), max(-FLT_MAX);
  outVertices.clear();
  std::function<void(const aiNode *, const aiMatrix4x4 &)> visit;
  visit = [&](const aiNode *node, const aiMatrix4x4 &parent) {
    const aiMatrix4x4 transform = parent * node->mTransformation;
    aiMatrix3x3 normalTransform(transform);
    normalTransform.Inverse().Transpose();
    for (unsigned int item = 0; item < node->mNumMeshes; ++item) {
      const aiMesh *mesh = scene->mMeshes[node->mMeshes[item]];
      // Iterate faces, not vertex count: the old loop could read past
      // mFaces whenever a mesh had more vertices than triangles.
      for (unsigned int faceIndex = 0; faceIndex < mesh->mNumFaces;
           ++faceIndex) {
        const auto &face = mesh->mFaces[faceIndex];
        if (face.mNumIndices != 3)
          continue;
        for (unsigned int j = 0; j < 3; ++j) {
          const auto index = face.mIndices[j];
          const auto p = transform * mesh->mVertices[index];
          auto n = mesh->HasNormals() ? normalTransform * mesh->mNormals[index]
                                      : aiVector3D(0, 1, 0);
          if (!std::isfinite(n.SquareLength()) || n.SquareLength() < 1e-12f)
            n = {0, 1, 0};
          else
            n.Normalize();
          min = glm::min(min, glm::vec3(p.x, p.y, p.z));
          max = glm::max(max, glm::vec3(p.x, p.y, p.z));
          outVertices.insert(outVertices.end(), {p.x, p.y, p.z, n.x, n.y, n.z});
        }
      }
    }
    for (unsigned int child = 0; child < node->mNumChildren; ++child)
      visit(node->mChildren[child], transform);
  };
  visit(scene->mRootNode, aiMatrix4x4());
  if (outVertices.empty())
    return false;
  center = (min + max) * 0.5f;
  return true;
}

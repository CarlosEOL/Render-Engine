#pragma once

#include "glad/glad.h"

class AssetManager
{
public:
    static GLuint LoadTexture(const char* filePath, bool srgb = false);
};

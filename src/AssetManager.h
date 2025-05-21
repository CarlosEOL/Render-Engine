#pragma once

#include "glad/glad.h"

class AssetManager
{
public:
    
    GLuint hiddenTex;
    GLuint rightTex;
    GLuint wrongTex;

    //Set up an instance, returns an address in memory
    static AssetManager& Get() {
        static AssetManager instance;
        return instance;
    }

private:
    AssetManager();
    GLuint LoadTexture(const char* filePath);
    ~AssetManager() = default;
};

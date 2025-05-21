#include "AssetManager.h"

#define STB_IMAGE_IMPLEMENTATION
#include <iostream>
#include "stb_image.h"

using namespace std;

GLuint AssetManager::LoadTexture(const char* filePath) {
    int width, height, nrChannels;
    stbi_set_flip_vertically_on_load(true); // Flip for OpenGL
    unsigned char* data = stbi_load(filePath, &width, &height, &nrChannels, 0);

    if (!data) {
        cerr << "Failed to load texture: " << filePath << std::endl;
        return 0;
    }

    GLenum format = GL_RGB;
    if (nrChannels == 4)
        format = GL_RGBA;

    GLuint textureID;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);

    // Upload pixel data to GPU
    glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);

    // Texture settings
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); // No repeating
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); // Smoother scaling
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    if (textureID == 0) {
        std::cerr << "Invalid texture loaded!" << std::endl;
    }
    
    stbi_image_free(data); // Free CPU memory
    return textureID;
}

AssetManager::AssetManager()
{
    hiddenTex = LoadTexture("assets/hidden.png");
    cout<<"Loaded Hidden."<<endl;
    rightTex = LoadTexture("assets/right.png");
    cout<<"Loaded Right."<<endl;
    wrongTex = LoadTexture("assets/wrong.png");
    cout<<"Loaded Wrong."<<endl;
}

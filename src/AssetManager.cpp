#include "AssetManager.h"

#define STB_IMAGE_IMPLEMENTATION
#include <iostream>
#include "stb_image.h"

using namespace std;

GLuint AssetManager::LoadTexture(const char* filePath, bool srgb) {
    int width, height, nrChannels;
    stbi_set_flip_vertically_on_load(true); // Flip for OpenGL
    // Always decode to RGBA so 1- and 2-channel images cannot be read as RGB past the end of the buffer.
    unsigned char* data = stbi_load(filePath, &width, &height, &nrChannels, 4);

    if (!data) {
        cerr << "Failed to load texture: " << filePath << std::endl;
        return 0;
    }

    GLuint textureID;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);

    // Rows are tightly packed whatever the width; the default alignment of 4 would skew odd widths.
    GLint previousAlignment = 4;
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &previousAlignment);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    // Upload pixel data to GPU
    glTexImage2D(GL_TEXTURE_2D, 0, srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);
    glPixelStorei(GL_UNPACK_ALIGNMENT, previousAlignment);

    // Texture settings
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); // No repeating
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR); // Uses the mipmaps generated above
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    if (textureID == 0) {
        std::cerr << "Invalid texture loaded!" << std::endl;
    }
    
    stbi_image_free(data); // Free CPU memory
    return textureID;
}

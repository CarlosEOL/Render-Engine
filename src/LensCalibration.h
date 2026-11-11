// SPDX-License-Identifier: CC-BY-SA-3.0
// Calibration coefficients extracted from Lensfun contributors' database.
// https://github.com/lensfun/lensfun/tree/master/data/db
// Retrieved 2026-11-10. This subset retains full-frame entries at fixed focal lengths.
// Sources: slr-canon.xml, slr-nikon.xml, slr-zeiss.xml.
// License: https://creativecommons.org/licenses/by-sa/3.0/
#pragma once
#include <glm/glm.hpp>

namespace LensCalibration {
    struct Profile {
        const char* name;
        float focalLength;
        int distortionModel; // 0 ideal, 1 poly3, 2 PTLens
        glm::vec3 distortion;
        glm::vec3 redTca; // b, c, v
        glm::vec3 blueTca;
        glm::vec3 vignetting;
    };
    inline const Profile profiles[] = {
        {"Free camera", 0, 0, {0,0,0}, {0,0,1}, {0,0,1}, {0,0,0}},
        {"Human vision approximation", 0, 0, {0,0,0}, {0,0,1}, {0,0,1}, {0,0,0}},
        {"Canon EF 50mm f/1.2L", 50, 2, {0.00476f,-0.01688f,0.00748f}, {0,0,1}, {0,0,1}, {0,0,0}},
        {"Nikkor 14-24mm at 14mm", 14, 1, {-0.01343f,0,0}, {0.0000628f,0,1.0001266f}, {-0.0000429f,0,1.0001134f}, {0,0,0}},
        {"ZEISS Otus 85mm f/1.4", 85, 1, {0.001793f,0,0}, {0,0,1}, {0,0,1}, {0,0,0}},
        {"Ideal 200mm telephoto", 200, 0, {0,0,0}, {0,0,1}, {0,0,1}, {0,0,0}},
        // Distagon vignetting measured at f/2, focus distance 1000 m.
        {"ZEISS Distagon 35mm f/2", 35, 2, {0.00786f,-0.02862f,0.01911f}, {0,0,1.0001515f}, {0,0,1.0001430f}, {-1.9515f,2.1666f,-0.9851f}}
    };
}

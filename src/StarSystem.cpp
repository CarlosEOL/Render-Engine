#include "StarSystem.h"
#include "Astronomy.h"
#include "PlanetTerrain.h"
#include "glad/glad.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iomanip>
#include <random>
#include <sstream>

unsigned int compileShaderProgram(const char *, const char *);

namespace
{
double Random(std::mt19937_64 &engine, double lower, double upper)
{
    return std::uniform_real_distribution<double>(lower, upper)(engine);
}

void AlignUp(glm::vec3 &front, glm::vec3 &up, const glm::vec3 &nextUp)
{
    const auto axis = glm::cross(up, nextUp);
    const float sine = glm::length(axis);
    if (sine > 0.000001f)
        front = glm::normalize(glm::vec3(
            glm::rotate(glm::mat4(1), std::atan2(sine, glm::dot(up, nextUp)), axis / sine) * glm::vec4(front, 0)));
    up = nextUp;
}

// cross(front, up) vanishes when the two are parallel, which would turn every direction built from it into NaN.
glm::vec3 RightOf(const glm::vec3 &front, const glm::vec3 &up)
{
    auto right = glm::cross(front, up);
    if (glm::length(right) < 1e-3f)
        right = glm::cross(front, std::abs(front.y) < .9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0));
    return glm::normalize(right);
}

// The atmosphere pass changes a pixel only when the ray through its centre meets the planet's shell, and it copies
// the whole framebuffer to find out. A shell that is off screen, behind the camera or smaller than the gap between
// pixel centres leaves the frame untouched, so such a planet can skip the pass. The shell is bounded conservatively:
// a point on a sphere of radius r at view depth d projects within r / (d - r) * (1 + |offset| / d) of the centre.
bool ShellReachesPixel(const glm::mat4 &projection, const glm::mat4 &view, const glm::vec3 &center, float radius,
                       int width, int height)
{
    const double shell = radius * 1.03; // the shader's shell is 1.025 times the radius
    const glm::dvec3 c = glm::dvec3(view * glm::vec4(center, 1));
    const double depth = -c.z;
    if (depth <= 2 * shell)
        return true;
    const double spread = shell / (depth - shell);
    const auto reaches = [&](double offset, double scale, int pixels) {
        const double middle = scale * offset / depth;
        const double half = scale * spread * (1 + std::abs(offset) / depth) * 1.01 + 0.1 / pixels; // 0.1 / pixels is 0.05 px
        // Pixel i is centred at NDC (i + 0.5) * 2 / pixels - 1.
        const double low = std::ceil((middle - half + 1) * 0.5 * pixels - 0.5);
        const double high = std::floor((middle + half + 1) * 0.5 * pixels - 0.5);
        return std::max(low, 0.0) <= std::min(high, pixels - 1.0);
    };
    return reaches(c.x, projection[0][0], width) && reaches(c.y, projection[1][1], height);
}
} // namespace

StarSystem::Star StarSystem::DescribeStar(int id, const std::string &name)
{
    std::mt19937_64 random(0x8c3c010cb4754c91ULL ^ static_cast<unsigned long long>(id) * 7919ULL);
    Star result;
    result.id = id;
    result.name = name;
    const double choice = Random(random, 0, 1);
    char spectral;
    if (choice < 0.55)
    {
        spectral = 'M';
        result.massSolar = Random(random, 0.10, 0.55);
        result.temperature = Random(random, 2700, 3900);
    }
    else if (choice < 0.78)
    {
        spectral = 'K';
        result.massSolar = Random(random, 0.55, 0.85);
        result.temperature = Random(random, 3900, 5200);
    }
    else if (choice < 0.90)
    {
        spectral = 'G';
        result.massSolar = Random(random, 0.85, 1.10);
        result.temperature = Random(random, 5200, 6000);
    }
    else if (choice < 0.96)
    {
        spectral = 'F';
        result.massSolar = Random(random, 1.10, 1.50);
        result.temperature = Random(random, 6000, 7500);
    }
    else if (choice < 0.985)
    {
        spectral = 'A';
        result.massSolar = Random(random, 1.50, 2.50);
        result.temperature = Random(random, 7500, 10000);
    }
    else if (choice < 0.998)
    {
        spectral = 'B';
        result.massSolar = Random(random, 2.50, 12.0);
        result.temperature = Random(random, 10000, 30000);
    }
    else
    {
        spectral = 'O';
        result.massSolar = Random(random, 15, 35);
        result.temperature = Random(random, 30000, 45000);
    }
    result.radiusSolar = std::pow(result.massSolar, 0.8);
    result.luminositySolar = result.radiusSolar * result.radiusSolar * std::pow(result.temperature / 5772.0, 4);
    result.ageGyr = Random(random, 0.05, 0.90) * std::min(13.7, 10.0 * result.massSolar / result.luminositySolar);
    result.classification = std::string(1, spectral) + std::to_string(id % 10) + " V main-sequence star";
    return result;
}

void StarSystem::Generate(int id, const std::string &name)
{
    solarSystem = false;
    asteroids.clear();
    orbitPaths.clear();
    star = DescribeStar(id, name);
    std::mt19937_64 random(0x4f6cdd1dULL + static_cast<unsigned long long>(id) * 104729ULL);
    const int count = 4 + static_cast<int>(random() % 7);
    planets.clear();
    double axis = std::max(star.radiusSolar * Astronomy::solarRadiusAu * 8,
                           std::sqrt(star.luminositySolar) * Random(random, 0.15, 0.30));
    for (int index = 0; index < count; ++index)
    {
        Planet planet;
        planet.name = name + " " + static_cast<char>('b' + index);
        const int category = index < 2 ? 0 : (index == count - 1 ? 2 : static_cast<int>(random() % 3));
        planet.gas = category > 0;
        if (category == 0)
        {
            planet.radiusEarth = Random(random, 0.35, 1.95);
            planet.massEarth = std::pow(planet.radiusEarth, 3) * Random(random, 0.7, 1.3);
            planet.classification = planet.radiusEarth > 1.3 ? "Super-Earth" : "Terrestrial planet";
        }
        else if (category == 1)
        {
            planet.radiusEarth = Random(random, 2.0, 4.5);
            planet.massEarth = Random(random, 7, 35);
            planet.classification = "Ice giant";
        }
        else
        {
            planet.radiusEarth = Random(random, 7, 12);
            planet.massEarth = Random(random, 60, 900);
            planet.classification = "Gas giant";
        }
        planet.orbit = {axis, Random(random, 0.0, 0.12), Random(random, -0.12, 0.12),
                        Random(random, 0, glm::two_pi<double>()), Random(random, 0, glm::two_pi<double>())};
        const double albedo = Random(random, 0.1, 0.65);
        planet.temperature = 278.5 * std::pow(star.luminositySolar * (1.0 - albedo), 0.25) / std::sqrt(axis);
        planet.daySeconds = Random(random, 7, 70) * 3600;
        planet.tilt = static_cast<float>(Random(random, -0.7, 0.7));
        planet.seed = static_cast<unsigned int>(random() % 10000);
        planet.seaLevel = planet.temperature > 160 && planet.temperature < 420
                              ? static_cast<float>(Random(random, -0.18, 0.22))
                              : -1.0f;
        planet.atmosphere = planet.gas || random() % 4 != 0;
        planet.color = glm::vec3(Random(random, .12, .65), Random(random, .15, .65), Random(random, .12, .65));
        planets.push_back(planet);
        axis *= Random(random, 1.5, 2.05);
    }
    selected = -1;
    landed = following = false;
    lastTime = 0;
    camera = glm::dvec3(0, 0, std::max(0.1, star.radiusSolar * Astronomy::solarRadiusAu * 8));
    speedAu = 0.1;
}

glm::dvec3 StarSystem::Position(int planet, double seconds) const
{
    if (planet < 0)
        return glm::dvec3(0);
    const auto relative = RelativePosition(planet, seconds);
    return planets[planet].parent < 0 ? relative : Position(planets[planet].parent, seconds) + relative;
}

double StarSystem::GravityFor(int planet) const
{
    return Astronomy::solarGravityAu * (planets[planet].parent < 0
                                            ? star.massSolar
                                            : planets[planets[planet].parent].massEarth * 5.97217e24 / 1.98847e30);
}

glm::dvec3 StarSystem::RelativePosition(int planet, double seconds) const
{
    const auto p = OrbitalMechanics::PositionAtTimeDouble(planets[planet].orbit, GravityFor(planet), seconds);
    return {p.x, p.z, -p.y}; // Ecliptic north is the camera's +Y axis.
}

glm::mat4 StarSystem::Rotation(int planet, double seconds) const
{
    const auto &body = planets[planet];
    auto rotation = glm::rotate(glm::mat4(1), body.tilt, glm::vec3(1, 0, 0));
    return glm::rotate(rotation,
                       static_cast<float>(std::remainder(seconds / body.daySeconds, 1.0) * glm::two_pi<double>()),
                       glm::vec3(0, 1, 0));
}

double StarSystem::SurfaceRadius(int planet, const glm::dvec3 &direction, double seconds) const
{
    const auto &body = planets[planet];
    const auto local =
        glm::normalize(glm::vec3(glm::transpose(Rotation(planet, seconds)) * glm::vec4(glm::vec3(direction), 0)));
    const double radius = body.radiusEarth * Astronomy::earthRadiusAu;
    if (body.gas)
        return radius;
    return radius * PlanetTerrain::SurfaceRadius(
                        local, body.seed, static_cast<float>(std::remainder(seconds, glm::two_pi<double>())),
                        body.seaLevel, 0.003f, static_cast<float>(2.0 / (body.radiusEarth * 6371000.0)));
}

void StarSystem::ConstrainSurface(double seconds, glm::vec3 &front, glm::vec3 &up)
{
    if (landedPlanet < 0)
        return;
    const auto center = Position(landedPlanet, seconds);
    const auto offset = camera - center;
    const auto radial = glm::length(offset) > 0 ? glm::normalize(offset) : glm::dvec3(up);
    AlignUp(front, up, glm::vec3(radial));
    camera = center + radial * (SurfaceRadius(landedPlanet, radial, seconds) + 2.0 / Astronomy::metresPerAu);
}

void StarSystem::StoreLandedDirection(double seconds)
{
    const auto spin = glm::dmat3(glm::mat3(Rotation(landedPlanet, seconds)));
    landedDirection = glm::normalize(glm::transpose(spin) * (camera - Position(landedPlanet, seconds)));
}

void StarSystem::Update(double seconds, glm::vec3 &front, glm::vec3 &up)
{
    if (landed)
    {
        const auto rotation = Rotation(landedPlanet, seconds) * glm::transpose(Rotation(landedPlanet, lastTime));
        // Rebuilt from the stored planet-frame direction every frame: carrying camera through float matrices
        // frame after frame let it drift along the surface by metres at Earth scale. This is the same matrix the
        // planet is drawn with, so the ground stays fixed relative to the camera.
        camera = Position(landedPlanet, seconds) +
                 glm::dmat3(glm::mat3(Rotation(landedPlanet, seconds))) * landedDirection;
        front = glm::normalize(glm::vec3(rotation * glm::vec4(front, 0)));
        up = glm::normalize(glm::vec3(rotation * glm::vec4(up, 0)));
        ConstrainSurface(seconds, front, up);
    }
    else if (following)
    {
        const auto position = Position(selected, seconds);
        camera += position - lastFollowPosition;
        lastFollowPosition = position;
    }
    lastTime = seconds;
}

void StarSystem::Move(GLFWwindow *window, float realSeconds, glm::vec3 &front, glm::vec3 &up)
{
    double distance = speedAu * realSeconds;
    if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)
        distance *= 5;
    const auto right = RightOf(front, up);
    glm::dvec3 movement(0);
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
        movement += glm::dvec3(front);
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
        movement -= glm::dvec3(front);
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
        movement += glm::dvec3(right);
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
        movement -= glm::dvec3(right);
    if (!landed)
    {
        if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS)
            movement += glm::dvec3(up);
        if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS)
            movement -= glm::dvec3(up);
    }
    camera += distance * movement;
    if (landed)
    {
        ConstrainSurface(lastTime, front, up);
        StoreLandedDirection(lastTime);
    }
    else
    {
        for (int planet = 0; planet < static_cast<int>(planets.size()); ++planet)
        {
            const auto delta = camera - Position(planet, lastTime);
            const double radius = planets[planet].radiusEarth * Astronomy::earthRadiusAu;
            if (glm::length(delta) < radius * 1.01)
            {
                const auto radial = glm::length(delta) > 0 ? glm::normalize(delta) : glm::dvec3(0, 0, 1);
                camera = Position(planet, lastTime) +
                         radial * (SurfaceRadius(planet, radial, lastTime) + 2.0 / Astronomy::metresPerAu);
            }
        }
    }
}

void StarSystem::GoToSelected(double seconds, glm::vec3 &front, glm::vec3 &up)
{
    landed = following = false;
    const auto target = Position(selected, seconds);
    const double radius = selected < 0 ? star.radiusSolar * Astronomy::solarRadiusAu
                                       : planets[selected].radiusEarth * Astronomy::earthRadiusAu;
    auto offset = camera - target;
    if (glm::length(offset) < radius)
        offset = glm::dvec3(0, 0, 1);
    camera = target + glm::normalize(offset) * radius * 5.0;
    front = glm::normalize(glm::vec3(target - camera));
    // Looking straight along the pole would leave front parallel to up and the view matrix undefined.
    up = std::abs(front.y) > .9999f ? glm::vec3(0, 0, -1) : glm::vec3(0, 1, 0);
    speedAu = radius * 0.5;
    lastTime = seconds;
}

void StarSystem::Overview(glm::vec3 &front, glm::vec3 &up)
{
    landed = following = false;
    double outer = 0;
    for (const auto &body : planets)
        if (body.parent < 0)
            outer = std::max(outer, body.orbit.semiMajorAxis * (1 + body.orbit.eccentricity));
    camera = glm::dvec3(0, outer * 1.4, outer * 2.0);
    front = glm::normalize(glm::vec3(-camera));
    up = glm::vec3(0, 1, 0);
    speedAu = outer * 0.3;
}

void StarSystem::ToggleLanding(double seconds, glm::vec3 &front, glm::vec3 &up)
{
    if (landed)
    {
        landed = false;
        camera += glm::dvec3(up) * 1000.0 / Astronomy::metresPerAu;
        return;
    }
    if (selected < 0 || planets[selected].gas)
        return;
    landedPlanet = selected;
    landed = true;
    following = false;
    ConstrainSurface(seconds, front, up);
    StoreLandedDirection(seconds);
    auto horizon = glm::vec3(-Position(selected, seconds));
    horizon -= up * glm::dot(horizon, up);
    if (glm::length(horizon) < .000001f)
        horizon = glm::cross(up, std::abs(up.y) < .9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0));
    front = glm::normalize(horizon);
    speedAu = 2.0 / Astronomy::metresPerAu;
    lastTime = seconds;
}

void StarSystem::ToggleFollow(double seconds)
{
    following = !following;
    landed = false;
    lastFollowPosition = Position(selected, seconds);
}

void StarSystem::Pick(const glm::vec3 &direction, double seconds)
{
    double best = 1;
    int picked = selected;
    for (int body = -1; body < static_cast<int>(planets.size()); ++body)
    {
        const auto offset = Position(body, seconds) - camera;
        const double radius = body < 0 ? star.radiusSolar * Astronomy::solarRadiusAu
                                       : planets[body].radiusEarth * Astronomy::earthRadiusAu;
        const double distance = glm::length(offset);
        const double along = glm::dot(offset, glm::dvec3(direction));
        if (along <= 0 || distance == 0)
            continue;
        const double score =
            glm::length(offset - along * glm::dvec3(direction)) / distance / std::max(0.012, radius / distance);
        if (score < best)
        {
            best = score;
            picked = body;
        }
    }
    if (picked != selected)
    {
        selected = picked;
        // lastFollowPosition belongs to the old selection; keeping it would move the camera by the distance between the two bodies.
        following = false;
    }
}

std::string StarSystem::Information(double seconds) const
{
    std::ostringstream text;
    text << star.name << "\n"
         << star.classification << "\n\n"
         << std::fixed << std::setprecision(3) << "Age  " << star.ageGyr << " billion years\n"
         << "Mass  " << star.massSolar << " solar\nRadius  " << star.radiusSolar << " solar\n"
         << std::setprecision(0) << "Temperature  " << star.temperature << " K\n"
         << "Planets  " << planets.size() << "\n";
    if (selected >= 0)
    {
        const auto &body = planets[selected];
        text << "\n"
             << body.name << "\n"
             << body.classification << "\n"
             << std::setprecision(2) << "Radius  " << body.radiusEarth << " Earth\n"
             << "Mass  " << body.massEarth << " Earth\n"
             << std::setprecision(3) << "Orbit  " << body.orbit.semiMajorAxis << " AU\n"
             << std::setprecision(1) << "Period  "
             << glm::two_pi<double>() / OrbitalMechanics::MeanMotion(GravityFor(selected), body.orbit.semiMajorAxis) /
                    86400
             << " days\n"
             << "Temperature  " << body.temperature << " K\n";
    }
    text << "\nF2  Orbit paths\nF3  Information panel\nF4  System overview\nF  Follow selection\nG  Go to selection\nL "
            " Land / lift off\n"
         << (landed      ? "\nStanding on surface"
             : following ? "\nFollowing selection"
                         : "\nFree camera");
    return text.str();
}

bool StarSystem::Initialize()
{
    const std::string vertex = std::string("#version 330 core\n") + PlanetTerrain::ShaderFunctions() + R"(
layout(location=0) in vec3 position;
uniform mat4 model;
uniform mat4 viewProjection;
uniform int bodyKind;
uniform float phase;
uniform bool patchDrawing;
uniform mat3 patchFrame;
out vec3 localPosition;
out vec3 relativePosition;
void main() {
    // The surface patch is stored around +Z and turned to face the camera here instead of being rebuilt on the CPU.
    vec3 local = bodyKind == 1 && patchDrawing ? patchFrame * position : position;
    localPosition = local;
    if (bodyKind == 4) {
        relativePosition = vec3(0);
        gl_Position = vec4(position.xy,0,1);
        return;
    }
    vec3 displaced = local;
    if (bodyKind == 1)
        displaced = planetSurface(normalize(local), terrainHeight(normalize(local)), phase);
    relativePosition = (model * vec4(displaced,1)).xyz;
    gl_Position = viewProjection * vec4(relativePosition,1);
}
)";
    const std::string fragment =
        std::string("#version 330 core\nconst float horizonRadius=0.0; const vec3 observerBeta=vec3(0);\n") +
        PlanetTerrain::ShaderFunctions() + OrbitalMechanics::RelativityShaderFunctions() + R"(
in vec3 localPosition;
in vec3 relativePosition;
out vec4 color;
uniform int bodyKind;
uniform mat3 normalMatrix;
uniform vec3 starPosition;
uniform vec3 albedo;
uniform float temperature;
uniform float phase;
uniform bool patchEnabled;
uniform bool patchDrawing;
uniform vec3 patchCenter;
void main() {
    if (bodyKind == 4) {
        color=vec4(albedo,1); gl_FragDepth=0; return;
    }
    gl_FragDepth=log2(1.0+length(relativePosition)*1e12)/log2(1.0+20000.0*1e12);
    if (bodyKind == 3) { color=vec4(albedo,1); return; }
    vec3 radial=normalize(localPosition);
    if (patchEnabled) {
        bool inside=dot(radial,patchCenter)>cos(0.020);
        if (inside != patchDrawing) discard;
    }
    if (bodyKind == 0) {
        float granules=0.94+0.06*terrainNoise(radial*75.0);
        color=vec4(blackbody(temperature)*granules,1);
        return;
    }
    vec3 normal=radial;
    vec3 material=albedo;
    float water=0;
    if (bodyKind == 1) {
        float height=terrainHeight(radial);
        water=1.0-smoothstep(seaLevel-.008,seaLevel+.008,height);
        vec3 surface=planetSurface(radial,height,phase);
        normal=normalize(cross(dFdx(surface),dFdy(surface)));
        if (dot(normal,radial)<0) normal=-normal;
        float moisture=.5+.5*terrainNoise(radial*5.0+vec3(31));
        material=mix(albedo*vec3(1.3,.9,.5),albedo*vec3(.45,1.1,.55),moisture);
        material=mix(vec3(.55,.46,.31),material,smoothstep(seaLevel,seaLevel+.04,height));
        material=mix(material,vec3(.34,.32,.30),smoothstep(.15,.35,height));
        material=mix(material,vec3(.9),smoothstep(.25,.45,height+.15*pow(abs(radial.y),4.0)));
        material*=.80+.35*(.5+.5*terrainNoise(radial*400.0));
        vec3 ocean=mix(vec3(.02,.30,.36),vec3(.003,.015,.05),
            1.0-exp(-15.0*max(seaLevel-height,0.0)));
        material=mix(material,ocean,water);
    } else {
        float bands=.5+.5*sin(70.0*radial.y+7.0*terrainFbm(radial*5.0));
        material=mix(albedo*.4,min(vec3(1),albedo*1.5+vec3(.15)),bands);
    }
    normal=normalize(normalMatrix*normal);
    vec3 light=normalize(starPosition-relativePosition);
    vec3 view=normalize(-relativePosition);
    float diffuse=max(dot(normal,light),0.0);
    float specular=water*pow(max(dot(normal,normalize(light+view)),0.0),100.0);
    color=vec4(material*(.012+diffuse)+specular,1);
}
)";
    program = compileShaderProgram(vertex.c_str(), fragment.c_str());
    int linked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked)
    {
        Release();
        return false;
    }
    const auto location = [&](const char *name) { return glGetUniformLocation(program, name); };
    uniform = {location("viewProjection"), location("starPosition"), location("phase"),
               location("model"),          location("normalMatrix"), location("bodyKind"),
               location("terrainSeed"),    location("seaLevel"),     location("terrainRelief"),
               location("waterWaveHeight"), location("albedo"),      location("temperature"),
               location("patchEnabled"),   location("patchDrawing"), location("patchCenter"),
               location("patchFrame")};
    glUseProgram(program);
    glUniform1f(uniform.terrainRelief, .003f);
    const auto upload = [](unsigned int &vao, unsigned int &buffer, const std::vector<glm::vec3> &vertices) {
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &buffer);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, buffer);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(glm::vec3), vertices.data(), GL_DYNAMIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), nullptr);
        glEnableVertexAttribArray(0);
    };
    const auto sphere = PlanetTerrain::SphereVertices(64, 128);
    sphereCount = static_cast<int>(sphere.size());
    upload(sphereVao, sphereBuffer, sphere);
    upload(lineVao, lineBuffer, {{0, 0, 0}});
    // A patch of the sphere around +Z; the vertex shader rotates it to wherever the camera is.
    constexpr int segments = 96;
    const auto point = [&](int x, int y) {
        return glm::normalize(glm::vec3(.025f * (2.0f * x / segments - 1.0f), .025f * (2.0f * y / segments - 1.0f), 1.0f));
    };
    std::vector<glm::vec3> patch;
    patch.reserve(segments * segments * 6);
    for (int row = 0; row < segments; ++row)
        for (int col = 0; col < segments; ++col)
            patch.insert(patch.end(), {point(col, row), point(col + 1, row), point(col + 1, row + 1), point(col, row),
                                       point(col + 1, row + 1), point(col, row + 1)});
    patchCount = static_cast<int>(patch.size());
    upload(patchVao, patchBuffer, patch);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    return atmosphere.Initialize();
}

void StarSystem::BuildOrbitPaths()
{
    orbitPaths.clear();
    orbitPaths.reserve(planets.size() * orbitSamples);
    for (int planet = 0; planet < static_cast<int>(planets.size()); ++planet)
    {
        const double period = glm::two_pi<double>() /
                              OrbitalMechanics::MeanMotion(GravityFor(planet), planets[planet].orbit.semiMajorAxis);
        for (int point = 0; point < orbitSamples; ++point)
            orbitPaths.push_back(RelativePosition(planet, period * point / (orbitSamples - 1)));
    }
}

void StarSystem::Render(float fieldOfView, float aspect, const glm::vec3 &front, const glm::vec3 &up, double seconds,
                        bool trajectories, bool photonView)
{
    if (!program || planets.empty())
        return;
    const int count = static_cast<int>(planets.size());
    // Each Position() is a Kepler solve plus its parent's, and the passes below need every body several times
    // over, so solve once per frame. A moon always comes after its parent in planets.
    positions.resize(count);
    for (int planet = 0; planet < count; ++planet)
    {
        const int parent = planets[planet].parent;
        positions[planet] = (parent < 0 ? glm::dvec3(0) : positions[parent]) + RelativePosition(planet, seconds);
    }
    const auto bodyPosition = [&](int body) { return body < 0 ? glm::dvec3(0) : positions[body]; };
    double clearance = glm::length(camera) - star.radiusSolar * Astronomy::solarRadiusAu;
    for (int planet = 0; planet < count; ++planet)
    {
        const auto delta = camera - positions[planet];
        const double distance = glm::length(delta);
        if (distance > 0)
            clearance = std::min(clearance, distance - SurfaceRadius(planet, delta / distance, seconds));
    }
    if (landed)
        clearance = 1.0 / Astronomy::metresPerAu;
    const auto projection = glm::perspective(glm::radians(fieldOfView), aspect,
                                             static_cast<float>(std::clamp(clearance * .01, 1e-13, 1e-3)), 20000.0f);
    const auto view = glm::lookAt(glm::vec3(0), front, up);
    const auto vp = projection * view;
    glClear(GL_DEPTH_BUFFER_BIT);
    glUseProgram(program);
    glUniformMatrix4fv(uniform.viewProjection, 1, GL_FALSE, glm::value_ptr(vp));
    glUniform3fv(uniform.starPosition, 1, glm::value_ptr(glm::vec3(-camera)));
    glUniform1f(uniform.phase, static_cast<float>(std::remainder(seconds, glm::two_pi<double>())));
    const auto setModel = [&](const glm::mat4 &model) {
        glUniformMatrix4fv(uniform.model, 1, GL_FALSE, glm::value_ptr(model));
    };
    auto model = glm::translate(glm::mat4(1), glm::vec3(-camera));
    model = glm::scale(model, glm::vec3(star.radiusSolar * Astronomy::solarRadiusAu));
    setModel(model);
    glUniform1i(uniform.bodyKind, 0);
    glUniform1i(uniform.patchEnabled, false);
    glUniform1f(uniform.terrainSeed, static_cast<float>(star.id % 10000));
    glUniform1f(uniform.temperature, static_cast<float>(star.temperature));
    glBindVertexArray(sphereVao);
    glDrawArrays(GL_TRIANGLES, 0, sphereCount);
    for (int planet = 0; planet < count; ++planet)
    {
        const auto &body = planets[planet];
        const auto &center = positions[planet];
        const double radius = body.radiusEarth * Astronomy::earthRadiusAu;
        const auto rotation = Rotation(planet, seconds);
        model = glm::translate(glm::mat4(1), glm::vec3(center - camera)) * rotation;
        model = glm::scale(model, glm::vec3(radius));
        setModel(model);
        // The model is a rotation and a uniform scale, so its normal matrix is the rotation alone.
        glUniformMatrix3fv(uniform.normalMatrix, 1, GL_FALSE, glm::value_ptr(glm::mat3(rotation)));
        glUniform1i(uniform.bodyKind, body.gas ? 2 : 1);
        glUniform1f(uniform.terrainSeed, static_cast<float>(body.seed));
        glUniform1f(uniform.seaLevel, body.seaLevel);
        glUniform1f(uniform.waterWaveHeight, static_cast<float>(2.0 / (body.radiusEarth * 6371000.0)));
        glUniform3fv(uniform.albedo, 1, glm::value_ptr(body.color));
        const bool patch = !body.gas && glm::length(camera - center) < radius * 1.15;
        glUniform1i(uniform.patchEnabled, patch);
        glUniform1i(uniform.patchDrawing, false);
        if (patch)
        {
            const auto direction =
                glm::normalize(glm::vec3(glm::transpose(rotation) * glm::vec4(glm::vec3(camera - center), 0)));
            const auto reference = std::abs(direction.y) < .9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
            const auto right = glm::normalize(glm::cross(reference, direction));
            glUniform3fv(uniform.patchCenter, 1, glm::value_ptr(direction));
            glUniformMatrix3fv(uniform.patchFrame, 1, GL_FALSE,
                               glm::value_ptr(glm::mat3(right, glm::cross(direction, right), direction)));
        }
        glBindVertexArray(sphereVao);
        glDrawArrays(GL_TRIANGLES, 0, sphereCount);
        if (patch)
        {
            glUniform1i(uniform.patchDrawing, true);
            glBindVertexArray(patchVao);
            glDrawArrays(GL_TRIANGLES, 0, patchCount);
        }
    }
    glUniform1i(uniform.patchEnabled, false);
    glUniform1i(uniform.bodyKind, 3);
    setModel(glm::mat4(1));

    // Every point and line of the frame goes into one buffer upload. Orbits do not depend on time, so their
    // polylines are built once and only translated by the parent's position and the camera here.
    if (trajectories && orbitPaths.empty())
        BuildOrbitPaths();
    lineVertices.clear();
    const auto mark = [&] { return static_cast<int>(lineVertices.size()); };
    const int markerFirst = mark();
    for (int body = -1; body < count; ++body)
        lineVertices.push_back(glm::vec3(bodyPosition(body) - camera));
    const int orbitFirst = mark();
    if (trajectories)
        for (int planet = 0; planet < count; ++planet)
        {
            const auto parent = bodyPosition(planets[planet].parent);
            for (int point = 0; point < orbitSamples; ++point)
                lineVertices.push_back(glm::vec3(orbitPaths[planet * orbitSamples + point] + parent - camera));
        }
    const int asteroidFirst = mark();
    for (const auto &orbit : asteroids)
    {
        const auto p = OrbitalMechanics::PositionAtTimeDouble(orbit, Astronomy::solarGravityAu, seconds);
        lineVertices.push_back(glm::vec3(glm::dvec3(p.x, p.z, -p.y) - camera));
    }
    const int photonFirst = mark();
    if (photonView)
        for (int body = -1; body < count; ++body)
        {
            const auto source = bodyPosition(body);
            const double duration = std::max(.001, glm::length(source - camera) / Astronomy::lightSpeedAu);
            for (int point = 0; point < 8; ++point)
            {
                const double fraction = std::fmod(seconds / duration + point / 8.0, 1.0);
                lineVertices.push_back(glm::vec3(glm::mix(source, camera, fraction) - camera));
            }
        }
    const int crosshairFirst = mark();
    const auto clip = vp * glm::vec4(glm::vec3(bodyPosition(selected) - camera), 1);
    const auto crosshairAt = clip.w > 0 ? glm::vec2(clip) / clip.w : glm::vec2(2);
    const bool crosshair = std::abs(crosshairAt.x) < 1 && std::abs(crosshairAt.y) < 1;
    if (crosshair)
        for (int sign : {-1, 1})
        {
            lineVertices.push_back({crosshairAt.x + sign * .006f, crosshairAt.y, 0});
            lineVertices.push_back({crosshairAt.x + sign * .020f, crosshairAt.y, 0});
            lineVertices.push_back({crosshairAt.x, crosshairAt.y + sign * .006f * aspect, 0});
            lineVertices.push_back({crosshairAt.x, crosshairAt.y + sign * .020f * aspect, 0});
        }
    glBindVertexArray(lineVao);
    glBindBuffer(GL_ARRAY_BUFFER, lineBuffer);
    const size_t bytes = lineVertices.size() * sizeof(glm::vec3);
    if (bytes > lineCapacity)
        lineCapacity = bytes + bytes / 2;
    // Re-specifying the storage lets the driver hand out fresh memory instead of waiting for last frame's draws.
    glBufferData(GL_ARRAY_BUFFER, lineCapacity, nullptr, GL_DYNAMIC_DRAW);
    glBufferSubData(GL_ARRAY_BUFFER, 0, bytes, lineVertices.data());
    const auto draw = [&](GLenum mode, int first, int vertices, const glm::vec3 &color, float size) {
        glUniform3fv(uniform.albedo, 1, glm::value_ptr(color));
        glPointSize(size);
        glDrawArrays(mode, first, vertices);
    };
    draw(GL_POINTS, markerFirst, orbitFirst - markerFirst, glm::vec3(.15f, .45f, .65f), 3);
    if (trajectories)
        for (int planet = 0; planet < count; ++planet)
            draw(GL_LINE_STRIP, orbitFirst + planet * orbitSamples, orbitSamples, glm::vec3(.035f, .12f, .18f), 1);
    if (!asteroids.empty())
        draw(GL_POINTS, asteroidFirst, photonFirst - asteroidFirst, glm::vec3(.13f, .10f, .07f), 1);
    if (photonView)
        draw(GL_POINTS, photonFirst, crosshairFirst - photonFirst, glm::vec3(.3f, .7f, 1), 3);
    glBindVertexArray(0);
    int viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);
    for (int planet = 0; planet < count; ++planet)
    {
        if (!planets[planet].atmosphere)
            continue;
        const auto center = glm::vec3(positions[planet] - camera);
        const auto radius = static_cast<float>(planets[planet].radiusEarth * Astronomy::earthRadiusAu);
        if (ShellReachesPixel(projection, view, center, radius, viewport[2], viewport[3]))
            atmosphere.Render(vp, glm::vec3(0), center, radius, true, glm::vec3(-camera));
    }
    if (crosshair)
    {
        glUseProgram(program);
        glUniform1i(uniform.bodyKind, 4);
        glDisable(GL_DEPTH_TEST);
        glBindVertexArray(lineVao);
        glBindBuffer(GL_ARRAY_BUFFER, lineBuffer);
        draw(GL_LINES, crosshairFirst, mark() - crosshairFirst, glm::vec3(.2f, 1, .75f), 1);
        glEnable(GL_DEPTH_TEST);
    }
    glBindVertexArray(0);
}

void StarSystem::Release()
{
    atmosphere.Release();
    if (program)
        glDeleteProgram(program);
    for (auto vao : {sphereVao, lineVao, patchVao})
        if (vao)
            glDeleteVertexArrays(1, &vao);
    for (auto buffer : {sphereBuffer, lineBuffer, patchBuffer})
        if (buffer)
            glDeleteBuffers(1, &buffer);
    program = sphereVao = sphereBuffer = lineVao = lineBuffer = patchVao = patchBuffer = 0;
    sphereCount = patchCount = 0;
    lineCapacity = 0;
}

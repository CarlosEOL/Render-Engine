#include "QuantumParticleView.h"
#include "glad/glad.h"
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <random>

unsigned int compileShaderProgram(const char *, const char *);

bool QuantumParticleView::Initialize()
{
    const char *vertex = R"(#version 330 core
layout(location=0) in vec3 position;
layout(location=1) in float phase;
uniform mat4 viewProjection;
uniform float globalPhase;
out float angle;
void main() {
    angle=phase+globalPhase;
    gl_Position=viewProjection*vec4(position,1);
})";
    const char *fragment = R"(#version 330 core
in float angle;
out vec4 color;
uniform bool points;
uniform vec3 lineColor;
void main() {
    if (!points) {color=vec4(lineColor,1);return;}
    float radius=length(gl_PointCoord-.5)*2;
    if(radius>1)discard;
    vec3 tint=.55+.45*cos(angle+vec3(0,2.094,4.189));
    color=vec4(tint,.28*exp(-radius*radius*3));
})";
    program = compileShaderProgram(vertex, fragment);
    int linked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked)
    {
        Release();
        return false;
    }
    const auto create = [&](unsigned int &vao, unsigned int &buffer) {
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &buffer);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, buffer);
        glBufferData(GL_ARRAY_BUFFER, sizeof(Point), nullptr, GL_DYNAMIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Point), nullptr);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, sizeof(Point), reinterpret_cast<void *>(sizeof(glm::vec3)));
        glEnableVertexAttribArray(1);
    };
    create(pointVao, pointBuffer);
    create(lineVao, lineBuffer);
    glBindVertexArray(0);
    return true;
}

void QuantumParticleView::Update(const QuantumStates::Example &example, double time,
                                 const std::vector<std::complex<double>> *numericalWave)
{
    if (!program)
        return;
    using Family = QuantumStates::Family;
    if (example.family == Family::Hydrogen && cachedOrbital == example.state)
        return;
    points.clear();
    std::mt19937_64 engine(0x9ca137 + example.state * 137);
    std::uniform_real_distribution<double> unit(0, 1);
    std::normal_distribution<double> normal(0, 1);
    if (example.family == Family::Hydrogen)
    {
        cachedOrbital = example.state;
        glm::dvec3 position(.7, .3, .2);
        auto density = [&](const glm::dvec3 &p) {
            return std::norm(QuantumStates::Hydrogen3D(example.state, p.x, p.y, p.z, 0));
        };
        double probability = density(position);
        const double step = example.state == 0 ? .8 : example.state < 4 ? 2 : 4;
        // Metropolis sampling of |psi|^2 in three spatial dimensions; samples have no trajectory identity.
        for (int iteration = 0; iteration < 3000 + 18000 * 6; ++iteration)
        {
            const auto proposal = position + step * glm::dvec3(normal(engine), normal(engine), normal(engine));
            const double next = density(proposal);
            if (unit(engine) * probability < next)
            {
                position = proposal;
                probability = next;
            }
            if (iteration >= 3000 && (iteration - 3000) % 6 == 0)
            {
                const auto psi = QuantumStates::Hydrogen3D(example.state, position.x, position.y, position.z, 0);
                // Display axes: atomic z is the vertical camera Y; physical scales remain equal.
                points.push_back({glm::vec3(position.x, position.z, -position.y), static_cast<float>(std::arg(psi))});
            }
        }
    }
    else
    {
        cachedOrbital = -1;
        if (example.family == Family::Spin)
        {
            // The Bloch vector is a state-space direction, not a particle position.
        }
        else if (example.family == Family::Bell)
        {
            const auto probabilities = QuantumStates::Probabilities(example, time);
            for (int basis = 0; basis < 4; ++basis)
            {
                const int count = static_cast<int>(probabilities[basis] * 14000);
                for (int point = 0; point < count; ++point)
                    points.push_back(
                        {glm::vec3(-6 + basis * 4 + .45 * normal(engine), .45 * normal(engine), .45 * normal(engine)),
                         static_cast<float>(example.state == 1 && basis == 2 ? 3.141592653589793 : 0)});
            }
        }
        else
        {
            constexpr int bins = QuantumStates::Solver::size;
            std::vector<std::complex<double>> wave(bins);
            std::vector<double> cdf(bins);
            double integral = 0;
            for (int bin = 0; bin < bins; ++bin)
            {
                const double x = -12 + bin * QuantumStates::Solver::dx;
                wave[bin] = numericalWave ? (*numericalWave)[bin] : QuantumStates::Evaluate(example, x, 0, time);
                integral += std::norm(wave[bin]);
                cdf[bin] = integral;
            }
            const int visiblePoints=static_cast<int>(14000*std::clamp(integral*QuantumStates::Solver::dx,0.0,1.0));
            if (integral > 1e-12)
                for (int point = 0; point < visiblePoints; ++point)
                {
                    const auto found = std::lower_bound(cdf.begin(), cdf.end(), unit(engine) * integral);
                    const int bin = std::min(bins - 1, static_cast<int>(found - cdf.begin()));
                    const double x = -12 + (bin + unit(engine) - .5) * QuantumStates::Solver::dx;
                    points.push_back({glm::vec3(x, .55 * normal(engine), .55 * normal(engine)),
                                      static_cast<float>(std::arg(wave[bin]))});
                }
        }
    }
    pointCount = static_cast<int>(points.size());
    glBindBuffer(GL_ARRAY_BUFFER, pointBuffer);
    glBufferData(GL_ARRAY_BUFFER, points.size() * sizeof(Point), points.data(), GL_DYNAMIC_DRAW);
}

float QuantumParticleView::SuggestedDistance(const QuantumStates::Example &example) const
{
    if (example.family == QuantumStates::Family::Spin)
        return 12.0f;
    if (example.family == QuantumStates::Family::Oscillator)
        return example.state <= 6 ? std::max(8.0f, static_cast<float>(4 * std::sqrt(2 * example.state + 1.0))) : 12.0f;
    if (example.family == QuantumStates::Family::Box)
        return 20.0f;
    return example.family == QuantumStates::Family::Hydrogen ? (example.state == 0  ? 9.0f
                                                                : example.state < 4 ? 28.0f
                                                                                    : 65.0f)
                                                             : 28.0f;
}

void QuantumParticleView::Render(const QuantumStates::Example &example, double time, int width, int height, float fov,
                                 const glm::vec3 &camera, const glm::vec3 &front, const glm::vec3 &up) const
{
    if (!program || width <= 0 || height <= 0)
        return;
    const auto projection = glm::perspective(glm::radians(fov), static_cast<float>(width) / height, .005f, 2000.0f);
    const auto vp = projection * glm::lookAt(camera, camera + front, up);
    glUseProgram(program);
    glUniformMatrix4fv(glGetUniformLocation(program, "viewProjection"), 1, GL_FALSE, glm::value_ptr(vp));
    const double energy = example.state == 0 ? -.5 : example.state < 4 ? -.125 : -1.0 / 18;
    glUniform1f(glGetUniformLocation(program, "globalPhase"),
                example.family == QuantumStates::Family::Hydrogen
                    ? static_cast<float>(std::remainder(-energy * time, 6.283185307179586))
                    : 0);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glUniform1i(glGetUniformLocation(program, "points"), true);
    glPointSize(3);
    glBindVertexArray(pointVao);
    glDrawArrays(GL_POINTS, 0, pointCount);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_TRUE);
    glUniform1i(glGetUniformLocation(program, "points"), false);
    const auto draw = [&](const std::vector<Point> &vertices, GLenum mode, const glm::vec3 &color) {
        glUniform3fv(glGetUniformLocation(program, "lineColor"), 1, glm::value_ptr(color));
        glBindVertexArray(lineVao);
        glBindBuffer(GL_ARRAY_BUFFER, lineBuffer);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Point), vertices.data(), GL_DYNAMIC_DRAW);
        glDrawArrays(mode, 0, static_cast<int>(vertices.size()));
    };
    const bool spin = example.family == QuantumStates::Family::Spin;
    const float size = spin                                                ? 3.0f
                       : example.family == QuantumStates::Family::Hydrogen ? SuggestedDistance(example) * .25f
                                                                           : 10.0f;
    for (int axis = 0; axis < 3; ++axis)
    {
        glm::vec3 a(0), b(0);
        a[axis] = -size;
        b[axis] = size;
        glm::vec3 color(.06f);
        color[axis] = .28f;
        draw({{a, 0}, {b, 0}}, GL_LINES, color);
    }
    if (spin)
    {
        for (int plane = 0; plane < 3; ++plane)
        {
            std::vector<Point> ring;
            for (int point = 0; point <= 128; ++point)
            {
                const float angle = static_cast<float>(point * 6.283185307179586 / 128);
                glm::vec3 p(0);
                p[plane] = 3 * std::cos(angle);
                p[(plane + 1) % 3] = 3 * std::sin(angle);
                ring.push_back({p, 0});
            }
            draw(ring, GL_LINE_STRIP, {.12f, .24f, .32f});
        }
        glm::vec3 direction(0, 1, 0);
        if (example.state == 1)
            direction = {0, -1, 0};
        if (example.state == 2)
            direction = {1, 0, 0};
        if (example.state == 3)
            direction = {-1, 0, 0};
        if (example.state == 4)
            direction = {0, 0, -1};
        if (example.state == 5)
            direction = {0, std::cos(time), std::sin(time)};
        draw({{{0, 0, 0}, 0}, {direction * 3.0f, 0}}, GL_LINES, {.25f, .85f, 1});
        glPointSize(10);
        draw({{direction * 3.0f, 0}}, GL_POINTS, {1, .65f, .2f});
    }
    glBindVertexArray(0);
}
void QuantumParticleView::Release()
{
    if (program)
        glDeleteProgram(program);
    for (auto vao : {pointVao, lineVao})
        if (vao)
            glDeleteVertexArrays(1, &vao);
    for (auto buffer : {pointBuffer, lineBuffer})
        if (buffer)
            glDeleteBuffers(1, &buffer);
    program = pointVao = pointBuffer = lineVao = lineBuffer = 0;
}

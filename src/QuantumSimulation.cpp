#include "QuantumSimulation.h"
#include "glad/glad.h"
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iomanip>
#include <sstream>

unsigned int compileShaderProgram(const char *, const char *);

bool QuantumSimulation::Initialize()
{
    const char *vertex = R"(#version 330 core
out vec2 uv;
void main() {
    vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);
    uv=p; gl_Position=vec4(p*2-1,0,1);
})";
    const char *fragment = R"(#version 330 core
in vec2 uv;
out vec4 color;
uniform sampler2D wave;
uniform int mode;
uniform float aspect;
uniform float peakDensity;
uniform float phase;
uniform vec3 bloch;
uniform vec4 probabilities;
uniform int barCount;
uniform bool showPotential;
vec3 hue(float a) { return .55+.45*cos(a+vec3(0,2.094,4.189)); }
float line(float a,float b,float width) { return 1-smoothstep(width,width*2,abs(a-b)); }
float segment(vec2 p,vec2 a,vec2 b) {
    vec2 d=b-a; float h=clamp(dot(p-a,d)/max(dot(d,d),1e-8),0,1);
    return length(p-a-h*d);
}
void main() {
    vec3 background=vec3(.008,.014,.026);
    vec3 result=background;
    if (mode==1) {
        vec2 coord=(uv-.5)*vec2(aspect,1)*1.8+.5;
        if (min(coord.x,coord.y)<0 || max(coord.x,coord.y)>1) { color=vec4(background,1); return; }
        vec2 psi=texture(wave,coord).rg;
        float density=dot(psi,psi)/max(peakDensity,1e-12);
        float angle=atan(psi.y,psi.x)+phase;
        result+=hue(angle)*(1-exp(-density*3.5));
        result+=vec3(.018)*max(line(coord.x,.5,.001),line(coord.y,.5,.001));
    } else if (mode>=2) {
        vec2 p=(uv-vec2(.34,.62))*vec2(aspect,1)*2.5;
        float radius=length(p);
        if (mode==2) {
            result+=vec3(.08,.14,.20)*line(radius,.65,.004);
            float ellipse=sqrt(p.x*p.x+pow(p.y/.28,2));
            result+=vec3(.04,.10,.14)*line(ellipse,.65,.004);
            result+=vec3(.05,.10,.15)*max(line(p.x,0,.003),line(p.y,0,.003))*step(radius,.65);
            vec2 endpoint=vec2(.866*bloch.x-.5*bloch.y,.25*bloch.x+.433*bloch.y+.866*bloch.z)*.65;
            float arrow=1-smoothstep(.009,.017,segment(p,vec2(0),endpoint));
            result+=arrow*vec3(.15,.8,1.2);
            result+=vec3(.8,.4,.1)*(1-smoothstep(.025,.04,length(p-endpoint)));
        }
        for (int i=0;i<4;++i) {
            if (i>=barCount) break;
            float left=.12+float(i)*.76/float(barCount);
            float right=left+.48/float(barCount);
            float value=probabilities[i];
            bool bar=uv.x>left && uv.x<right && uv.y>.08 && uv.y<.08+value*.22;
            if (bar) result=vec3(.12,.55,.85);
            if (uv.y>.08 && uv.y<.30 && uv.x>left && uv.x<right)
                result+=vec3(.03,.06,.08);
        }
    } else {
        vec2 psi=texture(wave,vec2(uv.x,.5)).rg;
        psi=mat2(cos(phase),sin(phase),-sin(phase),cos(phase))*psi;
        float density=dot(psi,psi)/max(peakDensity,1e-12);
        if (uv.y>.48) {
            result+=hue(atan(psi.y,psi.x))*(1-exp(-density*3))
                *exp(-pow((uv.y-.75)/.17,4));
        } else {
            float y=(uv.y-.08)/.32;
            result+=vec3(.08,.12,.16)*line(y,0,.008);
            result+=vec3(.05,.07,.10)*line(uv.x,.5,.001);
            result+=vec3(.8,.9,1.1)*line(y,density*.95,.008);
            result+=vec3(.95,.19,.17)*line(y,.35+psi.x/sqrt(max(peakDensity,1e-12))*.27,.007);
            result+=vec3(.15,.5,1)*line(y,.35+psi.y/sqrt(max(peakDensity,1e-12))*.27,.007);
            if (showPotential) result+=vec3(.85,.45,.08)*line(y,texture(wave,vec2(uv.x,.5)).b/4.0,.006);
        }
    }
    color=vec4(result,1);
})";
    program = compileShaderProgram(vertex, fragment);
    int linked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked)
    {
        Release();
        return false;
    }
    glGenVertexArrays(1, &vao);
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if (!particles.Initialize())
    {
        Release();
        return false;
    }
    Select(0);
    return true;
}
void QuantumSimulation::Select(int index)
{
    selected = std::clamp(index, 0, static_cast<int>(QuantumStates::Examples().size()) - 1);
    time = 0;
    const auto &example = QuantumStates::Examples()[selected];
    if (example.family == QuantumStates::Family::Numerical)
        solver.Reset(example.state);
    Upload();
}
void QuantumSimulation::Advance(double seconds)
{
    if (seconds <= 0)
        return;
    const auto &example = QuantumStates::Examples()[selected];
    if (example.family == QuantumStates::Family::Numerical)
        solver.Advance(seconds);
    time += seconds;
    // Hydrogen density is stationary; its global phase is rotated on the GPU.
    if (example.family != QuantumStates::Family::Hydrogen)
        Upload();
}
void QuantumSimulation::Upload()
{
    if (!texture)
        return;
    const auto &example = QuantumStates::Examples()[selected];
    const bool hydrogen = example.family == QuantumStates::Family::Hydrogen;
    const bool numerical = example.family == QuantumStates::Family::Numerical;
    const int width = QuantumStates::Solver::size;
    const int height = 1;
    const double extent = 12;
    std::vector<glm::vec4> data(width * height);
    peakDensity = 1e-15;
    visibleNorm = mean = deviation = 0;
    double secondMoment = 0;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
        {
            const double coordinate = hydrogen ? (example.state == 0  ? 12
                                                  : example.state < 4 ? 36
                                                                      : 80) *
                                                     x / static_cast<double>(width - 1)
                                               : -extent + 2 * extent * x / (width - 1);
            const auto psi = hydrogen
                                 ? std::complex<double>(QuantumStates::HydrogenRadial(example.state, coordinate), 0)
                             : numerical ? solver.Wave()[x]
                                         : QuantumStates::Evaluate(example, coordinate, 0, time);
            data[y * width + x] = glm::vec4(psi.real(), psi.imag(), numerical ? solver.Potential()[x] : 0, 0);
            const double density = std::norm(psi);
            peakDensity = std::max(peakDensity, density);
            if (!hydrogen)
            {
                const double weight = density * 2 * extent / (width - 1);
                visibleNorm += weight;
                mean += coordinate * weight;
                secondMoment += coordinate * coordinate * weight;
            }
        }
    if (visibleNorm > 1e-12)
    {
        mean /= visibleNorm;
        deviation = std::sqrt(std::max(0.0, secondMoment / visibleNorm - mean * mean));
    }
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, width, height, 0, GL_RGBA, GL_FLOAT, data.data());
    particles.Update(example, time, numerical ? &solver.Wave() : nullptr);
}
void QuantumSimulation::Render(int width, int height, float fieldOfView, const glm::vec3 &camera,
                               const glm::vec3 &front, const glm::vec3 &up) const
{
    if (!program)
        return;
    const auto &example = QuantumStates::Examples()[selected];
    using Family = QuantumStates::Family;
    const int graphHeight = std::max(90, height / 5);
    glViewport(0, graphHeight, width, height - graphHeight);
    particles.Render(example, time, width, height - graphHeight, fieldOfView, camera, front, up);
    glViewport(0, 0, width, graphHeight);
    const float aspect = static_cast<float>(width) / graphHeight;
    const int mode = (example.family == Family::Spin || example.family == Family::Bell) ? 3 : 0;
    glUseProgram(program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glUniform1i(glGetUniformLocation(program, "wave"), 0);
    glUniform1i(glGetUniformLocation(program, "mode"), mode);
    glUniform1i(glGetUniformLocation(program, "showPotential"), example.family == Family::Numerical);
    glUniform1f(glGetUniformLocation(program, "aspect"), aspect);
    glUniform1f(glGetUniformLocation(program, "peakDensity"), static_cast<float>(peakDensity));
    const double energy = example.state == 0 ? -.5 : example.state < 4 ? -.125 : -1.0 / 18;
    glUniform1f(
        glGetUniformLocation(program, "phase"),
        example.family == Family::Hydrogen ? static_cast<float>(std::remainder(-energy * time, 6.283185307179586)) : 0);
    glm::vec3 bloch(0, 0, 1);
    if (example.state == 1)
        bloch = {0, 0, -1};
    if (example.state == 2)
        bloch = {1, 0, 0};
    if (example.state == 3)
        bloch = {-1, 0, 0};
    if (example.state == 4)
        bloch = {0, 1, 0};
    if (example.state == 5)
        bloch = {0, -std::sin(time), std::cos(time)};
    glUniform3fv(glGetUniformLocation(program, "bloch"), 1, glm::value_ptr(bloch));
    const auto p = QuantumStates::Probabilities(example, time);
    glm::vec4 bars(0);
    for (int i = 0; i < static_cast<int>(p.size()); ++i)
        bars[i] = static_cast<float>(p[i]);
    glUniform4fv(glGetUniformLocation(program, "probabilities"), 1, glm::value_ptr(bars));
    glUniform1i(glGetUniformLocation(program, "barCount"), static_cast<int>(p.size()));
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glViewport(0, 0, width, height);
}
std::string QuantumSimulation::Information() const
{
    const auto &example = QuantumStates::Examples()[selected];
    std::ostringstream text;
    text << example.name << "\r\n\r\n" << example.explanation << "\r\n\r\n";
    text << "Dimensionless time  " << std::fixed << std::setprecision(3) << time << "\r\n";
    const auto probabilities = QuantumStates::Probabilities(example, time);
    if (!probabilities.empty())
    {
        const char *qubit[] = {"P(0)", "P(1)"};
        const char *joint[] = {"P(00)", "P(01)", "P(10)", "P(11)"};
        for (int i = 0; i < static_cast<int>(probabilities.size()); ++i)
            text << (probabilities.size() == 2 ? qubit[i] : joint[i]) << "  " << probabilities[i] << "\r\n";
        if (example.family == QuantumStates::Family::Spin)
            text << "\r\n3D Bloch sphere: physical display Y represents Bloch Z; display -Z represents Bloch Y. This "
                    "is state space, not a particle trajectory.\r\n";
        else
            text << "\r\nBars left to right: 00, 01, 10, 11.\r\n";
    }
    else if (example.family != QuantumStates::Family::Hydrogen)
    {
        text << "Probability in display  " << std::setprecision(6) << visibleNorm << "\r\n"
             << "Mean x  " << mean << "\r\nSigma x  " << deviation << "\r\n\r\n"
             << "Horizontal: x = -12 to +12\r\nWhite: probability density\r\nRed: real amplitude\r\nBlue: imaginary "
                "amplitude\r\nTop: density / phase strip\r\nCurves use shared peak scaling.\r\n";
    }
    else
        text << "\r\n3D cloud samples |psi(x,y,z)|^2 in Bohr radii.\r\nBottom graph: radial probability r^2 "
                "|R_nl(r)|^2, from r=0 to "
             << (example.state == 0  ? 12
                 : example.state < 4 ? 36
                                     : 80)
             << " Bohr radii.\r\n";
    if (example.family != QuantumStates::Family::Hydrogen && example.family != QuantumStates::Family::Spin &&
        example.family != QuantumStates::Family::Bell)
        text << "\r\n3D cloud: Born probability along X. Gaussian display thickness in Y/Z is illustrative; the "
                "underlying equation is 1D. Dots are probability samples, not classical particle paths.\r\n";
    if (example.family == QuantumStates::Family::Numerical)
        text << "\r\nAmber curve: potential V (vertical chart span = 4 energy units).\r\n";
    return text.str();
}
void QuantumSimulation::Release()
{
    particles.Release();
    if (program)
        glDeleteProgram(program);
    if (vao)
        glDeleteVertexArrays(1, &vao);
    if (texture)
        glDeleteTextures(1, &texture);
    program = vao = texture = 0;
}

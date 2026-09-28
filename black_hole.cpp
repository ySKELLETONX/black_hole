#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#include <vector>
#include <iostream>
#define _USE_MATH_DEFINES
#include <cmath>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <chrono>
#include <ctime>
#include <fstream>
#include <string>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
using namespace glm;
using namespace std;

#ifdef _WIN32
// Ask NVIDIA Optimus / AMD PowerXpress drivers to use the dedicated GPU
extern "C" {
    __declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
    __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

// VARS
double c = 299792458.0;
double G = 6.67430e-11;
bool Gravity = false;

struct BlackHole {
    vec3 position;
    double mass;
    double r_s;

    BlackHole(vec3 pos, double m) : position(pos), mass(m) { r_s = 2.0 * G * mass / (c * c); }
};
BlackHole SagA(vec3(0.0f, 0.0f, 0.0f), 8.54e36); // Sagittarius A black hole

// -- Render settings (tweakable live in the UI) -- //
struct Settings {
    // camera / quality
    float fov         = 60.0f;
    float renderScale = 1.0f;
    int   maxSteps    = 1000;
    float stepScale   = 0.05f;
    bool  autoOrbit   = false;
    float orbitRate   = 0.05f;
    // accretion disk (radii in Schwarzschild radii)
    bool  diskEnabled = true;
    float diskInner   = 3.0f;    // ISCO
    float diskOuter   = 14.0f;
    float diskTemp    = 4200.0f;
    float diskBright  = 2.5f;
    float diskDensity = 1.0f;
    float diskDetail  = 1.0f;
    float diskSpeed   = 1.0f;
    float doppler     = 1.0f;
    // background
    float stars       = 1.0f;
    float galaxy      = 1.0f;
    // post
    float exposure    = 1.0f;
    float bloom       = 0.12f;
    float vignette    = 0.35f;
    bool  showGrid    = false;
    bool  showUI      = true;
    bool  paused      = false;
};
Settings S;

struct Camera {
    // Orbit around the black hole at (0, 0, 0). Target values are driven by input,
    // current values ease towards them for smooth motion.
    vec3  target = vec3(0.0f);
    float radius = 22.0f * float(SagA.r_s), radiusT = radius;
    float minRadius = 2.5f * float(SagA.r_s), maxRadius = 150.0f * float(SagA.r_s);
    float azimuth = 0.6f, azimuthT = azimuth;
    float elevation = float(M_PI) / 2.0f - 0.12f, elevationT = elevation; // polar angle from +y

    float orbitSpeed = 0.005f;

    bool   dragging = false;
    double lastX = 0.0, lastY = 0.0;

    vec3 position() const {
        return vec3(
            radius * sin(elevation) * cos(azimuth),
            radius * cos(elevation),
            radius * sin(elevation) * sin(azimuth)
        );
    }
    void tick(float dt) {
        float k = 1.0f - exp(-dt * 10.0f);
        azimuth   += (azimuthT - azimuth) * k;
        elevation += (elevationT - elevation) * k;
        radius    += (radiusT - radius) * k;
    }
    void processMouseMove(double x, double y) {
        float dx = float(x - lastX);
        float dy = float(y - lastY);
        if (dragging) {
            azimuthT   += dx * orbitSpeed;
            elevationT -= dy * orbitSpeed;
            elevationT  = glm::clamp(elevationT, 0.01f, float(M_PI) - 0.01f);
        }
        lastX = x;
        lastY = y;
    }
    void processMouseButton(int button, int action, GLFWwindow* win) {
        if (button == GLFW_MOUSE_BUTTON_LEFT || button == GLFW_MOUSE_BUTTON_MIDDLE) {
            if (action == GLFW_PRESS) {
                dragging = true;
                glfwGetCursorPos(win, &lastX, &lastY);
            } else if (action == GLFW_RELEASE) {
                dragging = false;
            }
        }
        if (button == GLFW_MOUSE_BUTTON_RIGHT) {
            Gravity = (action == GLFW_PRESS);
        }
    }
    void processScroll(double yoffset) {
        radiusT *= float(pow(0.9, yoffset));
        radiusT  = glm::clamp(radiusT, minRadius, maxRadius);
    }
};
Camera camera;

struct ObjectData {
    vec4 posRadius; // xyz = position, w = radius
    vec4 color;     // rgb = color, a = unused
    float  mass;
    vec3 velocity = vec3(0.0f, 0.0f, 0.0f); // Initial velocity
};
vector<ObjectData> objects = {
    { vec4(4e11f, 0.0f, 0.0f, 4e10f)   , vec4(1.0f, 0.85f, 0.45f, 1), 1.98892e30 },
    { vec4(0.0f, 0.0f, 4e11f, 4e10f)   , vec4(1.0f, 0.35f, 0.2f, 1),  1.98892e30 },
    { vec4(0.0f, 0.0f, 0.0f, float(SagA.r_s)) , vec4(0,0,0,1), static_cast<float>(SagA.mass)  },
};

struct Engine {
    GLFWwindow* window;
    GLuint quadVAO = 0;
    GLuint postProgram = 0;
    GLuint computeProgram = 0;
    GLuint gridShaderProgram = 0;
    GLuint objectsUBO = 0;
    // HDR render target (with mip chain used for bloom)
    GLuint hdrTex = 0;
    int texW = 0, texH = 0;
    // -- grid mesh vars -- //
    GLuint gridVAO = 0;
    GLuint gridVBO = 0;
    GLuint gridEBO = 0;
    int gridIndexCount = 0;

    int WIDTH = 1280;
    int HEIGHT = 720;
    // Offscreen output (used by --screenshot so the size is not limited by the monitor)
    GLuint outFBO = 0, outTex = 0;
    int outW = 0, outH = 0;

    Engine() {
        if (!glfwInit()) {
            cerr << "GLFW init failed\n";
            exit(EXIT_FAILURE);
        }
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        window = glfwCreateWindow(WIDTH, HEIGHT, "Black Hole", nullptr, nullptr);
        if (!window) {
            cerr << "Failed to create GLFW window\n";
            glfwTerminate();
            exit(EXIT_FAILURE);
        }
        glfwMakeContextCurrent(window);
        glfwSwapInterval(1);
        glewExperimental = GL_TRUE;
        GLenum glewErr = glewInit();
        if (glewErr != GLEW_OK) {
            cerr << "Failed to initialize GLEW: "
                << (const char*)glewGetErrorString(glewErr)
                << "\n";
            glfwTerminate();
            exit(EXIT_FAILURE);
        }
        cout << "OpenGL " << glGetString(GL_VERSION) << "\n";
        cout << "GPU: " << glGetString(GL_RENDERER) << endl;

        postProgram       = CreateShaderProgram("post.vert", "post.frag");
        gridShaderProgram = CreateShaderProgram("grid.vert", "grid.frag");
        computeProgram    = CreateComputeProgram("geodesic.comp");

        glGenBuffers(1, &objectsUBO);
        glBindBuffer(GL_UNIFORM_BUFFER, objectsUBO);
        // int + padding + 16 x (vec4 posRadius + vec4 color) + 16 floats for mass
        GLsizeiptr objUBOSize = sizeof(int) + 3 * sizeof(float)
            + 16 * (sizeof(vec4) + sizeof(vec4))
            + 16 * sizeof(float);
        glBufferData(GL_UNIFORM_BUFFER, objUBOSize, nullptr, GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_UNIFORM_BUFFER, 3, objectsUBO);  // binding = 3 matches shader

        quadVAO = QuadVAO();
    }
    void generateGrid(const vector<ObjectData>& objects) {
        const int gridSize = 25;
        const float spacing = 1e10f;

        vector<vec3> vertices;
        vector<GLuint> indices;

        for (int z = 0; z <= gridSize; ++z) {
            for (int x = 0; x <= gridSize; ++x) {
                float worldX = (x - gridSize / 2) * spacing;
                float worldZ = (z - gridSize / 2) * spacing;

                float y = 0.0f;

                // Warp grid using Schwarzschild geometry (Flamm's paraboloid)
                for (const auto& obj : objects) {
                    vec3 objPos = vec3(obj.posRadius);
                    double mass = obj.mass;

                    double r_s = 2.0 * G * mass / (c * c);
                    double dx = worldX - objPos.x;
                    double dz = worldZ - objPos.z;
                    double dist = sqrt(dx * dx + dz * dz);

                    if (dist > r_s) {
                        double deltaY = 2.0 * sqrt(r_s * (dist - r_s));
                        y += static_cast<float>(deltaY) - 3e10f;
                    } else {
                        y += 2.0f * static_cast<float>(r_s) - 3e10f;
                    }
                }

                vertices.emplace_back(worldX, y, worldZ);
            }
        }

        for (int z = 0; z < gridSize; ++z) {
            for (int x = 0; x < gridSize; ++x) {
                int i = z * (gridSize + 1) + x;
                indices.push_back(i);
                indices.push_back(i + 1);

                indices.push_back(i);
                indices.push_back(i + gridSize + 1);
            }
        }

        if (gridVAO == 0) glGenVertexArrays(1, &gridVAO);
        if (gridVBO == 0) glGenBuffers(1, &gridVBO);
        if (gridEBO == 0) glGenBuffers(1, &gridEBO);

        glBindVertexArray(gridVAO);

        glBindBuffer(GL_ARRAY_BUFFER, gridVBO);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(vec3), vertices.data(), GL_DYNAMIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gridEBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(GLuint), indices.data(), GL_STATIC_DRAW);

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(vec3), (void*)0);

        gridIndexCount = (int)indices.size();

        glBindVertexArray(0);
    }
    void drawGrid(const mat4& viewProj) {
        glUseProgram(gridShaderProgram);
        glUniformMatrix4fv(glGetUniformLocation(gridShaderProgram, "viewProj"),
                        1, GL_FALSE, glm::value_ptr(viewProj));
        glBindVertexArray(gridVAO);

        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        glDrawElements(GL_LINES, gridIndexCount, GL_UNSIGNED_INT, 0);

        glDisable(GL_BLEND);
        glBindVertexArray(0);
    }
    GLuint CreateShaderProgram(const char* vertPath, const char* fragPath) {
        auto loadShader = [](const char* path, GLenum type) -> GLuint {
            std::ifstream in(path);
            if (!in.is_open()) {
                std::cerr << "Failed to open shader: " << path << "\n";
                exit(EXIT_FAILURE);
            }
            std::stringstream ss;
            ss << in.rdbuf();
            std::string srcStr = ss.str();
            const char* src = srcStr.c_str();

            GLuint shader = glCreateShader(type);
            glShaderSource(shader, 1, &src, nullptr);
            glCompileShader(shader);

            GLint success;
            glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
            if (!success) {
                GLint logLen;
                glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLen);
                std::vector<char> log(logLen);
                glGetShaderInfoLog(shader, logLen, nullptr, log.data());
                std::cerr << "Shader compile error (" << path << "):\n" << log.data() << "\n";
                exit(EXIT_FAILURE);
            }
            return shader;
        };

        GLuint vertShader = loadShader(vertPath, GL_VERTEX_SHADER);
        GLuint fragShader = loadShader(fragPath, GL_FRAGMENT_SHADER);

        GLuint program = glCreateProgram();
        glAttachShader(program, vertShader);
        glAttachShader(program, fragShader);
        glLinkProgram(program);

        GLint linkSuccess;
        glGetProgramiv(program, GL_LINK_STATUS, &linkSuccess);
        if (!linkSuccess) {
            GLint logLen;
            glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLen);
            std::vector<char> log(logLen);
            glGetProgramInfoLog(program, logLen, nullptr, log.data());
            std::cerr << "Shader link error:\n" << log.data() << "\n";
            exit(EXIT_FAILURE);
        }

        glDeleteShader(vertShader);
        glDeleteShader(fragShader);

        return program;
    }
    GLuint CreateComputeProgram(const char* path) {
        std::ifstream in(path);
        if(!in.is_open()) {
            std::cerr << "Failed to open compute shader: " << path << "\n";
            exit(EXIT_FAILURE);
        }
        std::stringstream ss;
        ss << in.rdbuf();
        std::string srcStr = ss.str();
        const char* src = srcStr.c_str();

        GLuint cs = glCreateShader(GL_COMPUTE_SHADER);
        glShaderSource(cs, 1, &src, nullptr);
        glCompileShader(cs);
        GLint ok;
        glGetShaderiv(cs, GL_COMPILE_STATUS, &ok);
        if(!ok) {
            GLint logLen;
            glGetShaderiv(cs, GL_INFO_LOG_LENGTH, &logLen);
            std::vector<char> log(logLen);
            glGetShaderInfoLog(cs, logLen, nullptr, log.data());
            std::cerr << "Compute shader compile error:\n" << log.data() << "\n";
            exit(EXIT_FAILURE);
        }

        GLuint prog = glCreateProgram();
        glAttachShader(prog, cs);
        glLinkProgram(prog);
        glGetProgramiv(prog, GL_LINK_STATUS, &ok);
        if(!ok) {
            GLint logLen;
            glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &logLen);
            std::vector<char> log(logLen);
            glGetProgramInfoLog(prog, logLen, nullptr, log.data());
            std::cerr << "Compute shader link error:\n" << log.data() << "\n";
            exit(EXIT_FAILURE);
        }

        glDeleteShader(cs);
        return prog;
    }
    // (Re)allocate the HDR target with a full mip chain when the size changes.
    void ensureTarget(int w, int h) {
        if (w == texW && h == texH && hdrTex) return;
        if (hdrTex) glDeleteTextures(1, &hdrTex);
        texW = w; texH = h;
        int levels = 1 + (int)floor(log2((double)std::max(w, h)));
        glGenTextures(1, &hdrTex);
        glBindTexture(GL_TEXTURE_2D, hdrTex);
        glTexStorage2D(GL_TEXTURE_2D, levels, GL_RGBA16F, w, h);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    void outputSize(int& w, int& h) {
        if (outFBO) { w = outW; h = outH; }
        else glfwGetFramebufferSize(window, &w, &h);
    }
    void createOffscreen(int w, int h) {
        outW = w; outH = h;
        glGenTextures(1, &outTex);
        glBindTexture(GL_TEXTURE_2D, outTex);
        glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, w, h);
        glGenFramebuffers(1, &outFBO);
        glBindFramebuffer(GL_FRAMEBUFFER, outFBO);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, outTex, 0);
    }
    void setUniform(GLuint prog, const char* name, float v) { glUniform1f(glGetUniformLocation(prog, name), v); }
    void setUniform(GLuint prog, const char* name, int v)   { glUniform1i(glGetUniformLocation(prog, name), v); }
    void setUniform(GLuint prog, const char* name, vec3 v)  { glUniform3fv(glGetUniformLocation(prog, name), 1, value_ptr(v)); }

    void dispatchCompute(const Camera& cam, float time) {
        int fbW, fbH;
        outputSize(fbW, fbH);
        int w = std::max(16, int(fbW * S.renderScale));
        int h = std::max(16, int(fbH * S.renderScale));
        ensureTarget(w, h);

        float rs = float(SagA.r_s);
        vec3 pos = cam.position();
        vec3 fwd = normalize(cam.target - pos);
        vec3 right = normalize(cross(fwd, vec3(0, 1, 0)));  // y is up, disk is in the x-z plane
        vec3 up = cross(right, fwd);
        float tanHalfFov = tan(radians(S.fov * 0.5f));

        GLuint p = computeProgram;
        glUseProgram(p);
        setUniform(p, "uCamPos", pos / rs);
        setUniform(p, "uCamRight", right);
        setUniform(p, "uCamUp", up);
        setUniform(p, "uCamFwd", fwd);
        setUniform(p, "uTanHalfFov", tanHalfFov);
        setUniform(p, "uTime", time);
        setUniform(p, "uMaxSteps", S.maxSteps);
        setUniform(p, "uStepScale", S.stepScale);
        setUniform(p, "uPixelAngle", 2.0f * tanHalfFov / float(h));
        setUniform(p, "uRsMeters", rs);
        setUniform(p, "uDiskEnabled", S.diskEnabled ? 1 : 0);
        setUniform(p, "uDiskInner", S.diskInner);
        setUniform(p, "uDiskOuter", S.diskOuter);
        setUniform(p, "uDiskTemp", S.diskTemp);
        setUniform(p, "uDiskBrightness", S.diskBright);
        setUniform(p, "uDiskDensity", S.diskDensity);
        setUniform(p, "uDiskDetail", S.diskDetail);
        setUniform(p, "uDiskSpeed", S.diskSpeed);
        setUniform(p, "uDoppler", S.doppler);
        setUniform(p, "uStarBrightness", S.stars);
        setUniform(p, "uGalaxyBrightness", S.galaxy);
        uploadObjectsUBO(objects);

        glBindImageTexture(0, hdrTex, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
        glDispatchCompute((w + 15) / 16, (h + 15) / 16, 1);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

        glBindTexture(GL_TEXTURE_2D, hdrTex);
        glGenerateMipmap(GL_TEXTURE_2D);
    }
    void uploadObjectsUBO(const vector<ObjectData>& objs) {
        struct UBOData {
            int   numObjects;
            float _pad0, _pad1, _pad2;
            vec4  posRadius[16];
            vec4  color[16];
            float mass[16];
        } data = {};

        size_t count = std::min(objs.size(), size_t(16));
        data.numObjects = static_cast<int>(count);

        for (size_t i = 0; i < count; ++i) {
            data.posRadius[i] = objs[i].posRadius;
            data.color[i] = objs[i].color;
            data.mass[i] = objs[i].mass;
        }

        glBindBuffer(GL_UNIFORM_BUFFER, objectsUBO);
        glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(data), &data);
    }
    void drawPost(float time) {
        int fbW, fbH;
        outputSize(fbW, fbH);
        glViewport(0, 0, fbW, fbH);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);

        glUseProgram(postProgram);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, hdrTex);
        setUniform(postProgram, "uHdr", 0);
        glUniform2f(glGetUniformLocation(postProgram, "uTexel"), 1.0f / texW, 1.0f / texH);
        setUniform(postProgram, "uExposure", S.exposure);
        setUniform(postProgram, "uBloom", S.bloom);
        setUniform(postProgram, "uVignette", S.vignette);
        setUniform(postProgram, "uTime", time);

        glBindVertexArray(quadVAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }
    GLuint QuadVAO() {
        float quadVertices[] = {
            // positions   // texCoords
            -1.0f,  1.0f,  0.0f, 1.0f,  // top left
            -1.0f, -1.0f,  0.0f, 0.0f,  // bottom left
             1.0f, -1.0f,  1.0f, 0.0f,  // bottom right

            -1.0f,  1.0f,  0.0f, 1.0f,  // top left
             1.0f, -1.0f,  1.0f, 0.0f,  // bottom right
             1.0f,  1.0f,  1.0f, 1.0f   // top right
        };

        GLuint VAO, VBO;
        glGenVertexArrays(1, &VAO);
        glGenBuffers(1, &VBO);

        glBindVertexArray(VAO);
        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);

        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
        glEnableVertexAttribArray(1);
        return VAO;
    }
    void saveScreenshot(const string& path) {
        int fbW, fbH;
        outputSize(fbW, fbH);
        vector<unsigned char> pixels(size_t(fbW) * fbH * 3);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadBuffer(outFBO ? GL_COLOR_ATTACHMENT0 : GL_BACK);
        glReadPixels(0, 0, fbW, fbH, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
        stbi_flip_vertically_on_write(1);
        if (stbi_write_png(path.c_str(), fbW, fbH, 3, pixels.data(), fbW * 3))
            cout << "[INFO] Screenshot saved: " << path << endl;
        else
            cerr << "[ERROR] Could not save screenshot: " << path << endl;
    }
};
Engine engine;

bool screenshotRequested = false;

void setupCameraCallbacks(GLFWwindow* window) {
    glfwSetWindowUserPointer(window, &camera);

    glfwSetMouseButtonCallback(window, [](GLFWwindow* win, int button, int action, int mods) {
        if (ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureMouse && action == GLFW_PRESS) return;
        Camera* cam = (Camera*)glfwGetWindowUserPointer(win);
        cam->processMouseButton(button, action, win);
    });

    glfwSetCursorPosCallback(window, [](GLFWwindow* win, double x, double y) {
        Camera* cam = (Camera*)glfwGetWindowUserPointer(win);
        cam->processMouseMove(x, y);
    });

    glfwSetScrollCallback(window, [](GLFWwindow* win, double xoffset, double yoffset) {
        if (ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureMouse) return;
        Camera* cam = (Camera*)glfwGetWindowUserPointer(win);
        cam->processScroll(yoffset);
    });

    glfwSetKeyCallback(window, [](GLFWwindow* win, int key, int scancode, int action, int mods) {
        if (ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureKeyboard) return;
        if (action != GLFW_PRESS) return;
        switch (key) {
            case GLFW_KEY_G:
                Gravity = !Gravity;
                cout << "[INFO] Gravity turned " << (Gravity ? "ON" : "OFF") << endl;
                break;
            case GLFW_KEY_H:     S.showUI = !S.showUI; break;
            case GLFW_KEY_SPACE: S.autoOrbit = !S.autoOrbit; break;
            case GLFW_KEY_P:     screenshotRequested = true; break;
            case GLFW_KEY_ESCAPE: glfwSetWindowShouldClose(win, 1); break;
        }
    });
}

void drawUI(float fps) {
    if (!S.showUI) return;
    ImGui::SetNextWindowPos(ImVec2(12, 12), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(330, 0), ImGuiCond_FirstUseEver);
    ImGui::Begin("Black Hole", &S.showUI);
    ImGui::Text("%.0f FPS  |  %d x %d", fps, engine.texW, engine.texH);
    ImGui::TextDisabled("Drag: orbit   Scroll: zoom   H: hide UI");
    ImGui::TextDisabled("Space: auto orbit   P: screenshot");

    if (ImGui::CollapsingHeader("Accretion disk", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Enabled", &S.diskEnabled);
        ImGui::SliderFloat("Temperature (K)", &S.diskTemp, 1500.0f, 20000.0f, "%.0f");
        ImGui::SliderFloat("Brightness", &S.diskBright, 0.1f, 10.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
        ImGui::SliderFloat("Doppler / redshift", &S.doppler, 0.0f, 1.0f);
        ImGui::SliderFloat("Gas density", &S.diskDensity, 0.05f, 4.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
        ImGui::SliderFloat("Turbulence scale", &S.diskDetail, 0.3f, 3.0f);
        ImGui::SliderFloat("Rotation speed", &S.diskSpeed, 0.0f, 4.0f);
        ImGui::SliderFloat("Inner radius (rs)", &S.diskInner, 1.5f, 6.0f);
        ImGui::SliderFloat("Outer radius (rs)", &S.diskOuter, S.diskInner + 1.0f, 40.0f);
    }
    if (ImGui::CollapsingHeader("Sky", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("Stars", &S.stars, 0.0f, 3.0f);
        ImGui::SliderFloat("Milky Way", &S.galaxy, 0.0f, 3.0f);
    }
    if (ImGui::CollapsingHeader("Camera & post", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("Field of view", &S.fov, 20.0f, 110.0f, "%.0f deg");
        ImGui::Checkbox("Auto orbit", &S.autoOrbit);
        ImGui::SameLine();
        ImGui::Checkbox("Spacetime grid", &S.showGrid);
        ImGui::SliderFloat("Exposure", &S.exposure, 0.1f, 5.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
        ImGui::SliderFloat("Bloom", &S.bloom, 0.0f, 0.5f);
        ImGui::SliderFloat("Vignette", &S.vignette, 0.0f, 1.0f);
    }
    if (ImGui::CollapsingHeader("Quality")) {
        ImGui::SliderFloat("Render scale", &S.renderScale, 0.25f, 2.0f, "%.2fx");
        ImGui::SliderInt("Max steps", &S.maxSteps, 100, 3000);
        ImGui::SliderFloat("Step size", &S.stepScale, 0.01f, 0.2f, "%.3f");
        ImGui::Checkbox("Pause time", &S.paused);
    }
    ImGui::End();
}

string timestampedName() {
    time_t t = time(nullptr);
    tm lt;
#ifdef _WIN32
    localtime_s(&lt, &t);
#else
    localtime_r(&t, &lt);
#endif
    char buf[64];
    strftime(buf, sizeof(buf), "blackhole_%Y%m%d_%H%M%S.png", &lt);
    return buf;
}

// -- MAIN -- //
// Optional args: --screenshot out.png [--az deg] [--elev deg-above-disk] [--dist rs] [--time s] [--width w --height h]
int main(int argc, char** argv) {
    string autoShot;
    float simTime = 0.0f;
    for (int i = 1; i + 1 < argc; ++i) {
        string a = argv[i];
        if (a == "--screenshot") autoShot = argv[++i];
        else if (a == "--az")    camera.azimuth = camera.azimuthT = radians(stof(argv[++i]));
        else if (a == "--elev")  camera.elevation = camera.elevationT = float(M_PI) / 2.0f - radians(stof(argv[++i]));
        else if (a == "--dist")  camera.radius = camera.radiusT = stof(argv[++i]) * float(SagA.r_s);
        else if (a == "--time")  simTime = stof(argv[++i]);
        else if (a == "--width") engine.WIDTH = stoi(argv[++i]);
        else if (a == "--height") engine.HEIGHT = stoi(argv[++i]);
    }
    if (!autoShot.empty()) {
        S.showUI = false;
        glfwHideWindow(engine.window);
        engine.createOffscreen(engine.WIDTH, engine.HEIGHT);
    }

    setupCameraCallbacks(engine.window);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 8.0f;
    style.FrameRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.Colors[ImGuiCol_WindowBg].w = 0.82f;
    ImGui_ImplGlfw_InitForOpenGL(engine.window, true);
    ImGui_ImplOpenGL3_Init("#version 430");

    double lastTime = glfwGetTime();
    double fpsTimer = lastTime;
    int    fpsFrames = 0;
    float  fps = 0.0f;
    int    frame = 0;
    while (!glfwWindowShouldClose(engine.window)) {
        glfwPollEvents();

        double now = glfwGetTime();
        float  dt  = float(now - lastTime);
        lastTime   = now;
        if (!S.paused && autoShot.empty()) simTime += dt;
        if (S.autoOrbit) camera.azimuthT += dt * S.orbitRate;
        camera.tick(autoShot.empty() ? dt : 1.0f);

        fpsFrames++;
        if (now - fpsTimer > 0.5) {
            fps = float(fpsFrames / (now - fpsTimer));
            fpsTimer = now;
            fpsFrames = 0;
        }

        // Gravity
        for (auto& obj : objects) {
            for (auto& obj2 : objects) {
                if (&obj == &obj2) continue; // skip self-interaction
                float dx = obj2.posRadius.x - obj.posRadius.x;
                float dy = obj2.posRadius.y - obj.posRadius.y;
                float dz = obj2.posRadius.z - obj.posRadius.z;
                float distance = sqrt(dx * dx + dy * dy + dz * dz);
                if (distance > 0 && Gravity) {
                    double Gforce = (G * obj.mass * obj2.mass) / (distance * distance);
                    double acc1 = Gforce / obj.mass;
                    obj.velocity += vec3(dx, dy, dz) / distance * float(acc1);
                    obj.posRadius += vec4(obj.velocity, 0.0f);
                }
            }
        }

        // ---------- RUN RAYTRACER + POST ------------- //
        engine.dispatchCompute(camera, simTime);
        engine.drawPost(simTime);

        // ---------- GRID (optional overlay) ------------- //
        if (S.showGrid) {
            int fbW, fbH;
            engine.outputSize(fbW, fbH);
            engine.generateGrid(objects);
            mat4 view = lookAt(camera.position(), camera.target, vec3(0, 1, 0));
            mat4 proj = perspective(radians(S.fov), float(fbW) / float(std::max(fbH, 1)), 1e9f, 1e14f);
            engine.drawGrid(proj * view);
        }

        // ---------- UI ------------- //
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        drawUI(fps);
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        if (screenshotRequested) {
            engine.saveScreenshot(timestampedName());
            screenshotRequested = false;
        }
        if (!autoShot.empty() && ++frame >= 3) {
            engine.saveScreenshot(autoShot);
            break;
        }

        glfwSwapBuffers(engine.window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(engine.window);
    glfwTerminate();
    return 0;
}

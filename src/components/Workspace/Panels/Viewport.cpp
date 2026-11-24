#include "Viewport.h"

#include <juce_opengl/juce_opengl.h>
#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

//==============================================================
// VECTOR MATH
//==============================================================

namespace
{
    constexpr float kPi = juce::MathConstants<float>::pi;
    constexpr float kObjectRadius = 0.22f;        // visual sphere radius (world units)
    constexpr float kPickRadius   = 0.30f;        // click radius

    struct Vec3 { float x, y, z; };

    Vec3 operator+(Vec3 a, Vec3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
    Vec3 operator-(Vec3 a, Vec3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
    Vec3 operator*(Vec3 v, float s) { return { v.x * s, v.y * s, v.z * s }; }

    float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

    Vec3 cross(Vec3 a, Vec3 b)
    {
        return { a.y * b.z - a.z * b.y,
                 a.z * b.x - a.x * b.z,
                 a.x * b.y - a.y * b.x };
    }

    float length(Vec3 v) { return std::sqrt(dot(v, v)); }

    Vec3 normalize(Vec3 v)
    {
        const float len = length(v);
        if (len < 0.00001f)
            return { 0.0f, 0.0f, 0.0f };
        return v * (1.0f / len);
    }

    Vec3 getForward(float yaw, float pitch)
    {
        return normalize({ std::sin(yaw) * std::cos(pitch),
                           std::sin(pitch),
                           -std::cos(yaw) * std::cos(pitch) });
    }

    Vec3 v3(const float (&a)[3]) { return { a[0], a[1], a[2] }; }

    //----------------------------------------------------------
    // 3x3 rotation (columns) + Euler (degrees) -> matrix.
    // Convention: Unity-style, R = Ry * Rx * Rz (Z applied first).
    // If your audio engine uses a different order, change ONLY this.
    //----------------------------------------------------------
    struct Mat3 { Vec3 c[3]; };

    Vec3 apply(const Mat3& m, Vec3 v) { return m.c[0] * v.x + m.c[1] * v.y + m.c[2] * v.z; }

    Mat3 mul(const Mat3& a, const Mat3& b) { return { { apply(a, b.c[0]), apply(a, b.c[1]), apply(a, b.c[2]) } }; }

    Mat3 eulerToMat3(const float (&deg)[3])
    {
        const float rx = deg[0] * kPi / 180.0f;
        const float ry = deg[1] * kPi / 180.0f;
        const float rz = deg[2] * kPi / 180.0f;

        const float cx = std::cos(rx), sx = std::sin(rx);
        const float cy = std::cos(ry), sy = std::sin(ry);
        const float cz = std::cos(rz), sz = std::sin(rz);

        const Mat3 Rx = { { { 1, 0, 0 }, { 0, cx, sx }, { 0, -sx, cx } } };
        const Mat3 Ry = { { { cy, 0, -sy }, { 0, 1, 0 }, { sy, 0, cy } } };
        const Mat3 Rz = { { { cz, sz, 0 }, { -sz, cz, 0 }, { 0, 0, 1 } } };

        return mul(Ry, mul(Rx, Rz));
    }

    juce::Matrix3D<float> makeMatrix(Vec3 c0, Vec3 c1, Vec3 c2, Vec3 t, float s = 1.0f)
    {
        const float v[16] =
        {
            c0.x * s, c0.y * s, c0.z * s, 0.0f,
            c1.x * s, c1.y * s, c1.z * s, 0.0f,
            c2.x * s, c2.y * s, c2.z * s, 0.0f,
            t.x,      t.y,      t.z,      1.0f
        };
        return juce::Matrix3D<float>(v);
    }

    Vec3 axisDir(int a)
    {
        return a == 0 ? Vec3{ 1, 0, 0 } : a == 1 ? Vec3{ 0, 1, 0 } : Vec3{ 0, 0, 1 };
    }

    // Proper rotation sending local +X to world axis `a` (cyclic permutation).
    Mat3 axisBasis(int a)
    {
        if (a == 0) return { { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } } };
        if (a == 1) return { { { 0, 1, 0 }, { 0, 0, 1 }, { 1, 0, 0 } } };
        return           { { { 0, 0, 1 }, { 1, 0, 0 }, { 0, 1, 0 } } };
    }

    // Right-handed in-plane basis for rotation about axis a (u x v = axis).
    void planeBasis(int a, Vec3& u, Vec3& v)
    {
        u = axisDir((a + 1) % 3);
        v = axisDir((a + 2) % 3);
    }

    Vec3 axisColour(int a, bool highlighted)
    {
        if (highlighted) return { 1.0f, 0.90f, 0.30f };
        if (a == 0)      return { 0.90f, 0.25f, 0.25f };
        if (a == 1)      return { 0.40f, 0.85f, 0.30f };
        return                  { 0.30f, 0.50f, 0.95f };
    }

    // constant on-screen gizmo size
    float gizmoScale(Vec3 centre, Vec3 eye)
    {
        return juce::jmax(0.2f, length(centre - eye) * 0.14f);
    }

    // Param t along line (P + dir*t) that is closest to the ray (O + D*s). dir, D are unit.
    bool closestParamOnAxis(Vec3 P, Vec3 dir, Vec3 O, Vec3 D, float& tOut)
    {
        const Vec3  w0 = P - O;
        const float b  = dot(dir, D);
        const float d  = dot(dir, w0);
        const float e  = dot(D, w0);
        const float den = 1.0f - b * b;

        if (std::abs(den) < 1.0e-4f)
            return false;

        tOut = (b * e - d) / den;
        return true;
    }

    bool rayPlane(Vec3 O, Vec3 D, Vec3 P, Vec3 n, float& tOut)
    {
        const float den = dot(D, n);
        if (std::abs(den) < 1.0e-5f)
            return false;

        const float t = dot(P - O, n) / den;
        if (t < 0.0f)
            return false;

        tOut = t;
        return true;
    }
}


//==============================================================
// CONSTRUCTOR
//==============================================================

ViewportPanel::ViewportPanel()
    : Panel("3D VIEWPORT")
{
    openGLContext.setPreferredVersion({ 3, 2 });
    openGLContext.setPreferredProfile(juce::OpenGLProfile::core);

    openGLContext.setRenderer(this);
    openGLContext.setContinuousRepainting(true);
    openGLContext.setComponentPaintingEnabled(true);

    SettingsManager::get().addChangeListener(this);
    applySettings();

    // Focus button.
    addAndMakeVisible(focusCubeButton);
    focusCubeButton.setWantsKeyboardFocus(false);
    focusCubeButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff26384b));
    focusCubeButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    focusCubeButton.onClick = [this]()
    {
        resetCamera();
        grabKeyboardFocus();
    };

    // Gizmo mode buttons.
    for (auto* b : { &moveButton, &rotateButton })
    {
        addAndMakeVisible(*b);
        b->setWantsKeyboardFocus(false);
        b->setClickingTogglesState(true);
        b->setRadioGroupId(4711);
        b->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff26384b));
        b->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff3d6a99));
        b->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        b->setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    }

    moveButton.setToggleState(true, juce::dontSendNotification);
    moveButton.onClick   = [this]() { setGizmoMode(GizmoMode::move);   grabKeyboardFocus(); };
    rotateButton.onClick = [this]() { setGizmoMode(GizmoMode::rotate); grabKeyboardFocus(); };

    // Object label.
    objectLabel.setText("NO OBJECT SELECTED", juce::dontSendNotification);
    objectLabel.setFont(juce::Font(12.0f));
    objectLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    objectLabel.setColour(juce::Label::backgroundColourId, juce::Colour(0xdd171d25));
    objectLabel.setBorderSize(juce::BorderSize<int>(4, 8, 4, 8));
    objectLabel.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(objectLabel);

    // Controls label.
    controlsLabel.setText(
        "LMB: Select/Gizmo   RMB: Orbit   MMB: Pan   Wheel: Zoom   WASD/Q/E: Move   1/2: Move/Rotate   F: Focus   Del: Delete",
        juce::dontSendNotification);
    controlsLabel.setFont(juce::Font(11.0f));
    controlsLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    controlsLabel.setColour(juce::Label::backgroundColourId, juce::Colour(0xdd171d25));
    controlsLabel.setBorderSize(juce::BorderSize<int>(4, 8, 4, 8));
    controlsLabel.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(controlsLabel);

    setWantsKeyboardFocus(true);

    syncTimer.onTick = [this]() { syncScene(); };

    load_obj("C:/Users/devts/OneDrive/Desktop/Projects/3D Audio Workstation/Aether/models/head.obj", AudioListener::head_position);

    juce::OpenGLPixelFormat pixelFormat;
    pixelFormat.depthBufferBits = 24;
    pixelFormat.multisamplingLevel = 4;
    openGLContext.setPixelFormat(pixelFormat);

    openGLContext.attachTo(*this);
}


//==============================================================
// DESTRUCTOR
//==============================================================

ViewportPanel::~ViewportPanel()
{
    SettingsManager::get().removeChangeListener(this);
    syncTimer.stopTimer();
    syncTimer.onTick = nullptr;
    stopTimer();
    openGLContext.detach();
}


//==============================================================
// TIMELINE LINK
//==============================================================

void ViewportPanel::setTimeline(Timeline* newTimeline)
{
    if (timeline == newTimeline)
        return;

    timeline = newTimeline;
    hoveredObject = nullptr;
    dragObject = nullptr;
    activeAxis = hoveredAxis = -1;

    if (timeline != nullptr)
        syncTimer.startTimerHz(30);
    else
        syncTimer.stopTimer();

    syncScene();
}

// Builds a plain-data snapshot for the GL thread. Message thread only.
void ViewportPanel::syncScene()
{
    SceneSnapshot s;
    s.mode = (int) gizmoMode;
    s.highlightAxis = activeAxis >= 0 ? activeAxis : hoveredAxis;

    juce::String label = "NO OBJECT SELECTED";

    if (timeline != nullptr)
    {
        const auto sel  = timeline->getSelectedObject();
        const auto objs = timeline->getAudioObjects();

        for (int i = 0; i < (int) objs.size(); ++i)
        {
            const auto& o = objs[(size_t) i];
            if (o == nullptr)
                continue;

            SceneObject so;
            so.pos[0] = (float) o->position.x; so.pos[1] = (float) o->position.y; so.pos[2] = (float) o->position.z;
            so.rot[0] = (float) o->rotation.x; so.rot[1] = (float) o->rotation.y; so.rot[2] = (float) o->rotation.z;

            const auto c = timeline->getObjectColour(o);
            so.colour[0] = c.getFloatRed();
            so.colour[1] = c.getFloatGreen();
            so.colour[2] = c.getFloatBlue();

            so.selected = (o == sel);
            so.hovered  = (o == hoveredObject);
            so.muted    = o->muted;

            if (so.selected)
            {
                s.selectedIndex = (int) s.objects.size();
                label = "OBJECT - " + o->name.toUpperCase();
            }

            s.objects.push_back(so);
        }
    }

    objectLabel.setText(label, juce::dontSendNotification);

    const juce::ScopedLock sl(sceneLock);
    snapshot = std::move(s);
}

void ViewportPanel::setGizmoMode(GizmoMode newMode)
{
    gizmoMode = newMode;
    moveButton.setToggleState(newMode == GizmoMode::move, juce::dontSendNotification);
    rotateButton.setToggleState(newMode == GizmoMode::rotate, juce::dontSendNotification);
    syncScene();
}

void ViewportPanel::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    juce::ignoreUnused (source);
    applySettings();
}

void ViewportPanel::applySettings()
{
    auto& settings = SettingsManager::get();
    fieldOfViewDegrees.store ((float) juce::jlimit (30.0, 110.0,
                                        settings.getDouble (SettingsManager::Key::ViewportFov)));
    mouseSensitivity.store ((float) juce::jlimit (0.05, 1.0,
                                       settings.getDouble (SettingsManager::Key::MouseSensitivity)));
    moveSpeed.store ((float) juce::jlimit (1.0, 20.0,
                                settings.getDouble (SettingsManager::Key::MoveSpeed)));
    showGrid.store (settings.getBool (SettingsManager::Key::ShowGrid));
    invertMouseY.store (settings.getBool (SettingsManager::Key::InvertMouseY));
    openGLContext.triggerRepaint();
}


//==============================================================
// RESIZED
//==============================================================

void ViewportPanel::resized()
{
    Panel::resized();

    const int padding = 12;

    focusCubeButton.setBounds(getWidth() - 130 - padding, padding, 130, 30);
    rotateButton.setBounds(focusCubeButton.getX() - 6 - 90, padding, 90, 30);
    moveButton.setBounds(rotateButton.getX() - 6 - 90, padding, 90, 30);

    objectLabel.setBounds(padding, padding, 240, 28);

    controlsLabel.setBounds(padding, getHeight() - 32, juce::jmax(100, getWidth() - padding * 2), 24);

    openGLContext.triggerRepaint();
}


//==============================================================
// OPENGL INITIALIZATION
//==============================================================

void ViewportPanel::newOpenGLContextCreated()
{
    using namespace juce::gl;

    const char* vertexShader = R"(
        #version 150

        in vec3 position;
        in vec3 colour;

        uniform mat4 mvpMatrix;     // projection * view * model
        uniform mat4 modelMatrix;

        out vec3 vertexColour;
        out vec3 worldPos;

        void main()
        {
            vertexColour = colour;
            worldPos = (modelMatrix * vec4(position, 1.0)).xyz;
            gl_Position = mvpMatrix * vec4(position, 1.0);
        }
    )";

    const char* fragmentShader = R"(
        #version 150

        in vec3 vertexColour;
        in vec3 worldPos;

        uniform vec3  lightDir;     // direction TOWARD the light
        uniform vec3  cameraPos;
        uniform float lit;          // 0 = flat colour, 1 = flat-shaded lit, 2 = sphere-lit
        uniform vec3  tint;
        uniform vec3  centre;       // sphere centre (lit == 2)
        uniform float emissive;

        out vec4 fragmentColour;

        void main()
        {
            vec3 base = vertexColour * tint;

            if (lit < 0.5)
            {
                fragmentColour = vec4(base, 1.0);
                return;
            }

            vec3 n;
            if (lit > 1.5)
                n = normalize(worldPos - centre);
            else
                n = normalize(cross(dFdx(worldPos), dFdy(worldPos)));

            vec3 v = normalize(cameraPos - worldPos);

            if (dot(n, v) < 0.0)
                n = -n;

            vec3  l    = normalize(lightDir);
            float diff = max(dot(n, l), 0.0);
            float fill = max(dot(n, v), 0.0) * 0.25;
            float spec = pow(max(dot(n, normalize(l + v)), 0.0), 32.0) * 0.25;

            vec3 colour = base * (0.22 + diff * 0.85 + fill) + vec3(spec) + base * emissive;
            fragmentColour = vec4(colour, 1.0);
        }
    )";

    auto program = std::make_unique<juce::OpenGLShaderProgram>(openGLContext);

    if (!program->addVertexShader(vertexShader)
        || !program->addFragmentShader(fragmentShader)
        || !program->link())
    {
        DBG("Shader error: " << program->getLastError());
        return;
    }

    shaderProgram = std::move(program);

    positionAttributeLocation = glGetAttribLocation(shaderProgram->getProgramID(), "position");
    colourAttributeLocation   = glGetAttribLocation(shaderProgram->getProgramID(), "colour");

    auto makeUniform = [this](const char* name)
    {
        return std::make_unique<juce::OpenGLShaderProgram::Uniform>(*shaderProgram, name);
    };

    mvpUniform       = makeUniform("mvpMatrix");
    modelUniform     = makeUniform("modelMatrix");
    lightDirUniform  = makeUniform("lightDir");
    cameraPosUniform = makeUniform("cameraPos");
    litUniform       = makeUniform("lit");
    tintUniform      = makeUniform("tint");
    centreUniform    = makeUniform("centre");
    emissiveUniform  = makeUniform("emissive");

    if (positionAttributeLocation < 0
        || colourAttributeLocation < 0
        || mvpUniform->uniformID < 0)
    {
        DBG("Shader attribute or uniform not found.");
        shaderProgram.reset();
        mvpUniform.reset();
        return;
    }

    createSceneBuffers();
}


//==============================================================
// CREATE SCENE BUFFERS
//==============================================================

void ViewportPanel::createSceneBuffers()
{
    using namespace juce::gl;

    std::vector<Vertex> vertices;

    auto addVertex = [&vertices](Vec3 position, juce::Colour colour)
    {
        vertices.push_back({
            { position.x, position.y, position.z },
            { colour.getFloatRed(), colour.getFloatGreen(), colour.getFloatBlue() }
        });
    };

    auto addLine = [&addVertex](Vec3 a, Vec3 b, juce::Colour colour)
    {
        addVertex(a, colour);
        addVertex(b, colour);
    };

    //==========================================================
    // GRID
    //==========================================================

    const juce::Colour minorGrid(0xff283440);
    const juce::Colour majorGrid(0xff394957);
    const juce::Colour xAxis(0xffbd6868);
    const juce::Colour zAxis(0xff6695c7);

    for (int i = -20; i <= 20; ++i)
    {
        const bool major = (i % 5 == 0);
        const auto colour = major ? majorGrid : minorGrid;

        addLine({ -20.0f, 0.0f, (float) i }, { 20.0f, 0.0f, (float) i }, i == 0 ? xAxis : colour);
        addLine({ (float) i, 0.0f, -20.0f }, { (float) i, 0.0f, 20.0f }, i == 0 ? zAxis : colour);
    }

    gridVertexCount = (int) vertices.size();

    //==========================================================
    // HEAD MODEL
    //==========================================================

    modelFirstVertex = (int) vertices.size();
    vertices.insert(vertices.end(), modelVertices.begin(), modelVertices.end());
    modelVertexCount = (int) modelVertices.size();

    //==========================================================
    // PRIMITIVES (white; tinted per draw call)
    //==========================================================

    const juce::Colour white = juce::Colours::white;

    auto tri = [&](Vec3 a, Vec3 b, Vec3 c)
    {
        addVertex(a, white); addVertex(b, white); addVertex(c, white);
    };

    auto quad = [&](Vec3 a, Vec3 b, Vec3 c, Vec3 d)
    {
        tri(a, b, c);
        tri(a, c, d);
    };

    // Unit sphere
    {
        sphereMesh.first = (int) vertices.size();
        const int stacks = 16, slices = 24;

        auto sp = [&](int i, int j)
        {
            const float th = kPi * (float) i / (float) stacks;
            const float ph = 2.0f * kPi * (float) j / (float) slices;
            return Vec3{ std::sin(th) * std::cos(ph), std::cos(th), std::sin(th) * std::sin(ph) };
        };

        for (int i = 0; i < stacks; ++i)
            for (int j = 0; j < slices; ++j)
                quad(sp(i, j), sp(i + 1, j), sp(i + 1, j + 1), sp(i, j + 1));

        sphereMesh.count = (int) vertices.size() - sphereMesh.first;
    }

    // Arrow along +X (length 1)
    {
        arrowMesh.first = (int) vertices.size();
        const int seg = 12;
        const float shaftEnd = 0.78f, shaftR = 0.028f, coneR = 0.085f;

        auto cp = [&](float x, float r, int k)
        {
            const float a = 2.0f * kPi * (float) k / (float) seg;
            return Vec3{ x, r * std::cos(a), r * std::sin(a) };
        };

        for (int k = 0; k < seg; ++k)
        {
            quad(cp(0.0f, shaftR, k), cp(shaftEnd, shaftR, k), cp(shaftEnd, shaftR, k + 1), cp(0.0f, shaftR, k + 1));
            tri(cp(shaftEnd, coneR, k), cp(shaftEnd, coneR, k + 1), { 1.0f, 0.0f, 0.0f });
            tri({ shaftEnd, 0.0f, 0.0f }, cp(shaftEnd, coneR, k + 1), cp(shaftEnd, coneR, k));
        }

        arrowMesh.count = (int) vertices.size() - arrowMesh.first;
    }

    // Ring in the YZ plane (normal +X), radius 1
    {
        ringMesh.first = (int) vertices.size();
        const int majorSeg = 64, minorSeg = 6;
        const float rm = 0.018f;

        auto tp = [&](int i, int k)
        {
            const float u = 2.0f * kPi * (float) i / (float) majorSeg;
            const float v = 2.0f * kPi * (float) k / (float) minorSeg;
            const Vec3 radial = { 0.0f, std::cos(u), std::sin(u) };
            return radial * (1.0f + rm * std::cos(v)) + Vec3{ rm * std::sin(v), 0.0f, 0.0f };
        };

        for (int i = 0; i < majorSeg; ++i)
            for (int k = 0; k < minorSeg; ++k)
                quad(tp(i, k), tp(i + 1, k), tp(i + 1, k + 1), tp(i, k + 1));

        ringMesh.count = (int) vertices.size() - ringMesh.first;
    }

    //==========================================================
    // VAO AND VBO
    //==========================================================

    glGenVertexArrays(1, &vertexArrayObject);
    glBindVertexArray(vertexArrayObject);

    glGenBuffers(1, &vertexBufferObject);
    glBindBuffer(GL_ARRAY_BUFFER, vertexBufferObject);

    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(vertices.size() * sizeof(Vertex)),
                 vertices.data(),
                 GL_STATIC_DRAW);

    glVertexAttribPointer(static_cast<GLuint>(positionAttributeLocation), 3, GL_FLOAT, GL_FALSE,
                          sizeof(Vertex), nullptr);
    glEnableVertexAttribArray(static_cast<GLuint>(positionAttributeLocation));

    glVertexAttribPointer(static_cast<GLuint>(colourAttributeLocation), 3, GL_FLOAT, GL_FALSE,
                          sizeof(Vertex), reinterpret_cast<const void*>(offsetof(Vertex, colour)));
    glEnableVertexAttribArray(static_cast<GLuint>(colourAttributeLocation));

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}


//==============================================================
// CAMERA VIEW MATRIX
//==============================================================

juce::Matrix3D<float> ViewportPanel::createViewMatrix() const
{
    const Vec3 forward  = getForward(cameraYaw, cameraPitch);
    const Vec3 worldUp  = { 0.0f, 1.0f, 0.0f };
    const Vec3 right    = normalize(cross(forward, worldUp));
    const Vec3 cameraUp = cross(right, forward);

    const Vec3 eye = Vec3{ pivotX, pivotY, pivotZ } - forward * cameraDistance;

    const float values[16] =
    {
        right.x,   cameraUp.x,  -forward.x,  0.0f,
        right.y,   cameraUp.y,  -forward.y,  0.0f,
        right.z,   cameraUp.z,  -forward.z,  0.0f,
        -dot(right, eye), -dot(cameraUp, eye), dot(forward, eye), 1.0f
    };

    return juce::Matrix3D<float>(values);
}

void ViewportPanel::resetCamera()
{
    {
        const juce::ScopedLock lock(cameraLock);
        pivotX = 0.0f;
        pivotY = 0.0f;
        pivotZ = 0.0f;
        cameraYaw = 0.0f;
        cameraPitch = -0.18f;
        cameraDistance = 8.0f;
    }

    openGLContext.triggerRepaint();
}

void ViewportPanel::focusSelectedObject()
{
    if (timeline == nullptr)
        return;

    const auto sel = timeline->getSelectedObject();
    if (sel == nullptr)
        return;

    {
        const juce::ScopedLock lock(cameraLock);
        pivotX = (float) sel->position.x;
        pivotY = (float) sel->position.y;
        pivotZ = (float) sel->position.z;
    }

    openGLContext.triggerRepaint();
}


//==============================================================
// OPENGL RENDER
//==============================================================

void ViewportPanel::renderOpenGL()
{
    using namespace juce::gl;

    const int width = getWidth();
    const int height = getHeight();

    if (width <= 0 || height <= 0)
        return;

    const float scale = static_cast<float>(openGLContext.getRenderingScale());

    glViewport(0, 0, juce::roundToInt(width * scale), juce::roundToInt(height * scale));

    glClearColor(0.055f, 0.067f, 0.080f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_CULL_FACE);

    if (shaderProgram == nullptr || mvpUniform == nullptr || vertexArrayObject == 0
        || modelUniform == nullptr || tintUniform == nullptr || emissiveUniform == nullptr
        || centreUniform == nullptr)
        return;

    // Projection.
    const float aspect = static_cast<float>(width) / static_cast<float>(height);
    const float nearPlane = 0.1f;
    const float farPlane = 100.0f;
    const float fovRadians = fieldOfViewDegrees.load() * kPi / 180.0f;
    const float halfHeight = nearPlane * std::tan(fovRadians * 0.5f);
    const float halfWidth = halfHeight * aspect;

    const auto projection = juce::Matrix3D<float>::fromFrustum(-halfWidth, halfWidth,
                                                               -halfHeight, halfHeight,
                                                               nearPlane, farPlane);

    // View + eye position
    juce::Matrix3D<float> view;
    Vec3 eye;

    {
        const juce::ScopedLock lock(cameraLock);
        view = createViewMatrix();

        const Vec3 f = getForward(cameraYaw, cameraPitch);
        eye = { pivotX - f.x * cameraDistance,
                pivotY - f.y * cameraDistance,
                pivotZ - f.z * cameraDistance };
    }

    const auto viewProj = projection * view;

    SceneSnapshot snap;
    {
        const juce::ScopedLock sl(sceneLock);
        snap = snapshot;
    }

    shaderProgram->use();

    lightDirUniform->set(0.45f, 0.85f, 0.35f);
    cameraPosUniform->set(eye.x, eye.y, eye.z);

    glBindVertexArray(vertexArrayObject);

    const juce::Matrix3D<float> identity;
    const Vec3 white = { 1.0f, 1.0f, 1.0f };

    auto drawMesh = [&](const MeshRange& m, const juce::Matrix3D<float>& model,
                        Vec3 tint, float lit, float emissive, Vec3 centre)
    {
        const auto mvp = viewProj * model;
        mvpUniform->setMatrix4(mvp.mat, 1, GL_FALSE);
        modelUniform->setMatrix4(model.mat, 1, GL_FALSE);
        tintUniform->set(tint.x, tint.y, tint.z);
        centreUniform->set(centre.x, centre.y, centre.z);
        emissiveUniform->set(emissive);
        litUniform->set(lit);
        glDrawArrays(GL_TRIANGLES, m.first, m.count);
    };

    // grid: unlit
    mvpUniform->setMatrix4(viewProj.mat, 1, GL_FALSE);
    modelUniform->setMatrix4(identity.mat, 1, GL_FALSE);
    tintUniform->set(1.0f, 1.0f, 1.0f);
    emissiveUniform->set(0.0f);
    litUniform->set(0.0f);
    if (showGrid.load())
        glDrawArrays(GL_LINES, 0, gridVertexCount);

    // head model: lit
    if (modelVertexCount > 0)
        drawMesh({ modelFirstVertex, modelVertexCount }, identity, white, 1.0f, 0.0f, { 0, 0, 0 });

    // audio objects
    const Vec3 ex = { 1, 0, 0 }, ey = { 0, 1, 0 }, ez = { 0, 0, 1 };

    for (const auto& o : snap.objects)
    {
        const Vec3 p = v3(o.pos);
        float k = o.muted ? 0.35f : 1.0f;
        const Vec3 tint = { o.colour[0] * k, o.colour[1] * k, o.colour[2] * k };
        const float glow = o.selected ? 0.30f : (o.hovered ? 0.18f : 0.0f);

        drawMesh(sphereMesh, makeMatrix(ex, ey, ez, p, kObjectRadius), tint, 2.0f, glow, p);

        // orientation pointer: arrow along the object's local +Z
        const Mat3 R = eulerToMat3(o.rot);
        const Vec3 pointerTint = o.muted ? Vec3{ 0.25f, 0.3f, 0.35f } : Vec3{ 0.75f, 0.92f, 1.0f };
        drawMesh(arrowMesh, makeMatrix(R.c[2], R.c[0], R.c[1], p, 0.6f), pointerTint, 0.0f, 0.0f, p);
    }

    // gizmo for the selected object, drawn on top of everything
    if (snap.selectedIndex >= 0)
    {
        glClear(GL_DEPTH_BUFFER_BIT);

        const auto& o = snap.objects[(size_t) snap.selectedIndex];
        const Vec3 c = v3(o.pos);
        const float s = gizmoScale(c, eye);

        for (int a = 0; a < 3; ++a)
        {
            const Mat3 B = axisBasis(a);
            const Vec3 col = axisColour(a, a == snap.highlightAxis);
            const auto model = makeMatrix(B.c[0], B.c[1], B.c[2], c, s);

            drawMesh(snap.mode == (int) GizmoMode::move ? arrowMesh : ringMesh,
                     model, col, 0.0f, 0.0f, c);
        }
    }

    glBindVertexArray(0);
}


//==============================================================
// OPENGL CLEANUP
//==============================================================

void ViewportPanel::openGLContextClosing()
{
    using namespace juce::gl;

    mvpUniform.reset();
    modelUniform.reset();
    shaderProgram.reset();
    lightDirUniform.reset();
    cameraPosUniform.reset();
    litUniform.reset();
    tintUniform.reset();
    centreUniform.reset();
    emissiveUniform.reset();

    positionAttributeLocation = -1;
    colourAttributeLocation = -1;

    if (vertexArrayObject != 0)
    {
        glDeleteVertexArrays(1, &vertexArrayObject);
        vertexArrayObject = 0;
    }

    if (vertexBufferObject != 0)
    {
        glDeleteBuffers(1, &vertexBufferObject);
        vertexBufferObject = 0;
    }

    gridVertexCount = 0;
    modelFirstVertex = 0;
    modelVertexCount = 0;
    sphereMesh = {};
    arrowMesh = {};
    ringMesh = {};
}


//==============================================================
// PICKING
//==============================================================

ViewportPanel::Ray3 ViewportPanel::makeMouseRay(juce::Point<int> p)
{
    float yaw, pitch, dist, px, py, pz;

    {
        const juce::ScopedLock lock(cameraLock);
        yaw = cameraYaw; pitch = cameraPitch; dist = cameraDistance;
        px = pivotX; py = pivotY; pz = pivotZ;
    }

    const Vec3 f     = getForward(yaw, pitch);
    const Vec3 right = normalize(cross(f, { 0.0f, 1.0f, 0.0f }));
    const Vec3 up    = cross(right, f);
    const Vec3 eye   = Vec3{ px, py, pz } - f * dist;

    const float w = (float) juce::jmax(1, getWidth());
    const float h = (float) juce::jmax(1, getHeight());

    const float ndcX = 2.0f * (float) p.x / w - 1.0f;
    const float ndcY = 1.0f - 2.0f * (float) p.y / h;
    const float tanHalf = std::tan (fieldOfViewDegrees.load() * kPi / 360.0f);

    const Vec3 dir = normalize(f + right * (ndcX * tanHalf * (w / h)) + up * (ndcY * tanHalf));

    return { { eye.x, eye.y, eye.z }, { dir.x, dir.y, dir.z } };
}

int ViewportPanel::pickAxis(juce::Point<int> p)
{
    if (timeline == nullptr)
        return -1;

    const auto sel = timeline->getSelectedObject();
    if (sel == nullptr)
        return -1;

    const auto ray = makeMouseRay(p);
    const Vec3 O = v3(ray.o), D = v3(ray.d);
    const Vec3 c = { (float) sel->position.x, (float) sel->position.y, (float) sel->position.z };
    const float s = gizmoScale(c, O);

    int best = -1;
    float bestErr = 1.0e9f;

    for (int a = 0; a < 3; ++a)
    {
        float err = 1.0e9f, limit = 0.0f;

        if (gizmoMode == GizmoMode::move)
        {
            float t;
            if (! closestParamOnAxis(c, axisDir(a), O, D, t))
                continue;

            t = juce::jlimit(0.0f, s, t);
            const Vec3 q = c + axisDir(a) * t;
            err = length(cross(q - O, D));
            limit = s * 0.09f;
        }
        else
        {
            float t;
            if (! rayPlane(O, D, c, axisDir(a), t))
                continue;

            err = std::abs(length(O + D * t - c) - s);
            limit = s * 0.10f;
        }

        if (err < limit && err < bestErr)
        {
            bestErr = err;
            best = a;
        }
    }

    return best;
}

AudioObject::Ptr ViewportPanel::pickObject(juce::Point<int> p)
{
    if (timeline == nullptr)
        return nullptr;

    const auto ray = makeMouseRay(p);
    const Vec3 O = v3(ray.o), D = v3(ray.d);

    AudioObject::Ptr best;
    float bestT = 1.0e9f;

    for (const auto& o : timeline->getAudioObjects())
    {
        if (o == nullptr)
            continue;

        const Vec3 c = { (float) o->position.x, (float) o->position.y, (float) o->position.z };
        const Vec3 oc = O - c;
        const float b = dot(oc, D);
        const float cc = dot(oc, oc) - kPickRadius * kPickRadius;
        const float disc = b * b - cc;

        if (disc < 0.0f)
            continue;

        float t = -b - std::sqrt(disc);
        if (t < 0.0f) t = -b + std::sqrt(disc);
        if (t < 0.0f) continue;

        if (t < bestT)
        {
            bestT = t;
            best = o;
        }
    }

    return best;
}


//==============================================================
// GIZMO DRAGGING
//==============================================================

void ViewportPanel::beginGizmoDrag(int axis, juce::Point<int> p)
{
    if (timeline == nullptr)
        return;

    dragObject = timeline->getSelectedObject();
    if (dragObject == nullptr)
        return;

    dragStartPos[0] = (float) dragObject->position.x;
    dragStartPos[1] = (float) dragObject->position.y;
    dragStartPos[2] = (float) dragObject->position.z;
    dragStartRot[0] = (float) dragObject->rotation.x;
    dragStartRot[1] = (float) dragObject->rotation.y;
    dragStartRot[2] = (float) dragObject->rotation.z;

    dragStartParam = 0.0f;
    dragLastAngle = 0.0f;
    dragAngleAccum = 0.0f;

    const auto ray = makeMouseRay(p);
    const Vec3 O = v3(ray.o), D = v3(ray.d);
    const Vec3 c = v3(dragStartPos);

    if (gizmoMode == GizmoMode::move)
    {
        float t;
        if (closestParamOnAxis(c, axisDir(axis), O, D, t))
            dragStartParam = t;
    }
    else
    {
        float t;
        if (rayPlane(O, D, c, axisDir(axis), t))
        {
            Vec3 u, v;
            planeBasis(axis, u, v);
            const Vec3 h = O + D * t - c;
            dragLastAngle = std::atan2(dot(h, v), dot(h, u));
        }
    }
}

void ViewportPanel::updateGizmoDrag(juce::Point<int> p, bool snap)
{
    if (timeline == nullptr || dragObject == nullptr || activeAxis < 0)
        return;

    const auto ray = makeMouseRay(p);
    const Vec3 O = v3(ray.o), D = v3(ray.d);
    const Vec3 c = v3(dragStartPos);
    const int a = activeAxis;

    if (gizmoMode == GizmoMode::move)
    {
        float t;
        if (! closestParamOnAxis(c, axisDir(a), O, D, t))
            return;

        float delta = t - dragStartParam;
        if (snap)
            delta = std::round(delta / 0.25f) * 0.25f;

        const Vec3 np = c + axisDir(a) * delta;
        timeline->setObjectPosition(dragObject, glm::vec3 { np.x, np.y, np.z });
    }
    else
    {
        float t;
        if (! rayPlane(O, D, c, axisDir(a), t))
            return;

        Vec3 u, v;
        planeBasis(a, u, v);
        const Vec3 h = O + D * t - c;
        const float ang = std::atan2(dot(h, v), dot(h, u));

        float diff = ang - dragLastAngle;
        while (diff >  kPi) diff -= 2.0f * kPi;
        while (diff < -kPi) diff += 2.0f * kPi;

        dragAngleAccum += diff;
        dragLastAngle = ang;

        float deg = dragAngleAccum * 180.0f / kPi;
        if (snap)
            deg = std::round(deg / 15.0f) * 15.0f;

        float r[3] = { dragStartRot[0], dragStartRot[1], dragStartRot[2] };
        r[a] += deg;

        timeline->setObjectRotation(dragObject, glm::vec3 { r[0], r[1], r[2] });
    }

    syncScene();
}


//==============================================================
// MOUSE
//==============================================================

void ViewportPanel::mouseDown(const juce::MouseEvent& event)
{
    grabKeyboardFocus();

    lastMousePosition = event.getPosition();

    if (! event.mods.isLeftButtonDown() || timeline == nullptr)
        return;

    // 1) gizmo handle of the selected object
    const int axis = pickAxis(event.getPosition());

    if (axis >= 0)
    {
        activeAxis = hoveredAxis = axis;
        beginGizmoDrag(axis, event.getPosition());
        syncScene();
        return;
    }

    // 2) an object sphere (or empty space -> deselect)
    timeline->selectObject(pickObject(event.getPosition()));
    syncScene();
}

void ViewportPanel::mouseUp(const juce::MouseEvent& event)
{
    if (activeAxis >= 0)
    {
        activeAxis = -1;
        dragObject = nullptr;
        hoveredAxis = pickAxis(event.getPosition());
        syncScene();
    }
}

void ViewportPanel::mouseMove(const juce::MouseEvent& event)
{
    const int axis = pickAxis(event.getPosition());
    hoveredAxis = axis;
    hoveredObject = axis >= 0 ? AudioObject::Ptr() : pickObject(event.getPosition());
    syncScene();
}

void ViewportPanel::mouseExit(const juce::MouseEvent&)
{
    hoveredAxis = -1;
    hoveredObject = nullptr;
    syncScene();
}

void ViewportPanel::mouseDrag(const juce::MouseEvent& event)
{
    if (activeAxis >= 0 && event.mods.isLeftButtonDown())
    {
        updateGizmoDrag(event.getPosition(), event.mods.isCtrlDown());
        return;
    }

    const bool rightDragging  = event.mods.isRightButtonDown();
    const bool middleDragging = event.mods.isMiddleButtonDown();

    if (!rightDragging && !middleDragging)
        return;

    const auto currentPosition = event.getPosition();
    const auto delta = currentPosition - lastMousePosition;
    lastMousePosition = currentPosition;

    const juce::ScopedLock lock(cameraLock);

    // ORBIT
    if (rightDragging && !event.mods.isShiftDown())
    {
        const float sensitivity = 0.005f * mouseSensitivity.load() / 0.25f;

        cameraYaw   += static_cast<float>(delta.x) * sensitivity;
        const float verticalDirection = invertMouseY.load() ? 1.0f : -1.0f;
        cameraPitch += static_cast<float>(delta.y) * sensitivity * verticalDirection;
        cameraPitch = juce::jlimit(-1.5f, 1.5f, cameraPitch);
    }
    // PAN
    else
    {
        const Vec3 forward  = getForward(cameraYaw, cameraPitch);
        const Vec3 right    = normalize(cross(forward, { 0.0f, 1.0f, 0.0f }));
        const Vec3 cameraUp = cross(right, forward);

        const float fovRadians = fieldOfViewDegrees.load() * kPi / 180.0f;
        const float panSpeed = 2.0f * cameraDistance * std::tan(fovRadians * 0.5f)
                               / (float) juce::jmax(1, getHeight());

        const Vec3 movement = right    * (-static_cast<float>(delta.x) * panSpeed)
                            + cameraUp * ( static_cast<float>(delta.y) * panSpeed);

        pivotX += movement.x;
        pivotY += movement.y;
        pivotZ += movement.z;
    }

    openGLContext.triggerRepaint();
}

void ViewportPanel::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    juce::ignoreUnused(event);

    const juce::ScopedLock lock(cameraLock);

    const float zoomFactor = std::exp(-wheel.deltaY * 0.4f);
    cameraDistance *= zoomFactor;
    cameraDistance = juce::jlimit(0.5f, 100.0f, cameraDistance);

    openGLContext.triggerRepaint();
}


//==============================================================
// KEYBOARD
//==============================================================

bool ViewportPanel::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        if (timeline != nullptr)
            if (auto sel = timeline->getSelectedObject())
            {
                timeline->removeObject(sel);
                return true;
            }

        return false;
    }

    const int code = juce::CharacterFunctions::toUpperCase((juce::juce_wchar) key.getKeyCode());

    if (code == '1') { setGizmoMode(GizmoMode::move);   return true; }
    if (code == '2') { setGizmoMode(GizmoMode::rotate); return true; }
    if (code == 'F') { focusSelectedObject();           return true; }

    if (code == 'W' || code == 'A' || code == 'S' || code == 'D' || code == 'Q' || code == 'E')
    {
        if (!isTimerRunning())
        {
            lastTickMs = juce::Time::getMillisecondCounterHiRes();
            startTimerHz(60);
        }
        return true;
    }

    return false;
}

void ViewportPanel::timerCallback()
{
    auto down = [](char c)
    {
        return juce::KeyPress::isKeyCurrentlyDown(c)
            || juce::KeyPress::isKeyCurrentlyDown(juce::CharacterFunctions::toUpperCase(c));
    };

    const bool w = down('w'), s = down('s'), a = down('a'),
               d = down('d'), q = down('q'), e = down('e');

    if (!w && !s && !a && !d && !q && !e)
    {
        stopTimer();
        return;
    }

    const double now = juce::Time::getMillisecondCounterHiRes();
    const float  dt  = juce::jlimit(0.0f, 0.1f, (float) ((now - lastTickMs) * 0.001));
    lastTickMs = now;

    const juce::ScopedLock lock(cameraLock);

    const Vec3 forward = { std::sin(cameraYaw), 0.0f, -std::cos(cameraYaw) };
    const Vec3 right   = { std::cos(cameraYaw), 0.0f,  std::sin(cameraYaw) };

    Vec3 movement = { 0.0f, 0.0f, 0.0f };

    if (w) movement = movement + forward;
    if (s) movement = movement - forward;
    if (d) movement = movement + right;
    if (a) movement = movement - right;
    if (e) movement.y += 1.0f;
    if (q) movement.y -= 1.0f;

    movement = normalize(movement);

    float speed = moveSpeed.load();

    if (juce::ModifierKeys::getCurrentModifiers().isShiftDown())
        speed *= 3.0f;

    const Vec3 step = movement * (speed * dt);

    pivotX += step.x;
    pivotY += step.y;
    pivotZ += step.z;

    openGLContext.triggerRepaint();
}
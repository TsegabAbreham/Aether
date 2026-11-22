#pragma once

#include <JuceHeader.h>
#include <glm/glm.hpp>

#include "Panel.h"
#include "Timeline.h"
#include "../../../Audio/Object/AudioListener.h"          

#include <functional>
#include <string>
#include <memory>
#include <vector>

class ViewportPanel : public Panel,
                      private juce::OpenGLRenderer,
                      private juce::Timer
{
public:
    ViewportPanel();
    ~ViewportPanel() override;

    // Call once from the parent:  viewport.setTimeline(&timeline);
    // The Timeline must outlive this panel (declare it BEFORE the panel in the parent).
    void setTimeline(Timeline* newTimeline);

    void resized() override;

    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent& event) override;

    void mouseWheelMove(
        const juce::MouseEvent& event,
        const juce::MouseWheelDetails& wheel
    ) override;

    bool keyPressed(const juce::KeyPress& key) override;

    bool load_obj(const std::string& filepath, glm::vec3 position);

private:

    //==========================================================
    // OPENGL
    //==========================================================

    juce::OpenGLContext openGLContext;

    void newOpenGLContextCreated() override;
    void renderOpenGL() override;
    void openGLContextClosing() override;

    void createSceneBuffers();

    struct Vertex
    {
        float position[3];
        float colour[3];
    };

    struct MeshRange
    {
        int first = 0;
        int count = 0;
    };

    GLuint vertexArrayObject = 0;
    GLuint vertexBufferObject = 0;

    GLint positionAttributeLocation = -1;
    GLint colourAttributeLocation = -1;

    int gridVertexCount = 0;

    int modelFirstVertex = 0;
    int modelVertexCount = 0;

    MeshRange sphereMesh;   // unit sphere
    MeshRange arrowMesh;    // arrow along +X, length 1
    MeshRange ringMesh;     // ring in the YZ plane, radius 1

    std::unique_ptr<juce::OpenGLShaderProgram> shaderProgram;

    std::unique_ptr<juce::OpenGLShaderProgram::Uniform> mvpUniform;
    std::unique_ptr<juce::OpenGLShaderProgram::Uniform> modelUniform;
    std::unique_ptr<juce::OpenGLShaderProgram::Uniform> lightDirUniform;
    std::unique_ptr<juce::OpenGLShaderProgram::Uniform> cameraPosUniform;
    std::unique_ptr<juce::OpenGLShaderProgram::Uniform> litUniform;
    std::unique_ptr<juce::OpenGLShaderProgram::Uniform> tintUniform;
    std::unique_ptr<juce::OpenGLShaderProgram::Uniform> centreUniform;
    std::unique_ptr<juce::OpenGLShaderProgram::Uniform> emissiveUniform;

    double lastTickMs = 0.0;

    std::vector<Vertex> modelVertices;


    //==========================================================
    // ORBIT CAMERA
    //==========================================================

    float pivotX = 0.0f;
    float pivotY = 0.0f;
    float pivotZ = 0.0f;

    float cameraYaw = 0.0f;
    float cameraPitch = -0.18f;
    float cameraDistance = 8.0f;

    juce::CriticalSection cameraLock;

    juce::Matrix3D<float> createViewMatrix() const;

    void resetCamera();
    void focusSelectedObject();


    //==========================================================
    // AUDIO OBJECTS + GIZMOS
    //==========================================================

    enum class GizmoMode { move = 0, rotate = 1 };

    // Plain data copied to the GL thread (the GL thread never touches the Timeline).
    struct SceneObject
    {
        float pos[3]    { 0, 0, 0 };
        float rot[3]    { 0, 0, 0 };   // degrees
        float colour[3] { 1, 1, 1 };
        bool selected = false;
        bool hovered  = false;
        bool muted    = false;
    };

    struct SceneSnapshot
    {
        std::vector<SceneObject> objects;
        int selectedIndex  = -1;
        int mode           = 0;
        int highlightAxis  = -1;
    };

    struct Ray3 { float o[3]; float d[3]; };

    struct SyncTimer : public juce::Timer
    {
        std::function<void()> onTick;
        void timerCallback() override { if (onTick) onTick(); }
    };

    Timeline* timeline = nullptr;
    SyncTimer syncTimer;                 // polls the Timeline (30 Hz) so Inspector edits show up

    SceneSnapshot snapshot;
    juce::CriticalSection sceneLock;

    GizmoMode gizmoMode = GizmoMode::move;

    AudioObject::Ptr hoveredObject;
    AudioObject::Ptr dragObject;
    int hoveredAxis = -1;
    int activeAxis  = -1;

    float dragStartPos[3]   { 0, 0, 0 };
    float dragStartRot[3]   { 0, 0, 0 };
    float dragStartParam    = 0.0f;
    float dragLastAngle     = 0.0f;
    float dragAngleAccum    = 0.0f;

    void syncScene();
    void setGizmoMode(GizmoMode newMode);

    Ray3 makeMouseRay(juce::Point<int> p);
    int  pickAxis(juce::Point<int> p);
    AudioObject::Ptr pickObject(juce::Point<int> p);
    void beginGizmoDrag(int axis, juce::Point<int> p);
    void updateGizmoDrag(juce::Point<int> p, bool snap);


    //==========================================================
    // MOUSE
    //==========================================================

    juce::Point<int> lastMousePosition;


    //==========================================================
    // KEYBOARD
    //==========================================================

    void timerCallback() override;


    //==========================================================
    // UI
    //==========================================================

    juce::TextButton focusCubeButton { "Focus Origin" };
    juce::TextButton moveButton      { "Move (1)" };
    juce::TextButton rotateButton    { "Rotate (2)" };

    juce::Label objectLabel;
    juce::Label controlsLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ViewportPanel)
};
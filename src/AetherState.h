#pragma once

#include <JuceHeader.h>
#include <glm/glm.hpp>

#include <map>
#include <string>
#include <vector>

struct AetherState
{
    static constexpr int currentVersion = 1;

    using Vector3 = glm::vec3;

    struct CameraState
    {
        float pivotX = 0.0f;
        float pivotY = 0.0f;
        float pivotZ = 0.0f;
        float yaw = 0.0f;
        float pitch = -0.18f;
        float distance = 8.0f;
    };

    struct ViewportState
    {
        CameraState camera;
        bool showGrid = true;
        float fieldOfViewDegrees = 60.0f;
    };

    struct AudioClipState
    {
        juce::Uuid id;
        juce::String name;
        juce::File file;
        double sampleRate = 44100.0;
        double sourceLengthSec = 0.0;
        double startSec = 0.0;
        double lengthSec = 0.0;
        float volume = 1.0f;
        float reverbMix = 0.0f;
        float speed = 1.0f;
        bool muted = false;
        Vector3 position { 0.0f, 0.0f, 0.0f };
        Vector3 rotation { 0.0f, 0.0f, 0.0f };
    };

    struct AudioTrackState
    {
        std::string id;
        std::string name;
        unsigned int colourArgb = 0;
        bool muted = false;
        bool solo = false;
        std::vector<std::string> objectIds;
        std::vector<AudioClipState> clips;
    };

    struct TimelineState
    {
        double pixelsPerSecond = 60.0;
        double viewStartSeconds = 0.0;
        double playheadSeconds = 0.0;
        int verticalScroll = 0;
        float roomAmount = 1.0f;
        std::vector<AudioTrackState> tracks;
    };

    struct KeyframeState
    {
        int id = 0;
        double time = 0.0;
        Vector3 pos { 0.0f, 0.0f, 0.0f };
        bool recorded = false;
    };

    struct ObjectAnimationState
    {
        std::vector<KeyframeState> keys;
    };

    struct AnimationState
    {
        std::map<juce::Uuid, ObjectAnimationState> tracks;
        int nextKeyId = 1;
        bool recording = false;
    };

    struct HierarchyState
    {
        std::vector<std::string> objectIds;
        std::string selectedObjectId;
    };

    struct ExplorerState
    {
        juce::File rootFolder;
        juce::File currentFolder;
        juce::File selectedFile;
        int iconSize = 84;
    };

    struct DockLayoutState
    {
        std::string id;
        int column = 0;
        int row = 0;
        double width = 0.0;
        double height = 0.0;
        std::string activePanelId;
        std::vector<std::string> panelIds;
    };

    struct PanelLayoutState
    {
        std::string id;
        std::string type;
        std::string name;
        std::string dockId;
        float x = 0.0f;
        float y = 0.0f;
        float width = 0.0f;
        float height = 0.0f;
        float scaleX = 1.0f;
        float scaleY = 1.0f;
    };

    struct WorkspaceState
    {
        std::vector<DockLayoutState> docks;
        std::vector<PanelLayoutState> panels;
    };

    std::string projectName;
    std::string projectFilePath;
    int version = currentVersion;
    ViewportState viewport;
    TimelineState timeline;
    AnimationState animation;
    HierarchyState hierarchy;
    ExplorerState explorer;
    WorkspaceState workspace;
};

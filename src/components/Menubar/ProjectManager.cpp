#include "ProjectManager.h"

#include <stdexcept>

using json = nlohmann::json;

namespace
{
    json toJson (const AetherState::Vector3& v)
    {
        return json::array ({ v.x, v.y, v.z });
    }

    json toJson (const juce::File& f)
    {
        return f == juce::File() ? "" : f.getFullPathName().toStdString();
    }

    json toJson (const AetherState::CameraState& c)
    {
        return {
            { "pivotX", c.pivotX }, { "pivotY", c.pivotY }, { "pivotZ", c.pivotZ },
            { "yaw", c.yaw }, { "pitch", c.pitch }, { "distance", c.distance }
        };
    }

    json toJson (const AetherState::ViewportState& v)
    {
        return {
            { "camera", toJson (v.camera) },
            { "showGrid", v.showGrid },
            { "fieldOfViewDegrees", v.fieldOfViewDegrees }
        };
    }

    json toJson (const AetherState::AudioClipState& c)
    {
        return {
            { "id", c.id.toString().toStdString() },
            { "name", c.name.toStdString() },
            { "file", toJson (c.file) },
            { "sampleRate", c.sampleRate },
            { "sourceLengthSec", c.sourceLengthSec },
            { "startSec", c.startSec },
            { "lengthSec", c.lengthSec },
            { "volume", c.volume },
            { "reverbMix", c.reverbMix },
            { "speed", c.speed },
            { "muted", c.muted },
            { "position", toJson (c.position) },
            { "rotation", toJson (c.rotation) }
        };
    }

    json toJson (const AetherState::AudioTrackState& t)
    {
        json clips = json::array();
        for (auto& c : t.clips)
            clips.push_back (toJson (c));

        return {
            { "id", t.id },
            { "name", t.name },
            { "colourArgb", t.colourArgb },
            { "muted", t.muted },
            { "solo", t.solo },
            { "objectIds", t.objectIds },
            { "clips", clips }
        };
    }

    json toJson (const AetherState::TimelineState& tl)
    {
        json tracks = json::array();
        for (auto& t : tl.tracks)
            tracks.push_back (toJson (t));

        return {
            { "pixelsPerSecond", tl.pixelsPerSecond },
            { "viewStartSeconds", tl.viewStartSeconds },
            { "playheadSeconds", tl.playheadSeconds },
            { "verticalScroll", tl.verticalScroll },
            { "roomAmount", tl.roomAmount },
            { "tracks", tracks }
        };
    }

    json toJson (const AetherState::KeyframeState& k)
    {
        return {
            { "id", k.id },
            { "time", k.time },
            { "pos", toJson (k.pos) },
            { "recorded", k.recorded }
        };
    }

    json toJson (const AetherState::AnimationState& a)
    {
        json tracks = json::object();   // keyed by object Uuid string
        for (auto& [uuid, objAnim] : a.tracks)
        {
            json keys = json::array();
            for (auto& k : objAnim.keys)
                keys.push_back (toJson (k));

            tracks[uuid.toString().toStdString()] = { { "keys", keys } };
        }

        return {
            { "tracks", tracks },
            { "nextKeyId", a.nextKeyId },
            { "recording", a.recording }
        };
    }

    json toJson (const AetherState::HierarchyState& h)
    {
        return {
            { "objectIds", h.objectIds },
            { "selectedObjectId", h.selectedObjectId }
        };
    }

    json toJson (const AetherState::ExplorerState& e)
    {
        return {
            { "rootFolder", toJson (e.rootFolder) },
            { "currentFolder", toJson (e.currentFolder) },
            { "selectedFile", toJson (e.selectedFile) },
            { "iconSize", e.iconSize }
        };
    }

    json toJson (const AetherState::WorkspaceState& w)
    {
        json docks = json::array();
        for (auto& d : w.docks)
            docks.push_back ({
                { "id", d.id }, { "column", d.column }, { "row", d.row },
                { "width", d.width }, { "height", d.height },
                { "activePanelId", d.activePanelId },
                { "panelIds", d.panelIds }
            });

        json panels = json::array();
        for (auto& p : w.panels)
            panels.push_back ({
                { "id", p.id }, { "type", p.type }, { "name", p.name },
                { "dockId", p.dockId },
                { "x", p.x }, { "y", p.y },
                { "width", p.width }, { "height", p.height },
                { "scaleX", p.scaleX }, { "scaleY", p.scaleY }
            });

        return { { "docks", docks }, { "panels", panels } };
    }
}

json ProjectManager::toJson (const AetherState& s)
{
    return {
        { "format", "aether-project" },
        { "version", s.version },
        { "projectName", s.projectName },
        { "viewport", ::toJson (s.viewport) },
        { "timeline", ::toJson (s.timeline) },
        { "animation", ::toJson (s.animation) },
        { "hierarchy", ::toJson (s.hierarchy) },
        { "explorer", ::toJson (s.explorer) },
        { "workspace", ::toJson (s.workspace) }
    };
}

bool ProjectManager::saveProject (const juce::File& file, const AetherState& state)
{
    if (! file.hasFileExtension(".atr") || ! file.getParentDirectory().isDirectory())
        return false;

    auto stateToSave = state;
    stateToSave.version = AetherState::currentVersion;
    stateToSave.projectFilePath = file.getFullPathName().toStdString();
    stateToSave.projectName = file.getFileNameWithoutExtension().toStdString();

    try
    {
        const auto text = toJson (stateToSave).dump (4);

        // replaceWithText writes via a temp file, so a failed save won't corrupt an existing project
        return file.replaceWithText (juce::String (text), false, false, "\n");
    }
    catch (const std::exception& e)
    {
        DBG ("saveProject failed: " << e.what());
        return false;
    }
}

namespace
{
    const json& obj (const json& j, const char* key)
    {
        static const json empty = json::object();
        if (! j.is_object() || ! j.contains (key))
            return empty;
        if (! j[key].is_object())
            throw std::runtime_error(std::string("Expected an object at '") + key + "'.");
        return j[key];
    }

    const json& arr (const json& j, const char* key)
    {
        static const json empty = json::array();
        if (! j.is_object() || ! j.contains (key))
            return empty;
        if (! j[key].is_array())
            throw std::runtime_error(std::string("Expected an array at '") + key + "'.");
        return j[key];
    }

    template <typename T>
    T val (const json& j, const char* key, T def)
    {
        if (! j.is_object() || ! j.contains (key))
            return def;
        return j[key].get<T>();
    }

    AetherState::Vector3 vec3From (const json& j, const char* key)
    {
        AetherState::Vector3 v { 0.0f, 0.0f, 0.0f };
        if (! j.is_object() || ! j.contains (key))
            return v;
        if (! j[key].is_array() || j[key].size() != 3)
            throw std::runtime_error(std::string("Expected a 3D vector at '") + key + "'.");
        v = { j[key][0].get<float>(), j[key][1].get<float>(), j[key][2].get<float>() };
        return v;
    }

    juce::File fileFrom (const json& j, const char* key)
    {
        auto path = val<std::string> (j, key, "");
        return path.empty() ? juce::File() : juce::File (juce::String (path));
    }

    AetherState::CameraState cameraFrom (const json& j)
    {
        AetherState::CameraState c;
        c.pivotX   = val (j, "pivotX", c.pivotX);
        c.pivotY   = val (j, "pivotY", c.pivotY);
        c.pivotZ   = val (j, "pivotZ", c.pivotZ);
        c.yaw      = val (j, "yaw", c.yaw);
        c.pitch    = val (j, "pitch", c.pitch);
        c.distance = val (j, "distance", c.distance);
        return c;
    }

    AetherState::AudioClipState clipFrom (const json& j)
    {
        AetherState::AudioClipState c;
        c.id              = juce::Uuid (juce::String (val<std::string> (j, "id", "")));
        c.name            = juce::String (val<std::string> (j, "name", ""));
        c.file            = fileFrom (j, "file");
        c.sampleRate      = val (j, "sampleRate", c.sampleRate);
        c.sourceLengthSec = val (j, "sourceLengthSec", c.sourceLengthSec);
        c.startSec        = val (j, "startSec", c.startSec);
        c.lengthSec       = val (j, "lengthSec", c.lengthSec);
        c.volume          = val (j, "volume", c.volume);
        c.reverbMix       = val (j, "reverbMix", c.reverbMix);
        c.speed           = val (j, "speed", c.speed);
        c.muted           = val (j, "muted", c.muted);
        c.position        = vec3From (j, "position");
        c.rotation        = vec3From (j, "rotation");
        return c;
    }

    AetherState::AudioTrackState trackFrom (const json& j)
    {
        AetherState::AudioTrackState t;
        t.id         = val<std::string> (j, "id", "");
        t.name       = val<std::string> (j, "name", "");
        t.colourArgb = val<unsigned int> (j, "colourArgb", 0u);
        t.muted      = val (j, "muted", false);
        t.solo       = val (j, "solo", false);
        t.objectIds  = val<std::vector<std::string>> (j, "objectIds", {});

        for (auto& c : arr (j, "clips"))
            t.clips.push_back (clipFrom (c));

        return t;
    }
}

AetherState ProjectManager::fromJson (const json& j)
{
    AetherState s;

    s.version     = val (j, "version", AetherState::currentVersion);
    s.projectName = val<std::string> (j, "projectName", "");

    // Viewport
    {
        auto& v = obj (j, "viewport");
        s.viewport.camera             = cameraFrom (obj (v, "camera"));
        s.viewport.showGrid           = val (v, "showGrid", s.viewport.showGrid);
        s.viewport.fieldOfViewDegrees = val (v, "fieldOfViewDegrees", s.viewport.fieldOfViewDegrees);
    }

    // Timeline
    {
        auto& t = obj (j, "timeline");
        s.timeline.pixelsPerSecond = val (t, "pixelsPerSecond", s.timeline.pixelsPerSecond);
        s.timeline.viewStartSeconds = val (t, "viewStartSeconds", s.timeline.viewStartSeconds);
        s.timeline.playheadSeconds = val (t, "playheadSeconds", s.timeline.playheadSeconds);
        s.timeline.verticalScroll  = val (t, "verticalScroll", s.timeline.verticalScroll);
        s.timeline.roomAmount      = val (t, "roomAmount", s.timeline.roomAmount);

        for (auto& tr : arr (t, "tracks"))
            s.timeline.tracks.push_back (trackFrom (tr));
    }

    // Animation
    {
        auto& a = obj (j, "animation");
        s.animation.nextKeyId = val (a, "nextKeyId", s.animation.nextKeyId);
        s.animation.recording = val (a, "recording", s.animation.recording);

        auto& tracks = obj (a, "tracks");
        for (auto it = tracks.begin(); it != tracks.end(); ++it)
        {
            AetherState::ObjectAnimationState oa;
            for (auto& k : arr (it.value(), "keys"))
            {
                AetherState::KeyframeState ks;
                ks.id       = val (k, "id", 0);
                ks.time     = val (k, "time", 0.0);
                ks.pos      = vec3From (k, "pos");
                ks.recorded = val (k, "recorded", false);
                oa.keys.push_back (ks);
            }
            s.animation.tracks[juce::Uuid (juce::String (it.key()))] = std::move (oa);
        }
    }

    // Hierarchy
    {
        auto& h = obj (j, "hierarchy");
        s.hierarchy.objectIds        = val<std::vector<std::string>> (h, "objectIds", {});
        s.hierarchy.selectedObjectId = val<std::string> (h, "selectedObjectId", "");
    }

    // Explorer
    {
        auto& e = obj (j, "explorer");
        s.explorer.rootFolder    = fileFrom (e, "rootFolder");
        s.explorer.currentFolder = fileFrom (e, "currentFolder");
        s.explorer.selectedFile  = fileFrom (e, "selectedFile");
        s.explorer.iconSize      = val (e, "iconSize", s.explorer.iconSize);
    }

    // Workspace
    {
        auto& w = obj (j, "workspace");

        for (auto& d : arr (w, "docks"))
        {
            AetherState::DockLayoutState dl;
            dl.id            = val<std::string> (d, "id", "");
            dl.column        = val (d, "column", 0);
            dl.row           = val (d, "row", 0);
            dl.width         = val (d, "width", 0.0);
            dl.height        = val (d, "height", 0.0);
            dl.activePanelId = val<std::string> (d, "activePanelId", "");
            dl.panelIds      = val<std::vector<std::string>> (d, "panelIds", {});
            s.workspace.docks.push_back (std::move (dl));
        }

        for (auto& p : arr (w, "panels"))
        {
            AetherState::PanelLayoutState pl;
            pl.id     = val<std::string> (p, "id", "");
            pl.type   = val<std::string> (p, "type", "");
            pl.name   = val<std::string> (p, "name", "");
            pl.dockId = val<std::string> (p, "dockId", "");
            pl.x      = val (p, "x", 0.0f);
            pl.y      = val (p, "y", 0.0f);
            pl.width  = val (p, "width", 0.0f);
            pl.height = val (p, "height", 0.0f);
            pl.scaleX = val (p, "scaleX", 1.0f);
            pl.scaleY = val (p, "scaleY", 1.0f);
            s.workspace.panels.push_back (std::move (pl));
        }
    }

    return s;
}

bool ProjectManager::loadProject (const juce::File& file,
                                  AetherState& outState,
                                  juce::String* errorMessage)
{
    auto fail = [&] (const juce::String& msg)
    {
        if (errorMessage != nullptr)
            *errorMessage = msg;
        return false;
    };

    if (! file.existsAsFile())
        return fail ("File does not exist.");
    if (! file.hasFileExtension(".atr"))
        return fail ("Project files must use the .atr extension.");

    try
    {
        auto j = json::parse (file.loadFileAsString().toStdString());

        if (! j.is_object() || val<std::string> (j, "format", "") != "aether-project")
            return fail ("This is not a valid Aether project file.");

        const int fileVersion = val (j, "version", 0);
        if (fileVersion > AetherState::currentVersion)
            return fail ("This project was saved with a newer version of Aether.");

        outState = fromJson (j);
        outState.projectFilePath = file.getFullPathName().toStdString();
        outState.projectName     = file.getFileNameWithoutExtension().toStdString();
        return true;
    }
    catch (const std::exception& e)
    {
        return fail (juce::String ("Could not parse project: ") + e.what());
    }
}
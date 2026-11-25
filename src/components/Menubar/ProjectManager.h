#pragma once

#include <JuceHeader.h>
#include "json.hpp"
#include "../../AetherState.h"

class ProjectManager
{
public:
    // Writes the full state to a JSON .atr file. Returns false on failure.
    bool saveProject (const juce::File& file, const AetherState& state);

    // Exposed in case you want to preview/debug the JSON
    static nlohmann::json toJson (const AetherState& state);

    // Reads an .atr file into outState. Returns false (and fills errorMessage) on failure.
    bool loadProject (const juce::File& file,
                    AetherState& outState,
                    juce::String* errorMessage = nullptr);

    static AetherState fromJson (const nlohmann::json& j);
};
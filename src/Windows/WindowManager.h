
#pragma once

#include <JuceHeader.h>
#include <memory>
#include "Window.h"


class WindowManager
{
public:
    WindowManager() = default;
    ~WindowManager();

    // Window creation
    void openSettings();

    // Window management
    void closeSettings();

    void closeAll();

private:
    std::unique_ptr<AetherWindow> settingsWindow;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WindowManager)
};

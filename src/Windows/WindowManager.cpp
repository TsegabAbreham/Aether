
#include "WindowManager.h"

#include "Settings/Setting.h"


WindowManager::~WindowManager()
{
    closeAll();
}

void WindowManager::openSettings (juce::AudioDeviceManager* deviceManager)
{
    if (!settingsWindow)
        settingsWindow = std::make_unique<SettingsWindow> (deviceManager);

    settingsWindow->setVisible(true);
    settingsWindow->toFront(true);
}

// Closing individual windows

void WindowManager::closeSettings()
{
    settingsWindow.reset();
}

// Close everything

void WindowManager::closeAll()
{
    settingsWindow.reset();
}

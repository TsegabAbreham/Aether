
#include "WindowManager.h"

#include "Settings/Setting.h"


WindowManager::~WindowManager()
{
    closeAll();
}

void WindowManager::openSettings()
{
    if (!settingsWindow)
        settingsWindow = std::make_unique<SettingsWindow>();

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

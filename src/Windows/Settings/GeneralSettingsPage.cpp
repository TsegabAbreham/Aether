#include "SettingsPageFactory.h"
#include "SettingsPageComponents.h"

namespace SettingsPages
{
    std::unique_ptr<juce::Component> makeGeneralPage()
    {
        using namespace SettingsUI;
        return makePanel ("General", {
            new ToggleRow ("Show splash screen on startup", Key::ShowSplash) });
    }
}

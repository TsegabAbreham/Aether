#include "SettingsPageFactory.h"
#include "SettingsPageComponents.h"

namespace SettingsPages
{
    std::unique_ptr<juce::Component> makeTimelinePage()
    {
        using namespace SettingsUI;
        return makePanel ("Timeline", {
            new ToggleRow ("Snap clips to grid", Key::TimelineSnap),
            new SliderRow ("Autosave interval (0 = off)", 0.0, 30.0, 1.0,
                           Key::AutosaveMinutes, " min") });
    }
}

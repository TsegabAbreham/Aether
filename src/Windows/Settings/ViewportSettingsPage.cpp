#include "SettingsPageFactory.h"
#include "SettingsPageComponents.h"

namespace SettingsPages
{
    std::unique_ptr<juce::Component> makeViewportPage()
    {
        using namespace SettingsUI;
        return makePanel ("Viewport", {
            new SliderRow ("Field of view", 30.0, 110.0, 1.0, Key::ViewportFov, " deg"),
            new SliderRow ("Mouse sensitivity", 0.05, 1.0, 0.01, Key::MouseSensitivity),
            new SliderRow ("Move speed (WASD)", 1.0, 20.0, 0.5, Key::MoveSpeed, " u/s"),
            new ToggleRow ("Show grid", Key::ShowGrid),
            new ToggleRow ("Invert mouse Y", Key::InvertMouseY) });
    }
}

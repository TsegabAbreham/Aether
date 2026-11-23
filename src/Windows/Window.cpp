#include "Window.h"

AetherWindow::AetherWindow(
    const juce::String& title,
    int width,
    int height
)
    : DocumentWindow(
        title,
        juce::Colour(0xff171923),
        DocumentWindow::allButtons
    )
{
    setResizable(true, true);
    centreWithSize(width, height);
}

void AetherWindow::closeButtonPressed()
{
    onClose();
}

void AetherWindow::onClose()
{
    setVisible(false);
}
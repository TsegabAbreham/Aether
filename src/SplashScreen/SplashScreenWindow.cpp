#include "SplashScreenWindow.h"

SplashScreenWindow::SplashScreenWindow()
    : juce::DocumentWindow("Aether", juce::Colour(0xff0d0e12), 0)   // 0 = no title-bar buttons
{
    setUsingNativeTitleBar(false);
    setTitleBarHeight(0);                       // borderless look
    setResizable(false, false);
    setAlwaysOnTop(true);

    setContentNonOwned(&splashScreen, true);    // window takes the content's 600x350 size
    centreWithSize(800, 550);

    setVisible(true);
}

SplashScreenWindow::~SplashScreenWindow()
{
    clearContentComponent();
}
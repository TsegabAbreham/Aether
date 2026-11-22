#include <JuceHeader.h>
#include "MainComponent.h"

#include "SplashScreen/SplashScreenWindow.h"

#include <functional>
#include <vector>

class AetherApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "Aether"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override          { return true; }

    //==========================================================================
    // Called when Aether starts
    void initialise(const juce::String& commandLine) override
    {
        juce::ignoreUnused(commandLine);

        splashWindow = std::make_unique<SplashScreenWindow>();

        steps = {
            { "Initialising...",        []  { /* quick setup (settings, logging, ...) */ } },
            { "Loading workspace...",   [this]
              {
                  // Created hidden; shown (maximised) once the splash finishes.
                  mainWindow = std::make_unique<MainWindow>(getApplicationName());
              } },
            { "Finalising...",          []  { /* anything that must run after the window exists */ } },
        };

        splashWindow->onFinished() = [this]
        {
            // Defer so the splash isn't destroyed while it is still inside its own callback
            juce::MessageManager::callAsync([this]
            {
                if (mainWindow != nullptr)
                    mainWindow->showWindow();

                splashWindow.reset();
            });
        };

        juce::Timer::callAfterDelay(150, [this] { runStep(0); });
    }

    void shutdown() override
    {
        mainWindow   = nullptr;
        splashWindow = nullptr;
    }

    //==========================================================================
    class MainWindow : public juce::DocumentWindow
    {
    public:
        explicit MainWindow(juce::String name)
            : juce::DocumentWindow(name,
                                   juce::Colours::darkgrey,
                                   juce::DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar(true);
            setContentOwned(new MainComponent(), true);

            setResizable(true, true);
            setResizeLimits(900, 600, 10000, 10000);

            // size it gets when the user un-maximises
            centreWithSize(1100, 700);

            // stays hidden until the splash screen is done
        }

        void showWindow()
        {
            setVisible(true);
            setFullScreen(true);      // maximised, title bar stays
            toFront(true);
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
    };

private:
    //==========================================================================
    struct Step
    {
        juce::String label;
        std::function<void()> work;
    };

    void runStep(size_t index)
    {
        if (splashWindow == nullptr)
            return;

        if (index >= steps.size())
        {
            splashWindow->setProgress(1.0, "Ready");
            return;
        }

        splashWindow->setProgress((double) index / (double) steps.size(), steps[index].label);

        juce::Timer::callAfterDelay(80, [this, index]
        {
            if (splashWindow == nullptr)
                return;

            steps[index].work();
            runStep(index + 1);
        });
    }

    std::unique_ptr<SplashScreenWindow> splashWindow;
    std::unique_ptr<MainWindow>         mainWindow;
    std::vector<Step>                   steps;
};

START_JUCE_APPLICATION(AetherApplication)
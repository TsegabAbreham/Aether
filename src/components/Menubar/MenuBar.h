#pragma once
#include <JuceHeader.h>

#include "../Workspace/Workspace.h"
#include "../../Windows/WindowManager.h"
#include "ProjectManager.h"


//==============================================================================
namespace Theme
{
    const juce::Colour bar     { 0xff14161a };
    const juce::Colour panel   { 0xff1b1d23 };
    const juce::Colour border  { 0xff2b2e36 };
    const juce::Colour text    { 0xffd5d8de };
    const juce::Colour textDim { 0xff6b7080 };
    const juce::Colour accent  { 0xff3ddcff };   // spatial cyan
}

//==============================================================================
class WorkstationLookAndFeel : public juce::LookAndFeel_V4
{
public:
    WorkstationLookAndFeel()
    {
        setColour(juce::PopupMenu::backgroundColourId,            Theme::panel);
        setColour(juce::PopupMenu::textColourId,                  Theme::text);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, Theme::accent.withAlpha(0.18f));
        setColour(juce::PopupMenu::highlightedTextColourId,       Theme::accent);
    }

    //---------------------------------------------------------------- Menu bar
    void drawMenuBarBackground(juce::Graphics& g, int w, int h,
                               bool, juce::MenuBarComponent&) override
    {
        g.fillAll(Theme::bar);
        g.setColour(Theme::border);
        g.fillRect(0, h - 1, w, 1);
    }

    juce::Font getMenuBarFont(juce::MenuBarComponent&, int, const juce::String&) override
    {
        return juce::Font(juce::FontOptions(13.5f));
    }

    int getMenuBarItemWidth(juce::MenuBarComponent& m, int idx, const juce::String& txt) override
    {
        auto font = getMenuBarFont(m, idx, txt);
        return juce::GlyphArrangement::getStringWidthInt(font, txt) + 26;
    }

    void drawMenuBarItem(juce::Graphics& g, int w, int h, int idx,
                         const juce::String& txt, bool over, bool open,
                         bool, juce::MenuBarComponent& m) override
    {
        auto r = juce::Rectangle<float>(0, 0, (float) w, (float) h).reduced(2.0f, 5.0f);

        if (open || over)
        {
            g.setColour(open ? Theme::accent.withAlpha(0.16f)
                             : juce::Colours::white.withAlpha(0.06f));
            g.fillRoundedRectangle(r, 5.0f);
        }

        g.setColour(open ? Theme::accent : Theme::text);
        g.setFont(getMenuBarFont(m, idx, txt));
        g.drawText(txt, r, juce::Justification::centred);
    }

    //---------------------------------------------------------------- Popups
    void drawPopupMenuBackground(juce::Graphics& g, int w, int h) override
    {
        g.fillAll(Theme::panel);
        g.setColour(Theme::border);
        g.drawRect(0, 0, w, h, 1);
    }

    void getIdealPopupMenuItemSize(const juce::String& text, bool isSeparator,
                                   int standardHeight, int& idealWidth, int& idealHeight) override
    {
        LookAndFeel_V4::getIdealPopupMenuItemSize(text, isSeparator, standardHeight,
                                                  idealWidth, idealHeight);
        idealHeight = isSeparator ? 9 : 28;
        idealWidth += 40;
    }

    void drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area,
                           bool isSeparator, bool isActive, bool isHighlighted,
                           bool isTicked, bool hasSubMenu,
                           const juce::String& text, const juce::String& shortcut,
                           const juce::Drawable*, const juce::Colour*) override
    {
        if (isSeparator)
        {
            g.setColour(Theme::border);
            g.fillRect(area.reduced(10, 0).withHeight(1).withCentre(area.getCentre()));
            return;
        }

        auto r = area.reduced(4, 1).toFloat();

        if (isHighlighted && isActive)
        {
            g.setColour(Theme::accent.withAlpha(0.16f));
            g.fillRoundedRectangle(r, 5.0f);
        }

        auto col = !isActive ? Theme::textDim
                             : (isHighlighted ? Theme::accent : Theme::text);
        g.setColour(col);
        g.setFont(juce::Font(juce::FontOptions(13.5f)));

        auto textArea = r.reduced(12.0f, 0.0f);

        if (isTicked)
            g.fillEllipse(r.getX() + 4.0f, r.getCentreY() - 2.0f, 4.0f, 4.0f);

        g.drawText(text, textArea, juce::Justification::centredLeft);

        if (shortcut.isNotEmpty())
        {
            g.setColour(isHighlighted ? Theme::accent.withAlpha(0.8f) : Theme::textDim);
            g.setFont(juce::Font(juce::FontOptions(12.0f)));
            g.drawText(shortcut, textArea, juce::Justification::centredRight);
        }

        if (hasSubMenu)
        {
            juce::Path arrow;
            auto cx = r.getRight() - 10.0f, cy = r.getCentreY();
            arrow.addTriangle(cx - 2.5f, cy - 4.0f, cx - 2.5f, cy + 4.0f, cx + 2.5f, cy);
            g.setColour(col);
            g.fillPath(arrow);
        }
    }
};

//==============================================================================
class MenuBar : public juce::Component,
                public juce::MenuBarModel
{
public:
    std::function<void(juce::File)> onFolderSelected;
    std::function<juce::AudioDeviceManager*()> onAudioDeviceManagerRequested;
    std::function<AetherState()> onProjectStateRequested;
    std::function<void(const AetherState&, const juce::File&)> onProjectSaved;
    std::function<bool()> onProjectDirtyCheck;
    std::function<bool(const AetherState&, juce::String&)> onProjectLoaded;
    std::function<bool(const juce::File&, juce::String&)> onAudioExportRequested;

    MenuBar()
    {
        menuBar.setLookAndFeel(&lnf);   // popups inherit this from the bar
        menuBar.setModel(this);
        addAndMakeVisible(menuBar);
    }

    ~MenuBar() override
    {
        menuBar.setModel(nullptr);
        menuBar.setLookAndFeel(nullptr);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(Theme::bar);
    }

    void resized() override
    {
        menuBar.setBounds(getLocalBounds());
    }

    juce::StringArray getMenuBarNames() override
    {
        return { "File", "Edit", "View", "Help" };
    }

    juce::PopupMenu getMenuForIndex(int, const juce::String& menuName) override
    {
        juce::PopupMenu menu;

        if (menuName == "File")
        {
            addMenuItem(menu, 1, "New Project",    "Ctrl+N");
            addMenuItem(menu, 2, "Open Folder...", "Ctrl+O");
            menu.addSeparator();
            addMenuItem(menu, 3, "Save",           "Ctrl+S");
            addMenuItem(menu, 4, "Open",           "Ctrl+Alt+O");
            menu.addSeparator();
            addMenuItem(menu, 7, "Export as...");
            menu.addSeparator();
            addMenuItem(menu, 5, "Settings",    "Ctrl+Alt+S");
            menu.addSeparator();
            addMenuItem(menu, 6, "Exit",           "Alt+F4");
        }
        else if (menuName == "Edit")
        {
            addMenuItem(menu, 10, "Undo", "Ctrl+Z");
            addMenuItem(menu, 11, "Redo", "Ctrl+Y");
        }
        else if (menuName == "View")
        {
            addMenuItem(menu, 20, "File Browser");
            addMenuItem(menu, 21, "Inspector");
            addMenuItem(menu, 22, "Viewport");
            addMenuItem(menu, 23, "Hierarchy");
            addMenuItem(menu, 24, "Timeline");
        }
        else if (menuName == "Help")
        {
            addMenuItem(menu, 30, "About");
        }

        return menu;
    }

    void menuItemSelected(int menuItemID, int) override
    {
        switch (menuItemID)
        {
            // ----------------------
            // ----- File Option ----
            // ----------------------
            case 2: openFolder(); break;
            
            case 3: chooseAndSaveProject(false); break;
            
            case 4:
                {
                    auto chooser = std::make_shared<juce::FileChooser>(
                        "Open Aether Project",
                        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
                        "*.atr");

                    chooser->launchAsync(
                        juce::FileBrowserComponent::openMode |
                        juce::FileBrowserComponent::canSelectFiles,
                        [this, chooser](const juce::FileChooser& fc)
                        {
                            auto projectFile = fc.getResult();

                            if (projectFile == juce::File())
                                return;

                            ProjectManager projectManager;
                            AetherState state;
                            juce::String error;

                            if (projectManager.loadProject(projectFile, state, &error))
                            {
                                if (onProjectLoaded && ! onProjectLoaded(state, error))
                                    juce::AlertWindow::showMessageBoxAsync(
                                        juce::MessageBoxIconType::WarningIcon,
                                        "Open Failed",
                                        error.isNotEmpty() ? error : "Could not restore this project.");
                            }
                            else
                            {
                                juce::AlertWindow::showMessageBoxAsync(
                                    juce::MessageBoxIconType::WarningIcon,
                                    "Open Failed",
                                    error);
                            }
                        });

                    break;
                }
            
            case 5:
                windowManager.openSettings (onAudioDeviceManagerRequested
                                                ? onAudioDeviceManagerRequested()
                                                : nullptr);
                break;
            case 7:
                {
                    auto chooser = std::make_shared<juce::FileChooser>(
                        "Export Final Audio",
                        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                            .getChildFile("Aether Export.wav"),
                        "*.wav;*.aiff;*.aif");
                    juce::Component::SafePointer<MenuBar> safe(this);

                    chooser->launchAsync(
                        juce::FileBrowserComponent::saveMode |
                        juce::FileBrowserComponent::canSelectFiles |
                        juce::FileBrowserComponent::warnAboutOverwriting,
                        [safe, chooser](const juce::FileChooser& fc)
                        {
                            if (safe == nullptr)
                                return;

                            auto destination = fc.getResult();
                            if (destination == juce::File())
                                return;

                            if (destination.getFileExtension().isEmpty())
                                destination = destination.withFileExtension("wav");

                            juce::String error;
                            if (! safe->onAudioExportRequested
                                || ! safe->onAudioExportRequested(destination, error))
                            {
                                juce::AlertWindow::showMessageBoxAsync(
                                    juce::MessageBoxIconType::WarningIcon,
                                    "Export Failed",
                                    error.isNotEmpty() ? error : "Audio export is unavailable.");
                                return;
                            }

                            juce::AlertWindow::showMessageBoxAsync(
                                juce::MessageBoxIconType::InfoIcon,
                                "Audio Exported",
                                "Final audio saved to:\n" + destination.getFullPathName());
                        });
                    break;
                }
            case 6: requestQuit(); break;
            

            default: break;
        }
    }

private:
    WorkstationLookAndFeel lnf;          // must be declared before menuBar
    juce::MenuBarComponent menuBar;

    static void addMenuItem(juce::PopupMenu& m, int id,
                            const juce::String& text,
                            const juce::String& shortcut = {})
    {
        juce::PopupMenu::Item item(text);
        item.itemID = id;
        item.shortcutKeyDescription = shortcut;
        m.addItem(item);
    }

    void chooseAndSaveProject(bool quitAfterSave)
    {
        if (! onProjectStateRequested)
        {
            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::WarningIcon,
                "Save Failed",
                "The project state is not available.");
            return;
        }

        const auto state = onProjectStateRequested();
        if (! state.projectFilePath.empty())
        {
            auto projectFile = juce::File(juce::String(state.projectFilePath));
            if (projectFile.hasFileExtension(".atr"))
            {
                if (saveProjectToFile(projectFile) && quitAfterSave)
                    juce::JUCEApplication::getInstance()->systemRequestedQuit();
                return;
            }
        }

        juce::File startFolder = state.explorer.rootFolder.isDirectory()
            ? state.explorer.rootFolder
            : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
        auto chooser = std::make_shared<juce::FileChooser>(
            "Save Aether Project",
            startFolder.getChildFile("Untitled.atr"),
            "*.atr");
        juce::Component::SafePointer<MenuBar> safe(this);

        chooser->launchAsync(
            juce::FileBrowserComponent::saveMode |
            juce::FileBrowserComponent::canSelectFiles |
            juce::FileBrowserComponent::warnAboutOverwriting,
            [safe, chooser, quitAfterSave](const juce::FileChooser& fc)
            {
                if (safe == nullptr)
                    return;

                auto projectFile = fc.getResult();
                if (projectFile == juce::File())
                    return;

                if (! projectFile.hasFileExtension(".atr"))
                    projectFile = projectFile.withFileExtension("atr");

                if (safe->saveProjectToFile(projectFile) && quitAfterSave)
                    juce::JUCEApplication::getInstance()->systemRequestedQuit();
            });
    }

    bool saveProjectToFile(const juce::File& projectFile)
    {
        if (! onProjectStateRequested)
            return false;

        auto state = onProjectStateRequested();
        state.projectName = projectFile.getFileNameWithoutExtension().toStdString();
        state.projectFilePath = projectFile.getFullPathName().toStdString();

        ProjectManager projectManager;
        if (! projectManager.saveProject(projectFile, state))
        {
            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::WarningIcon,
                "Save Failed",
                "Could not write " + projectFile.getFullPathName());
            return false;
        }

        if (onProjectSaved)
            onProjectSaved(state, projectFile);
        return true;
    }

    void requestQuit()
    {
        if (! onProjectDirtyCheck || ! onProjectDirtyCheck())
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
            return;
        }

        juce::Component::SafePointer<MenuBar> safe(this);
        juce::AlertWindow::showYesNoCancelBox(
            juce::MessageBoxIconType::QuestionIcon,
            "Unsaved Changes",
            "Save changes to this project before exiting?",
            "Save",
            "Don't Save",
            "Cancel",
            this,
            juce::ModalCallbackFunction::create(
                [safe](int result)
                {
                    if (safe == nullptr)
                        return;
                    if (result == 1)
                        safe->chooseAndSaveProject(true);
                    else if (result == 2)
                        juce::JUCEApplication::getInstance()->systemRequestedQuit();
                }));
    }

    void openFolder()
    {
        auto chooser = std::make_shared<juce::FileChooser>(
            "Select Project Folder",
            juce::File::getSpecialLocation(juce::File::userHomeDirectory));

        chooser->launchAsync(
            juce::FileBrowserComponent::openMode |
            juce::FileBrowserComponent::canSelectDirectories,
            [this, chooser](const juce::FileChooser& fc)
            {
                auto folder = fc.getResult();
                if (folder.isDirectory() && onFolderSelected)
                    onFolderSelected(folder);
            });
    }

    WindowManager windowManager; 

};
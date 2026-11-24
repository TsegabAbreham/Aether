#pragma once
#include <JuceHeader.h>

#include "../Workspace/Workspace.h"
#include "../../Windows/WindowManager.h"

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
            menu.addSeparator();
            addMenuItem(menu, 4, "Settings",    "Ctrl+Alt+S");
            menu.addSeparator();
            addMenuItem(menu, 5, "Exit",           "Alt+F4");
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
            // File
            case 2: openFolder(); break;
            /*
            case 3:
            {
                juce::AlertWindow::showOkCancelBox(
                    juce::MessageBoxIconType::QuestionIcon,
                    "Save Project?",
                    "Do you want to save your project?",
                    "Save",
                    "Cancel",
                    nullptr,
                    juce::ModalCallbackFunction::create(
                        [this](int result)
                        {
                            if (result != 1)
                                return;

                            auto chooser = std::make_shared<juce::FileChooser>(
                                "Save Aether Project",
                                juce::File::getSpecialLocation(
                                    juce::File::userDocumentsDirectory)
                                    .getChildFile("Untitled.atr"),
                                "*.atr"
                            );

                            chooser->launchAsync(
                                juce::FileBrowserComponent::saveMode |
                                juce::FileBrowserComponent::canSelectFiles,
                                [this, chooser](const juce::FileChooser& fc)
                                {
                                    auto projectFile = fc.getResult();

                                    if (projectFile == juce::File())
                                        return;

                                    if (!projectFile.hasFileExtension("atr"))
                                        projectFile = projectFile.withFileExtension("atr");

                                    ProjectManager projectManager;

                                    if (projectManager.saveProject(projectFile))
                                        DBG("Project saved successfully!");
                                    else
                                        DBG("Failed to save project!");
                                }
                            );
                        }
                    )
                );

                break;
            }
            */
            case 4:
                windowManager.openSettings (onAudioDeviceManagerRequested
                                                ? onAudioDeviceManagerRequested()
                                                : nullptr);
                break;
            case 5: juce::JUCEApplication::getInstance()->systemRequestedQuit(); break;
            
            // View

            // Help

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
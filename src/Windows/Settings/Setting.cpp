#include "Setting.h"
#include "SettingsPageFactory.h"

namespace
{
    namespace Theme
    {
        const juce::Colour bg     { 0xff1e1e22 };
        const juce::Colour panel  { 0xff26262c };
        const juce::Colour accent { 0xff4aa8ff };
        const juce::Colour text   { 0xffe6e6ea };
        const juce::Colour dim    { 0xff8a8a94 };
    }

    class SettingsContent : public juce::Component,
                            private juce::ListBoxModel
    {
    public:
        explicit SettingsContent (juce::AudioDeviceManager* dm)
            : deviceManager (dm)
        {
            categories = { "HRIR Profile", "Spatial Audio", "Audio Device",
                           "Viewport", "Timeline", "General" };

            sidebar.setModel (this);
            sidebar.setRowHeight (36);
            sidebar.setColour (juce::ListBox::backgroundColourId, Theme::panel);
            sidebar.setOutlineThickness (0);
            addAndMakeVisible (sidebar);
            showPage (0);
            sidebar.selectRow (0);
        }

        void paint (juce::Graphics& graphics) override { graphics.fillAll (Theme::bg); }

        void resized() override
        {
            auto area = getLocalBounds();
            sidebar.setBounds (area.removeFromLeft (150));
            if (page != nullptr)
                page->setBounds (area);
        }

    private:
        void showPage (int index)
        {
            if (index == currentPage && page != nullptr)
                return;

            currentPage = index;
            page.reset();

            switch (index)
            {
                case 0: page = SettingsPages::makeHrirPage(); break;
                case 1: page = SettingsPages::makeSpatialAudioPage(); break;
                case 2: page = SettingsPages::makeAudioDevicePage (deviceManager); break;
                case 3: page = SettingsPages::makeViewportPage(); break;
                case 4: page = SettingsPages::makeTimelinePage(); break;
                default: page = SettingsPages::makeGeneralPage(); break;
            }

            addAndMakeVisible (*page);
            resized();
        }

        int getNumRows() override { return categories.size(); }

        void paintListBoxItem (int row, juce::Graphics& graphics, int width, int height,
                               bool selected) override
        {
            if (selected)
            {
                graphics.setColour (Theme::accent.withAlpha (0.2f));
                graphics.fillRect (0, 0, width, height);
                graphics.setColour (Theme::accent);
                graphics.fillRect (0, 0, 3, height);
            }

            graphics.setColour (selected ? Theme::text : Theme::dim);
            graphics.setFont (juce::FontOptions (14.0f));
            graphics.drawText (categories[row], 16, 0, width - 16, height,
                               juce::Justification::centredLeft);
        }

        void selectedRowsChanged (int row) override
        {
            if (row >= 0)
                showPage (row);
        }

        juce::AudioDeviceManager* deviceManager;
        juce::StringArray categories;
        juce::ListBox sidebar;
        std::unique_ptr<juce::Component> page;
        int currentPage = -1;
    };
}

SettingsWindow::SettingsWindow (juce::AudioDeviceManager* deviceManager)
    : AetherWindow ("Aether Settings", 760, 520)
{
    auto* content = new SettingsContent (deviceManager);
    content->setSize (760, 520);
    setContentOwned (content, true);
    setVisible (true);
}

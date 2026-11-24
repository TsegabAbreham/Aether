#include "Setting.h"
#include "SettingsPageFactory.h"
#include "SettingsPageComponents.h"

namespace
{
    struct HrirOption
    {
        const char* hrirFile;
        const char* imageBase;
        const char* name;
        const char* description;
    };

    const HrirOption hrirOptions[] =
    {
        { "d1.hrir", "d1", "SADIE II - D1", "Dummy head measurement (SADIE II subject D1)." },
        { "h3.hrir", "h3", "SADIE II - H3", "Human subject measurement (SADIE II subject H3)." }
    };

    constexpr int numHrirOptions = (int) (sizeof (hrirOptions) / sizeof (hrirOptions[0]));

    juce::File findHrtfAsset (const juce::String& fileName)
    {
        const auto exeDir = juce::File::getSpecialLocation (juce::File::currentApplicationFile)
                                .getParentDirectory();
        const auto cwd = juce::File::getCurrentWorkingDirectory();
        const juce::File dirs[]
        {
            exeDir,
            exeDir.getChildFile ("assets/hrtf"),
            cwd,
            cwd.getChildFile ("assets/hrtf")
        };

        for (const auto& dir : dirs)
        {
            auto file = dir.getChildFile (fileName);
            if (file.existsAsFile())
                return file;
        }

        return {};
    }

    juce::Image loadProfileImage (const juce::String& baseName)
    {
        for (auto* extension : { ".png", ".jpg", ".jpeg" })
        {
            auto file = findHrtfAsset (baseName + extension);
            if (file.existsAsFile())
                return juce::ImageCache::getFromFile (file);
        }

        return {};
    }

    class ImagePreview : public juce::Component
    {
    public:
        void setImage (const juce::Image& newImage, const juce::String& missingMessage)
        {
            image = newImage;
            message = missingMessage;
            repaint();
        }

        void paint (juce::Graphics& graphics) override
        {
            auto bounds = getLocalBounds().toFloat();
            graphics.setColour (SettingsUI::Theme::inset);
            graphics.fillRoundedRectangle (bounds, 8.0f);

            auto area = bounds.reduced (8.0f);
            if (image.isValid())
            {
                graphics.drawImage (image, area, juce::RectanglePlacement::centred);
                return;
            }

            graphics.setColour (SettingsUI::Theme::dim);
            graphics.setFont (juce::FontOptions (12.0f));
            graphics.drawFittedText (message, area.toNearestInt(),
                                     juce::Justification::centred, 3);
        }

    private:
        juce::Image image;
        juce::String message;
    };

    class HrirPage : public juce::Component,
                     private juce::ListBoxModel
    {
    public:
        HrirPage()
        {
            list.setModel (this);
            list.setRowHeight (48);
            list.setColour (juce::ListBox::backgroundColourId, SettingsUI::Theme::inset);
            list.setOutlineThickness (0);
            addAndMakeVisible (list);
            addAndMakeVisible (preview);
            addAndMakeVisible (nameLabel);
            addAndMakeVisible (descLabel);
            addAndMakeVisible (fileLabel);

            nameLabel.setFont (juce::FontOptions (18.0f, juce::Font::bold));
            nameLabel.setColour (juce::Label::textColourId, SettingsUI::Theme::text);
            descLabel.setColour (juce::Label::textColourId, SettingsUI::Theme::dim);
            descLabel.setJustificationType (juce::Justification::topLeft);
            fileLabel.setColour (juce::Label::textColourId, SettingsUI::Theme::dim);
            fileLabel.setFont (juce::FontOptions (11.0f));

            applyButton.setButtonText ("Use this profile");
            addAndMakeVisible (applyButton);
            applyButton.onClick = [this]
            {
                const int row = list.getSelectedRow();
                if (row < 0 || row >= numHrirOptions)
                    return;

                const juce::String file (hrirOptions[row].hrirFile);
                SettingsWindow::activeHrirFile = file;
                if (SettingsWindow::onHrirChosen)
                    SettingsWindow::onHrirChosen (file);

                list.repaint();
                updatePreview();
            };

            int startRow = 0;
            for (int i = 0; i < numHrirOptions; ++i)
                if (SettingsWindow::activeHrirFile == hrirOptions[i].hrirFile)
                    startRow = i;

            list.selectRow (startRow);
            updatePreview();
        }

        void resized() override
        {
            auto area = getLocalBounds().reduced (12);
            list.setBounds (area.removeFromLeft (210));
            area.removeFromLeft (12);
            preview.setBounds (area.removeFromTop (260));
            area.removeFromTop (10);
            nameLabel.setBounds (area.removeFromTop (28));
            descLabel.setBounds (area.removeFromTop (44));
            fileLabel.setBounds (area.removeFromTop (20));
            area.removeFromTop (6);
            applyButton.setBounds (area.removeFromTop (30).removeFromLeft (150));
        }

    private:
        void updatePreview()
        {
            const int row = list.getSelectedRow();
            if (row < 0 || row >= numHrirOptions)
                return;

            const auto& option = hrirOptions[row];
            const juce::String hrirName (option.hrirFile);
            preview.setImage (loadProfileImage (option.imageBase),
                              "No image found.\nAdd " + juce::String (option.imageBase)
                                  + ".png to assets/hrtf");
            nameLabel.setText (option.name, juce::dontSendNotification);
            descLabel.setText (option.description, juce::dontSendNotification);

            const auto hrirPath = findHrtfAsset (hrirName);
            fileLabel.setText (hrirPath.existsAsFile() ? hrirPath.getFullPathName()
                                                       : hrirName + " not found in assets/hrtf",
                               juce::dontSendNotification);
            applyButton.setEnabled (hrirPath.existsAsFile()
                                    && SettingsWindow::activeHrirFile != hrirName);
        }

        int getNumRows() override { return numHrirOptions; }

        void paintListBoxItem (int row, juce::Graphics& graphics, int width, int height,
                               bool isSelected) override
        {
            if (row < 0 || row >= numHrirOptions)
                return;

            const auto& option = hrirOptions[row];
            if (isSelected)
            {
                graphics.setColour (SettingsUI::Theme::accent.withAlpha (0.25f));
                graphics.fillRect (0, 0, width, height);
            }

            graphics.setColour (SettingsUI::Theme::text);
            graphics.setFont (juce::FontOptions (14.0f));
            graphics.drawText (option.name, 12, 6, width - 24, 20,
                               juce::Justification::centredLeft);

            const bool active = SettingsWindow::activeHrirFile == option.hrirFile;
            graphics.setColour (active ? SettingsUI::Theme::accent : SettingsUI::Theme::dim);
            graphics.setFont (juce::FontOptions (11.0f));
            graphics.drawText (active ? juce::String ("Active") : juce::String (option.hrirFile),
                               12, 26, width - 24, 16, juce::Justification::centredLeft);
        }

        void selectedRowsChanged (int) override { updatePreview(); }

        void listBoxItemDoubleClicked (int, const juce::MouseEvent&) override
        {
            if (applyButton.isEnabled())
                applyButton.triggerClick();
        }

        juce::ListBox list;
        ImagePreview preview;
        juce::Label nameLabel, descLabel, fileLabel;
        juce::TextButton applyButton;
    };
}

namespace SettingsPages
{
    std::unique_ptr<juce::Component> makeHrirPage()
    {
        return std::make_unique<HrirPage>();
    }
}

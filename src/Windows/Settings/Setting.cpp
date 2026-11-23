#include "Setting.h"
#include "SettingsManager.h"

using Key = SettingsManager::Key;

namespace Theme
{
    const juce::Colour bg      { 0xff1e1e22 };
    const juce::Colour panel   { 0xff26262c };
    const juce::Colour inset   { 0xff17171b };
    const juce::Colour accent  { 0xff4aa8ff };
    const juce::Colour text    { 0xffe6e6ea };
    const juce::Colour dim     { 0xff8a8a94 };
}

//==============================================================================
// Property rows bound directly to SettingsManager
//==============================================================================
namespace
{
    class SliderRow : public juce::SliderPropertyComponent
    {
    public:
        SliderRow (const juce::String& n, double lo, double hi, double step, Key k,
                   const juce::String& suffix = {})
            : SliderPropertyComponent (n, lo, hi, step), key (k)
        {
            slider.setTextValueSuffix (suffix);
        }

        void   setValue (double v) override { SettingsManager::get().setDouble (key, v); }
        double getValue() const override    { return SettingsManager::get().getDouble (key); }

    private:
        Key key;
    };

    class ToggleRow : public juce::BooleanPropertyComponent
    {
    public:
        ToggleRow (const juce::String& n, Key k)
            : BooleanPropertyComponent (n, "On", "Off"), key (k) {}

        void setState (bool b) override { SettingsManager::get().setBool (key, b); }
        bool getState() const override  { return SettingsManager::get().getBool (key); }

    private:
        Key key;
    };

    class ChoiceRow : public juce::ChoicePropertyComponent
    {
    public:
        ChoiceRow (const juce::String& n, const juce::StringArray& items, Key k)
            : ChoicePropertyComponent (n), key (k)
        {
            choices = items;
        }

        void setIndex (int i) override { SettingsManager::get().setInt (key, i); }
        int  getIndex() const override { return SettingsManager::get().getInt (key); }

    private:
        Key key;
    };

    std::unique_ptr<juce::PropertyPanel> makePanel (const juce::String& section,
                                                    std::initializer_list<juce::PropertyComponent*> rows)
    {
        juce::Array<juce::PropertyComponent*> arr;
        for (auto* r : rows) arr.add (r);

        auto panel = std::make_unique<juce::PropertyPanel>();
        panel->addSection (section, arr);
        return panel;
    }
}

//==============================================================================
// Image preview (falls back to a drawn placeholder when no image is available)
//==============================================================================
class ImagePreview : public juce::Component
{
public:
    enum class Kind { Ear, Face };

    ImagePreview (Kind k, const juce::String& t) : kind (k), title (t) {}

    void setImageFile (const juce::File& f)
    {
        image = f.existsAsFile() ? juce::ImageCache::getFromFile (f) : juce::Image();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour (Theme::inset);
        g.fillRoundedRectangle (b, 8.0f);

        auto caption = b.removeFromBottom (24.0f);
        auto area    = b.reduced (8.0f);

        if (image.isValid())
            g.drawImage (image, area, juce::RectanglePlacement::centred);
        else
            drawPlaceholder (g, area);

        g.setColour (Theme::dim);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (title, caption.toNearestInt(), juce::Justification::centred);
    }

private:
    void drawPlaceholder (juce::Graphics& g, juce::Rectangle<float> a)
    {
        g.setColour (Theme::dim.withAlpha (0.6f));
        auto c = a.getCentre();
        const float r = juce::jmin (a.getWidth(), a.getHeight()) * 0.38f;

        if (kind == Kind::Face)
        {
            g.drawEllipse (c.x - r * 0.75f, c.y - r, r * 1.5f, r * 2.0f, 2.0f);          // head
            g.drawEllipse (c.x - r * 0.95f, c.y - r * 0.2f, r * 0.2f, r * 0.5f, 2.0f);   // left ear
            g.drawEllipse (c.x + r * 0.75f, c.y - r * 0.2f, r * 0.2f, r * 0.5f, 2.0f);   // right ear
        }
        else
        {
            juce::Path outer, inner;
            outer.addCentredArc (c.x, c.y, r * 0.6f, r, 0.0f, -2.6f, 2.6f, true);
            inner.addCentredArc (c.x + r * 0.05f, c.y, r * 0.3f, r * 0.55f, 0.0f, -2.4f, 2.4f, true);
            g.strokePath (outer, juce::PathStrokeType (2.0f));
            g.strokePath (inner, juce::PathStrokeType (2.0f));
        }

        g.setFont (juce::FontOptions (11.0f));
        auto textArea = a.toNearestInt();
        g.drawText ("No image", textArea.removeFromBottom (18), juce::Justification::centred);
    }

    Kind kind;
    juce::String title;
    juce::Image image;
};

//==============================================================================
// HRIR page
//==============================================================================
class HrirPage : public juce::Component,
                 private juce::ListBoxModel,
                 private juce::ChangeListener
{
public:
    HrirPage()
        : ear  (ImagePreview::Kind::Ear,  "Ear structure"),
          face (ImagePreview::Kind::Face, "Head / face structure")
    {
        list.setModel (this);
        list.setRowHeight (48);
        list.setColour (juce::ListBox::backgroundColourId, Theme::inset);
        list.setOutlineThickness (0);
        addAndMakeVisible (list);
        addAndMakeVisible (ear);
        addAndMakeVisible (face);

        addAndMakeVisible (nameLabel);
        addAndMakeVisible (descLabel);
        addAndMakeVisible (fileLabel);

        nameLabel.setFont (juce::FontOptions (18.0f, juce::Font::bold));
        nameLabel.setColour (juce::Label::textColourId, Theme::text);
        descLabel.setColour (juce::Label::textColourId, Theme::dim);
        descLabel.setJustificationType (juce::Justification::topLeft);
        fileLabel.setColour (juce::Label::textColourId, Theme::dim);
        fileLabel.setFont (juce::FontOptions (11.0f));

        applyBtn.setButtonText ("Use this profile");
        importBtn.setButtonText ("Import .hrir...");
        removeBtn.setButtonText ("Remove");
        addAndMakeVisible (applyBtn);
        addAndMakeVisible (importBtn);
        addAndMakeVisible (removeBtn);

        applyBtn.onClick = [this]
        {
            if (auto* p = selected())
                SettingsManager::get().setActiveHrir (p->id);
        };

        removeBtn.onClick = [this]
        {
            if (auto* p = selected())
                SettingsManager::get().removeCustomHrir (p->id);
        };

        importBtn.onClick = [this]
        {
            juce::Component::SafePointer<HrirPage> safe (this);

            chooser = std::make_unique<juce::FileChooser> ("Select an HRIR file",
                                                           juce::File(), "*.hrir");
            chooser->launchAsync (juce::FileBrowserComponent::openMode
                                  | juce::FileBrowserComponent::canSelectFiles,
                [safe] (const juce::FileChooser& fc)
                {
                    if (safe == nullptr || ! fc.getResult().existsAsFile())
                        return;

                    auto id = SettingsManager::get().importCustomHrir (fc.getResult());
                    auto& ps = SettingsManager::get().getHrirProfiles();

                    for (int i = 0; i < ps.size(); ++i)
                        if (ps.getReference (i).id == id)
                            safe->list.selectRow (i);
                });
        };

        SettingsManager::get().addChangeListener (this);

        if (auto* active = SettingsManager::get().getActiveHrir())
        {
            auto& ps = SettingsManager::get().getHrirProfiles();
            for (int i = 0; i < ps.size(); ++i)
                if (ps.getReference (i).id == active->id)
                    list.selectRow (i);
        }

        updatePreview();
    }

    ~HrirPage() override
    {
        SettingsManager::get().removeChangeListener (this);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12);
        list.setBounds (r.removeFromLeft (210));
        r.removeFromLeft (12);

        auto images = r.removeFromTop (200);
        ear.setBounds (images.removeFromLeft (images.getWidth() / 2 - 6));
        images.removeFromLeft (12);
        face.setBounds (images);

        r.removeFromTop (10);
        nameLabel.setBounds (r.removeFromTop (28));
        descLabel.setBounds (r.removeFromTop (60));
        fileLabel.setBounds (r.removeFromTop (20));
        r.removeFromTop (6);

        auto btns = r.removeFromTop (30);
        applyBtn.setBounds (btns.removeFromLeft (130));
        btns.removeFromLeft (8);
        importBtn.setBounds (btns.removeFromLeft (120));
        btns.removeFromLeft (8);
        removeBtn.setBounds (btns.removeFromLeft (80));
    }

private:
    const HrirProfile* selected() const
    {
        auto& ps = SettingsManager::get().getHrirProfiles();
        int row = list.getSelectedRow();
        return (row >= 0 && row < ps.size()) ? &ps.getReference (row) : nullptr;
    }

    void updatePreview()
    {
        auto* p = selected();
        auto* active = SettingsManager::get().getActiveHrir();

        ear.setImageFile  (p ? p->earImage  : juce::File());
        face.setImageFile (p ? p->faceImage : juce::File());

        nameLabel.setText (p ? p->name : juce::String ("No HRIR profiles found"),
                           juce::dontSendNotification);
        descLabel.setText (p ? p->description
                             : juce::String ("Put .hrir files in assets/hrtf next to the executable, or import one."),
                           juce::dontSendNotification);
        fileLabel.setText (p ? p->hrirFile.getFullPathName() : juce::String(),
                           juce::dontSendNotification);

        applyBtn.setEnabled (p != nullptr && active != nullptr && p->id != active->id);
        removeBtn.setVisible (p != nullptr && p->isCustom);
    }

    // ListBoxModel
    int getNumRows() override { return SettingsManager::get().getHrirProfiles().size(); }

    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool isSelected) override
    {
        auto& ps = SettingsManager::get().getHrirProfiles();
        if (row < 0 || row >= ps.size()) return;

        auto& p = ps.getReference (row);
        auto* active = SettingsManager::get().getActiveHrir();

        if (isSelected)
        {
            g.setColour (Theme::accent.withAlpha (0.25f));
            g.fillRect (0, 0, w, h);
        }

        g.setColour (Theme::text);
        g.setFont (juce::FontOptions (14.0f));
        g.drawText (p.name, 12, 6, w - 24, 20, juce::Justification::centredLeft);

        const bool isActive = (active != nullptr && active->id == p.id);
        g.setColour (isActive ? Theme::accent : Theme::dim);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (isActive ? juce::String ("Active") : p.hrirFile.getFileName(),
                    12, 26, w - 24, 16, juce::Justification::centredLeft);
    }

    void selectedRowsChanged (int) override { updatePreview(); }

    void listBoxItemDoubleClicked (int, const juce::MouseEvent&) override
    {
        applyBtn.triggerClick();
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        list.updateContent();
        list.repaint();
        updatePreview();
    }

    juce::ListBox list;
    ImagePreview ear, face;
    juce::Label nameLabel, descLabel, fileLabel;
    juce::TextButton applyBtn, importBtn, removeBtn;
    std::unique_ptr<juce::FileChooser> chooser;
};

//==============================================================================
// Settings content: sidebar + pages
//==============================================================================
class SettingsContent : public juce::Component, private juce::ListBoxModel
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

    void paint (juce::Graphics& g) override { g.fillAll (Theme::bg); }

    void resized() override
    {
        auto r = getLocalBounds();
        sidebar.setBounds (r.removeFromLeft (150));
        if (page != nullptr)
            page->setBounds (r);
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
            case 0:
                page = std::make_unique<HrirPage>();
                break;

            case 1:
                page = makePanel ("Spatial Audio", {
                    new ToggleRow ("Binaural rendering", Key::BinauralEnabled),
                    new SliderRow ("Master gain", -24.0, 12.0, 0.5, Key::MasterGainDb, " dB"),
                    new ChoiceRow ("Distance attenuation",
                                   juce::StringArray { "None", "Inverse", "Linear" },
                                   Key::DistanceModel),
                    new ToggleRow ("Doppler effect", Key::DopplerEnabled),
                    new ToggleRow ("Output limiter", Key::LimiterEnabled) });
                break;

            case 2:
                if (deviceManager != nullptr)
                {
                    page = std::make_unique<juce::AudioDeviceSelectorComponent> (
                        *deviceManager, 0, 0, 2, 2, false, false, true, false);
                }
                else
                {
                    auto l = std::make_unique<juce::Label> (juce::String(),
                                                            "No audio device manager was provided.");
                    l->setJustificationType (juce::Justification::centred);
                    page = std::move (l);
                }
                break;

            case 3:
                page = makePanel ("Viewport", {
                    new SliderRow ("Field of view", 30.0, 110.0, 1.0, Key::ViewportFov, " deg"),
                    new SliderRow ("Mouse sensitivity", 0.05, 1.0, 0.01, Key::MouseSensitivity),
                    new SliderRow ("Move speed (WASD)", 1.0, 20.0, 0.5, Key::MoveSpeed, " u/s"),
                    new ToggleRow ("Show grid", Key::ShowGrid),
                    new ToggleRow ("Invert mouse Y", Key::InvertMouseY) });
                break;

            case 4:
                page = makePanel ("Timeline", {
                    new ToggleRow ("Snap clips to grid", Key::TimelineSnap),
                    new SliderRow ("Autosave interval (0 = off)", 0.0, 30.0, 1.0,
                                   Key::AutosaveMinutes, " min") });
                break;

            default:
                page = makePanel ("General", {
                    new ToggleRow ("Show splash screen on startup", Key::ShowSplash) });
                break;
        }

        addAndMakeVisible (*page);
        resized();
    }

    int getNumRows() override { return categories.size(); }

    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool sel) override
    {
        if (sel)
        {
            g.setColour (Theme::accent.withAlpha (0.2f));
            g.fillRect (0, 0, w, h);
            g.setColour (Theme::accent);
            g.fillRect (0, 0, 3, h);
        }

        g.setColour (sel ? Theme::text : Theme::dim);
        g.setFont (juce::FontOptions (14.0f));
        g.drawText (categories[row], 16, 0, w - 16, h, juce::Justification::centredLeft);
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

//==============================================================================
SettingsWindow::SettingsWindow (juce::AudioDeviceManager* dm)
    : AetherWindow ("Aether Settings", 760, 520)
{
    auto* content = new SettingsContent (dm);
    content->setSize (760, 520);
    setContentOwned (content, true);
    setVisible (true);
}
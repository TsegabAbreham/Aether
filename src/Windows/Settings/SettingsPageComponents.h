#pragma once

#include "SettingsManager.h"

namespace SettingsUI
{
    namespace Theme
    {
        inline const juce::Colour bg     { 0xff1e1e22 };
        inline const juce::Colour panel  { 0xff26262c };
        inline const juce::Colour inset  { 0xff17171b };
        inline const juce::Colour accent { 0xff4aa8ff };
        inline const juce::Colour text   { 0xffe6e6ea };
        inline const juce::Colour dim    { 0xff8a8a94 };
    }

    using Key = SettingsManager::Key;

    class SliderRow : public juce::SliderPropertyComponent
    {
    public:
        SliderRow (const juce::String& name, double minimum, double maximum, double interval,
                   Key setting, const juce::String& suffix = {})
            : SliderPropertyComponent (name, minimum, maximum, interval), key (setting)
        {
            slider.setTextValueSuffix (suffix);
        }

        void setValue (double value) override { SettingsManager::get().setDouble (key, value); }
        double getValue() const override      { return SettingsManager::get().getDouble (key); }

    private:
        Key key;
    };

    class ToggleRow : public juce::BooleanPropertyComponent
    {
    public:
        ToggleRow (const juce::String& name, Key setting)
            : BooleanPropertyComponent (name, "On", "Off"), key (setting) {}

        void setState (bool state) override { SettingsManager::get().setBool (key, state); }
        bool getState() const override      { return SettingsManager::get().getBool (key); }

    private:
        Key key;
    };

    class ChoiceRow : public juce::ChoicePropertyComponent
    {
    public:
        ChoiceRow (const juce::String& name, const juce::StringArray& items, Key setting)
            : ChoicePropertyComponent (name), key (setting)
        {
            choices = items;
        }

        void setIndex (int index) override { SettingsManager::get().setInt (key, index); }
        int getIndex() const override      { return SettingsManager::get().getInt (key); }

    private:
        Key key;
    };

    inline std::unique_ptr<juce::PropertyPanel> makePanel (
        const juce::String& section,
        std::initializer_list<juce::PropertyComponent*> rows)
    {
        juce::Array<juce::PropertyComponent*> properties;
        for (auto* row : rows)
            properties.add (row);

        auto panel = std::make_unique<juce::PropertyPanel>();
        panel->addSection (section, properties);
        return panel;
    }
}

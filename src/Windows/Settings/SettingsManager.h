#pragma once
#include <JuceHeader.h>

struct HrirProfile
{
    juce::String id, name, description;
    juce::File   hrirFile, earImage, faceImage;
    bool         isCustom = false;
};

// Persistent app settings. Anything can listen via addChangeListener().
class SettingsManager : public juce::ChangeBroadcaster
{
public:
    enum class Key
    {
        BinauralEnabled, MasterGainDb, DistanceModel, DopplerEnabled, LimiterEnabled,
        ViewportFov, MouseSensitivity, MoveSpeed, ShowGrid, InvertMouseY,
        TimelineSnap, AutosaveMinutes, ShowSplash, ActiveHrir
    };

    static SettingsManager& get();

    // HRIR profiles
    const juce::Array<HrirProfile>& getHrirProfiles() const { return profiles; }
    const HrirProfile* findProfile (const juce::String& id) const;
    const HrirProfile* getActiveHrir() const;            // null only if no profiles exist
    void               setActiveHrir (const juce::String& id);
    juce::String       importCustomHrir (const juce::File& file);   // returns id or {}
    void               removeCustomHrir (const juce::String& id);
    void               rescanProfiles();

    // Typed values
    bool         getBool   (Key) const;
    double       getDouble (Key) const;
    int          getInt    (Key) const;
    juce::String getString (Key) const;
    void         setBool   (Key, bool);
    void         setDouble (Key, double);
    void         setInt    (Key, int);

private:
    SettingsManager();
    void setVar (Key, const juce::var&);
    juce::File getHrirDir() const;
    void addProfileFromFile (const juce::File&, bool custom);

    mutable juce::ApplicationProperties props;   // mutable: getUserSettings() is non-const
    juce::Array<HrirProfile> profiles;
};
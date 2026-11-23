#include "SettingsManager.h"

namespace
{
    struct KeyInfo { const char* name; juce::var def; };

    // Order MUST match SettingsManager::Key
    const KeyInfo& info (SettingsManager::Key k)
    {
        static const KeyInfo table[] =
        {
            { "binaural",      juce::var (true)  },
            { "masterGainDb",  juce::var (0.0)   },
            { "distanceModel", juce::var (1.0)   },
            { "doppler",       juce::var (false) },
            { "limiter",       juce::var (true)  },
            { "fov",           juce::var (60.0)  },
            { "mouseSens",     juce::var (0.25)  },
            { "moveSpeed",     juce::var (5.0)   },
            { "showGrid",      juce::var (true)  },
            { "invertY",       juce::var (false) },
            { "snap",          juce::var (true)  },
            { "autosave",      juce::var (5.0)   },
            { "splash",        juce::var (true)  },
            { "activeHrir",    juce::var ("d1")  },
        };
        return table[(int) k];
    }

    juce::File findImage (const juce::File& dir, const juce::String& stem, const juce::String& suffix)
    {
        for (auto* ext : { ".png", ".jpg", ".jpeg" })
        {
            auto f = dir.getChildFile (stem + "_" + suffix + ext);
            if (f.existsAsFile()) return f;
        }
        return {};
    }
}

SettingsManager& SettingsManager::get()
{
    static SettingsManager instance;
    return instance;
}

SettingsManager::SettingsManager()
{
    juce::PropertiesFile::Options o;
    o.applicationName     = "Aether";
    o.filenameSuffix      = ".settings";
    o.osxLibrarySubFolder = "Application Support";
    o.commonToAllUsers    = false;
    o.storageFormat       = juce::PropertiesFile::storeAsXML;
    props.setStorageParameters (o);

    rescanProfiles();
}

juce::File SettingsManager::getHrirDir() const
{
    return juce::File::getSpecialLocation (juce::File::currentExecutableFile)
               .getParentDirectory().getChildFile ("assets").getChildFile ("hrtf");
}

void SettingsManager::addProfileFromFile (const juce::File& f, bool custom)
{
    auto stem = f.getFileNameWithoutExtension();
    auto dir  = custom ? f.getParentDirectory() : getHrirDir().getChildFile ("images");

    HrirProfile p;
    p.id        = custom ? "custom_" + stem : stem;
    p.name      = custom ? stem + " (custom)" : stem.toUpperCase();
    p.hrirFile  = f;
    p.isCustom  = custom;
    p.earImage  = findImage (dir, stem, "ear");
    p.faceImage = findImage (dir, stem, "face");

    auto descFile = f.withFileExtension ("txt");
    p.description = descFile.existsAsFile() ? descFile.loadFileAsString().trim()
                                            : "HRIR set loaded from " + f.getFileName();
    profiles.add (p);
}

void SettingsManager::rescanProfiles()
{
    profiles.clear();

    for (auto& f : getHrirDir().findChildFiles (juce::File::findFiles, false, "*.hrir"))
        addProfileFromFile (f, false);

    auto custom = juce::StringArray::fromLines (props.getUserSettings()->getValue ("customHrirs"));
    for (auto& path : custom)
        if (juce::File (path).existsAsFile())
            addProfileFromFile (juce::File (path), true);
}

const HrirProfile* SettingsManager::findProfile (const juce::String& id) const
{
    for (auto& p : profiles)
        if (p.id == id) return &p;
    return nullptr;
}

const HrirProfile* SettingsManager::getActiveHrir() const
{
    if (auto* p = findProfile (getString (Key::ActiveHrir))) return p;
    return profiles.isEmpty() ? nullptr : &profiles.getReference (0);
}

void SettingsManager::setActiveHrir (const juce::String& id)
{
    if (findProfile (id) != nullptr)
        setVar (Key::ActiveHrir, id);
}

juce::String SettingsManager::importCustomHrir (const juce::File& file)
{
    if (! file.existsAsFile()) return {};

    auto* s = props.getUserSettings();
    auto lines = juce::StringArray::fromLines (s->getValue ("customHrirs"));
    lines.addIfNotAlreadyThere (file.getFullPathName());
    s->setValue ("customHrirs", lines.joinIntoString ("\n"));
    s->saveIfNeeded();

    rescanProfiles();
    sendChangeMessage();
    return "custom_" + file.getFileNameWithoutExtension();
}

void SettingsManager::removeCustomHrir (const juce::String& id)
{
    auto* p = findProfile (id);
    if (p == nullptr || ! p->isCustom) return;

    auto* s = props.getUserSettings();
    auto lines = juce::StringArray::fromLines (s->getValue ("customHrirs"));
    lines.removeString (p->hrirFile.getFullPathName());
    s->setValue ("customHrirs", lines.joinIntoString ("\n"));

    const bool wasActive = (getString (Key::ActiveHrir) == id);
    rescanProfiles();

    if (wasActive && ! profiles.isEmpty())
        setVar (Key::ActiveHrir, profiles.getReference (0).id);
    else
    {
        s->saveIfNeeded();
        sendChangeMessage();
    }
}

bool SettingsManager::getBool (Key k) const
{ return props.getUserSettings()->getBoolValue (info (k).name, (bool) info (k).def); }

double SettingsManager::getDouble (Key k) const
{ return props.getUserSettings()->getDoubleValue (info (k).name, (double) info (k).def); }

int SettingsManager::getInt (Key k) const
{ return juce::roundToInt (getDouble (k)); }

juce::String SettingsManager::getString (Key k) const
{ return props.getUserSettings()->getValue (info (k).name, info (k).def.toString()); }

void SettingsManager::setBool   (Key k, bool v)   { setVar (k, juce::var (v)); }
void SettingsManager::setDouble (Key k, double v) { setVar (k, juce::var (v)); }
void SettingsManager::setInt    (Key k, int v)    { setVar (k, juce::var ((double) v)); }

void SettingsManager::setVar (Key k, const juce::var& v)
{
    auto* s = props.getUserSettings();
    s->setValue (info (k).name, v);
    s->saveIfNeeded();
    sendChangeMessage();
}
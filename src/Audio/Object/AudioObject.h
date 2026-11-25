#pragma once
#include <JuceHeader.h>
#include <glm/glm.hpp>

#include "../../AetherState.h"
#include "../Spatialization/BinauralPanner.h"
#include "../Spatialization/HrirPanner.h"

struct Transform
{
    glm::vec3 position { 0.0f, 0.0f, 0.0f };       // initialised: glm::vec3 is NOT zeroed by default
    glm::vec3 rotation { 0.0f, 0.0f, 0.0f };
};


//==============================================================================
// One audio clip on the timeline == one "audio object" in the 3D scene.
// The Timeline owns these; Hierarchy and Inspector only read/modify them through
// the Timeline's setObject...() functions (which take care of thread safety).
//==============================================================================
struct AudioObject : AetherState::AudioClipState
{
    using Ptr = std::shared_ptr<AudioObject>;

    // audio data
    std::shared_ptr<juce::AudioBuffer<float>> data;
    std::shared_ptr<juce::AudioThumbnail>     thumb;

    // audio manipulators
    // audio-thread state (only touched while the Timeline lock is held)
    juce::Reverb   reverb;
    BinauralPanner panner;                     // simple model (ITD + head shadow), used as the fallback
    HrirPanner     hrir;                       // measured-HRIR renderer, used when a database is loaded
    double         reverbSampleRate  = 44100.0;
    float          appliedReverbSend = -1.0f;      // last send pushed into `reverb` (-1 = none yet)
    juce::int64    lastEndSample     = -1;

    void setReverbSampleRate(double sr)
    {
        reverbSampleRate = sr;
        reverb.setSampleRate(sr);
        appliedReverbSend = -1.0f;                 // force the parameters to be re-applied
    }

    // The reverb is used as a WET-ONLY send: the Timeline feeds it the un-attenuated source and adds
    // its output to the spatialised dry signal. `send` is 0..1.
    // Cheap enough for the audio thread (no allocation); only touches the reverb when the value changes.
    void applyReverbSend(float send, bool force = false)
    {
        if (! force && std::abs(send - appliedReverbSend) < 0.002f)
            return;

        appliedReverbSend = send;

        juce::Reverb::Parameters p;
        p.roomSize   = 0.7f;
        p.damping    = 0.5f;
        p.width      = 1.0f;
        p.freezeMode = 0.0f;
        p.wetLevel   = send * 0.5f;
        p.dryLevel   = 0.0f;                       // dry path is handled by the panner
        reverb.setParameters(p);
    }

    void updateReverbParams()
    {
        applyReverbSend(reverbMix, true);
    }

    // deep copy of the settings, shares the (read-only) audio data
    Ptr clone() const
    {
        auto c = std::make_shared<AudioObject>();
        c->name            = name + " copy";
        c->file            = file;
        c->data            = data;
        c->thumb           = thumb;
        c->sampleRate      = sampleRate;
        c->sourceLengthSec = sourceLengthSec;
        c->startSec        = startSec;
        c->lengthSec       = lengthSec;
        c->volume          = volume;
        c->reverbMix       = reverbMix;
        c->speed           = speed;
        c->muted           = muted;
        c->position        = position;
        c->rotation        = rotation;
        c->setReverbSampleRate(reverbSampleRate);
        c->panner.prepare(reverbSampleRate);        // fresh delay buffer at the right rate
        c->hrir.setDatabase(hrir.getDatabase(), reverbSampleRate);
        c->updateReverbParams();
        return c;
    }
};
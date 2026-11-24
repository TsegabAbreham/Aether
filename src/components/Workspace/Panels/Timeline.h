#pragma once
#include <JuceHeader.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <functional>
#include <memory>
#include <vector>
#include <glm/glm.hpp>


#include "../../../Audio/Object/AudioObject.h"
#include "../../../Audio/Spatialization/SpatialMath.h"
#include "../../../Audio/Spatialization/HrirDatabase.h"

//==============================================================================
// Same palette as the File Explorer (kept local so this header has no extra includes)
namespace TimelineTheme
{
    const juce::Colour bar     { 0xff14161a };
    const juce::Colour panel   { 0xff1b1d23 };
    const juce::Colour laneAlt { 0xff1e2027 };
    const juce::Colour border  { 0xff2b2e36 };
    const juce::Colour text    { 0xffd5d8de };
    const juce::Colour textDim { 0xff6b7080 };
    const juce::Colour accent  { 0xff3ddcff };
    const juce::Colour muteCol { 0xffff9f4a };
    const juce::Colour soloCol { 0xfff2c94c };

    inline juce::Colour trackColour(int i)
    {
        static const juce::Colour c[] { juce::Colour(0xff3ddcff), juce::Colour(0xffff9f4a),
                                        juce::Colour(0xffa78bfa), juce::Colour(0xff6cb6ff),
                                        juce::Colour(0xfff2c94c), juce::Colour(0xff5be3a0) };
        return c[(size_t) i % 6];
    }
}

//==============================================================================
// Multi-track timeline. Every clip is an AudioObject (see AudioObject.h).
//
//   - drop audio from the in-app File Explorer OR from the OS file manager onto any track
//   - drop below the last track to create a new track
//   - click / drag the ruler (or empty lane space) to move the playhead, Space = play/pause
//   - drag clips around in time and between tracks, Delete removes the selected clip
//   - mouse wheel = scroll, Shift+wheel = horizontal scroll, Ctrl+wheel = zoom
//
// Spatialisation: every clip is rendered through its own BinauralPanner (BinauralPanner.h)
// or, when an HRIR file is loaded, through measured HRIRs. The clip's position relative to the
// fixed AudioListener is turned into angles by SpatialMath::getSourceAngles(). The angles are
// recomputed every audio block and the renderers ramp their parameters, so moving a clip is
// heard immediately, no clicks.
//
// HRIR: the constructor calls loadDefaultHrir(), which looks for d1.hrir next to the
// executable (the CMake post-build step copies it there). The Settings window can switch
// profiles later by calling loadDefaultHrir("h3.hrir") and so on.
//
// Hierarchy / Inspector talk to the timeline through the public "audio object" API below.
// Playback is self-contained. Pass your AudioDeviceManager to share it, or pass nothing
// and the timeline opens its own default output device.
//==============================================================================
class Timeline : public juce::Component,
                 public juce::FileDragAndDropTarget,
                 public juce::DragAndDropTarget,
                 public juce::AudioSource,
                 private juce::Timer,
                 private juce::ScrollBar::Listener
{
public:
    using Ptr = AudioObject::Ptr;

    //==========================================================================
    // Notifications (all fire on the message thread)
    //==========================================================================
    juce::ChangeBroadcaster listBroadcaster;        // objects added / removed / renamed / moved to another track
    juce::ChangeBroadcaster selectionBroadcaster;   // selected object changed
    juce::ChangeBroadcaster transformBroadcaster;   // selected object's position / rotation changed
    juce::ChangeBroadcaster viewBroadcaster;        // horizontal zoom or scroll changed

    std::function<void(const AudioObject&)> onObjectChanged;      // ANY property of an object changed
    std::function<void(const AudioObject&)> onTransformChanged;   // position or rotation changed

    //==========================================================================
    explicit Timeline(juce::AudioDeviceManager* sharedDeviceManager = nullptr)
    {
        formatManager.registerBasicFormats();

        for (auto* b : { &toStartButton, &playButton, &stopButton, &addTrackButton })
            addAndMakeVisible(b);

        toStartButton.onClick  = [this]
        {
            setPlayheadSeconds (0.0);
            viewStart = 0.0;
            updateScrollBars();
            repaint();
            viewBroadcaster.sendChangeMessage();
        };
        playButton.onClick     = [this] { togglePlay(); };
        stopButton.onClick     = [this] { stop(); };
        addTrackButton.onClick = [this] { addTrack(); };

        zoomSlider.setRange(10.0, 400.0, 0.0);
        zoomSlider.setSkewFactorFromMidPoint(60.0);
        zoomSlider.setValue(pps, juce::dontSendNotification);
        zoomSlider.setColour(juce::Slider::thumbColourId, TimelineTheme::accent);
        zoomSlider.setColour(juce::Slider::trackColourId, TimelineTheme::accent.withAlpha(0.35f));
        zoomSlider.setColour(juce::Slider::backgroundColourId, TimelineTheme::border);
        zoomSlider.onValueChange = [this]
        {
            zoomAround(zoomSlider.getValue(), (float) (headerW + lanesArea().getWidth() / 2));
        };
        addAndMakeVisible(zoomSlider);

        for (auto* sb : { &hBar, &vBar })
        {
            sb->setAutoHide(false);
            sb->setColour(juce::ScrollBar::thumbColourId, juce::Colour(0xff3a3e4a));
            sb->addListener(this);
            addAndMakeVisible(sb);
        }

        for (int i = 0; i < 4; ++i)
            addTrack();

        setWantsKeyboardFocus(true);

        if (sharedDeviceManager != nullptr)
        {
            deviceManager = sharedDeviceManager;
        }
        else
        {
            ownedDeviceManager = std::make_unique<juce::AudioDeviceManager>();
            ownedDeviceManager->initialise(0, 2, nullptr, true);
            deviceManager = ownedDeviceManager.get();
        }

        player.setSource(this);
        deviceManager->addAudioCallback(&player);

        // The device is open now, so prepareToPlay() has set the real sample rate and the
        // HRIR database can be resampled to it.
        loadDefaultHrir();

        startTimerHz(30);
    }

    ~Timeline() override
    {
        stopTimer();

        if (deviceManager != nullptr)
            deviceManager->removeAudioCallback(&player);

        player.setSource(nullptr);
    }

    //==========================================================================
    // Public API: transport / loading
    //==========================================================================
    bool addAudioFile(const juce::File& file, int trackIndex = -1, double startSeconds = 0.0)
    {
        auto clip = createClip(file);

        if (clip == nullptr)
            return false;

        clip->startSec = juce::jmax(0.0, startSeconds);

        if (trackIndex < 0)
            trackIndex = tracks.empty() ? addTrack() : 0;

        while (trackIndex >= (int) tracks.size())
            addTrack();

        {
            const juce::ScopedLock sl(lock);
            tracks[(size_t) trackIndex]->clips.push_back(clip);
        }

        setSelected(clip);
        notifyListChanged();
        updateScrollBars();
        repaint();
        viewBroadcaster.sendChangeMessage();
        return true;
    }

    int addTrack()
    {
        {
            const juce::ScopedLock sl(lock);
            auto t = std::make_unique<Track>();
            t->name   = "Track " + juce::String(++trackCounter);
            t->colour = TimelineTheme::trackColour((int) tracks.size());
            tracks.push_back(std::move(t));
        }

        updateScrollBars();
        repaint();
        return (int) tracks.size() - 1;
    }

    void play()
    {
        if (playSample.load() >= (juce::int64) (getProjectEnd() * deviceSR.load()) - 1)
            playSample.store(0);

        playing.store(true);
    }

    void pause()                          { playing.store(false); }
    void togglePlay()                     { if (playing.load()) pause(); else play(); }
    bool isPlaying() const                { return playing.load(); }

    void stop()
    {
        playing.store(false);
        setPlayheadSeconds(0.0);
        viewStart = 0.0;
        updateScrollBars();
        repaint();
        viewBroadcaster.sendChangeMessage();
    }

    void setPlayheadSeconds(double seconds)
    {
        playSample.store((juce::int64) (juce::jmax(0.0, seconds) * deviceSR.load()));
        repaintPlayhead();
    }

    double getPlayheadSeconds() const     { return (double) playSample.load() / deviceSR.load(); }

    static int getHeaderWidth() { return headerW; }
    double getPixelsPerSecond() const { return pps; }
    double getViewStart() const { return viewStart; }

    void setView (double startSeconds, double pixelsPerSecond)
    {
        pps = juce::jlimit (10.0, 400.0, pixelsPerSecond);
        viewStart = juce::jmax (0.0, startSeconds);
        zoomSlider.setValue (pps, juce::dontSendNotification);
        updateScrollBars();
        repaint();
        viewBroadcaster.sendChangeMessage();
    }

    void setAnimationEnd (double endSeconds)
    {
        const double clampedEnd = juce::jmax (0.0, endSeconds);
        if (animationEndSeconds.exchange (clampedEnd) != clampedEnd)
            updateScrollBars();
    }

    // global amount of distance-based room reverb: 0 = off, 1 = default, up to 2 = very wet
    void setRoomAmount(float amount)      { roomAmount.store(juce::jlimit(0.0f, 2.0f, amount)); }
    float getRoomAmount() const           { return roomAmount.load(); }

    //==========================================================================
    // HRIR rendering (message thread)
    //==========================================================================

    // Looks for the file next to the executable (or in assets/hrtf) and loads it.
    // Returns true if the HRIR is now active. On failure the previous HRIR stays active.
    bool loadDefaultHrir(const juce::String& fileName = "d1.hrir")
    {
        const auto exeDir = juce::File::getSpecialLocation(juce::File::currentApplicationFile)
                                .getParentDirectory();
        const auto cwd = juce::File::getCurrentWorkingDirectory();

        const juce::File candidates[]
        {
            exeDir.getChildFile(fileName),
            exeDir.getChildFile("assets/hrtf").getChildFile(fileName),
            cwd.getChildFile(fileName),
            cwd.getChildFile("assets/hrtf").getChildFile(fileName)
        };

        for (auto& f : candidates)
        {
            if (! f.existsAsFile())
                continue;

            if (loadHrirFile(f))
            {
                DBG("HRIR loaded: " << f.getFullPathName());
                return true;
            }

            // The previous HRIR stays active when a load fails.
            DBG("HRIR found but failed to load: " << f.getFullPathName());
            return false;
        }

        DBG("HRIR not found: " << fileName << " (looked next to the exe: "
            << exeDir.getFullPathName() << ")");
        return false;
    }

    // Loads a .hrir file made by the Colab converter, resamples it to the device rate and
    // starts using it. Call from the message thread. If it fails, the current HRIR (or the
    // simple binaural model) keeps playing.
    //
    // 'force' = reload even if this file is already active (used after a sample-rate change).
    bool loadHrirFile(const juce::File& file, bool force = false)
    {
        const double sr = deviceSR.load();

        if (sr <= 0.0)
        {
            DBG("HRIR load skipped: audio device is not running yet");
            return false;
        }

        // Already using this file, nothing to do (avoids clicks from a pointless reset)
        {
            const juce::ScopedLock sl(lock);

            if (! force && hrirDb != nullptr && hrirFile == file && ! hrirReloadPending.load())
                return true;
        }

        auto db = std::make_shared<HrirDatabase>();
        juce::String err;

        if (! db->load(file, err))
        {
            DBG("HRIR load failed: " << err);
            return false;
        }

        db->azimuthSign = hrirAzimuthSign;
        db->prepare(sr);                                // resample outside the lock

        std::shared_ptr<HrirDatabase> previous;         // freed after the lock is released

        {
            const juce::ScopedLock sl(lock);

            previous = std::move(hrirDb);
            hrirDb   = db;
            hrirFile = file;

            for (auto& t : tracks)
                for (auto& c : t->clips)
                {
                    c->hrir.setDatabase(hrirDb.get(), sr);
                    c->hrir.prepare(sr);
                    c->hrir.reset();
                }
        }

        hrirReloadPending.store(false);
        return true;                                    // 'previous' is destroyed here, outside the lock
    }

    bool isHrirLoaded() const             { return hrirDb != nullptr; }

    // true = HRIR (if loaded), false = the simple binaural model. Handy for A/B listening.
    void setUseHrir(bool shouldUse)       { useHrir.store(shouldUse); }
    bool getUseHrir() const               { return useHrir.load(); }

    // -1 mirrors left/right if the 90 degree test says the file's azimuth runs the other way
    void setHrirAzimuthSign(float sign)
    {
        hrirAzimuthSign = sign < 0.0f ? -1.0f : 1.0f;

        if (hrirDb != nullptr)
            hrirDb->azimuthSign = hrirAzimuthSign;
    }

    // which measurement a clip is currently using (for the verification test)
    juce::String describeHrir(const Ptr& o) const
    {
        if (o == nullptr || hrirDb == nullptr)
            return "HRIR: not loaded";

        const int i = o->hrir.getLastIndex();

        if (i < 0 || i >= hrirDb->getNumMeasurements())
            return "HRIR: idle (play the clip)";

        return "HRIR measurement " + juce::String(i)
             + "  azimuth " + juce::String(hrirDb->azimuthDeg(i), 1)
             + "  elevation " + juce::String(hrirDb->elevationDeg(i), 1)
             + "   (azimuth 90 = left, 270 = right)";
    }


    //==========================================================================
    // Public API: audio objects (message thread only)
    //==========================================================================

    // every clip currently on the timeline, ordered by track, then insertion order
    std::vector<Ptr> getAudioObjects() const
    {
        std::vector<Ptr> out;
        const juce::ScopedLock sl(lock);

        for (auto& t : tracks)
            for (auto& c : t->clips)
                out.push_back(c);

        return out;
    }

    int getNumAudioObjects() const
    {
        int n = 0;
        const juce::ScopedLock sl(lock);

        for (auto& t : tracks)
            n += (int) t->clips.size();

        return n;
    }

    Ptr  getSelectedObject() const                  { return selectedClip; }
    void selectObject(const Ptr& o)                 { setSelected(o); }

    int          getObjectTrackIndex(const Ptr& o) const { return trackIndexOf(o); }

    juce::String getObjectTrackName(const Ptr& o) const
    {
        const int i = trackIndexOf(o);
        return i >= 0 ? tracks[(size_t) i]->name : juce::String();
    }

    juce::Colour getObjectColour(const Ptr& o) const
    {
        const int i = trackIndexOf(o);
        return i >= 0 ? tracks[(size_t) i]->colour : TimelineTheme::textDim;
    }

    void removeObject(const Ptr& o)
    {
        if (o == nullptr)
            return;

        {
            const juce::ScopedLock sl(lock);

            for (auto& t : tracks)
                t->clips.erase(std::remove(t->clips.begin(), t->clips.end(), o), t->clips.end());
        }

        if (selectedClip == o)
            setSelected(nullptr);

        notifyListChanged();
        updateScrollBars();
        repaint();
    }

    void renameObject(const Ptr& o, const juce::String& newName)
    {
        const auto n = newName.trim();

        if (o == nullptr || n.isEmpty())
            return;

        o->name = n;                                // the audio thread never reads the name
        notifyListChanged();
        objectEdited(*o);
        repaint();
    }

    void setObjectMuted(const Ptr& o, bool shouldBeMuted)
    {
        if (o == nullptr) return;

        { const juce::ScopedLock sl(lock); o->muted = shouldBeMuted; }
        objectEdited(*o);
        notifyListChanged();                        // hierarchy shows the mute state
        repaint();
    }

    // linear gain, 0 .. 4  (1 = 0 dB)
    void setObjectVolume(const Ptr& o, float gain)
    {
        if (o == nullptr) return;

        { const juce::ScopedLock sl(lock); o->volume = juce::jlimit(0.0f, 4.0f, gain); }
        objectEdited(*o);
    }

    // 0 .. 1
    void setObjectReverb(const Ptr& o, float mix)
    {
        if (o == nullptr) return;

        {
            const juce::ScopedLock sl(lock);
            o->reverbMix = juce::jlimit(0.0f, 1.0f, mix);
            o->updateReverbParams();
        }
        objectEdited(*o);
    }

    // 0.25 .. 4. The clip is compressed / stretched on the timeline (its start stays put).
    void setObjectSpeed(const Ptr& o, float newSpeed)
    {
        if (o == nullptr) return;

        {
            const juce::ScopedLock sl(lock);
            o->speed     = juce::jlimit(0.25f, 4.0f, newSpeed);
            o->lengthSec = o->sourceLengthSec / (double) o->speed;
        }

        objectEdited(*o);
        updateScrollBars();
        repaint();
    }

    // World-space position. Accepts AudioVec3 or glm::vec3 (anything with .x .y .z).
    // The audio thread reads position every block, so it is written under the lock.
    template <typename Vec3>
    void setObjectPosition(const Ptr& o, const Vec3& p)
    {
        if (o == nullptr) return;

        {
            const juce::ScopedLock sl(lock);
            o->position.x = p.x;
            o->position.y = p.y;
            o->position.z = p.z;
        }

        objectEdited(*o);
        transformBroadcaster.sendChangeMessage();
        if (onTransformChanged) onTransformChanged(*o);
    }

    template <typename Vec3>
    void setObjectRotation(const Ptr& o, const Vec3& r)
    {
        if (o == nullptr) return;

        {
            const juce::ScopedLock sl(lock);
            o->rotation.x = r.x;
            o->rotation.y = r.y;
            o->rotation.z = r.z;
        }

        objectEdited(*o);
        transformBroadcaster.sendChangeMessage();
        if (onTransformChanged) onTransformChanged(*o);
    }

    //==========================================================================
    // AudioSource (runs on the audio thread)
    //==========================================================================
    void prepareToPlay(int, double sampleRate) override
    {
        deviceSR.store(sampleRate);

        const juce::ScopedLock sl(lock);

        for (auto& t : tracks)
            for (auto& c : t->clips)
            {
                c->setReverbSampleRate(sampleRate);
                c->panner.prepare(sampleRate);
                c->hrir.prepare(sampleRate);

                // database was resampled for another rate -> reload it on the message thread
                if (c->hrir.hasDatabase() && ! c->hrir.isReady())
                    hrirReloadPending.store(true);
            }
    }

    void releaseResources() override {}

    void getNextAudioBlock(const juce::AudioSourceChannelInfo& info) override
    {
        info.clearActiveBufferRegion();

        if (! playing.load())
            return;

        const juce::int64 start = playSample.load();
        const int         n     = info.numSamples;
        const double      sr    = deviceSR.load();
        juce::int64       maxEnd = 0;
        bool              gotLock = false;

        {
            const juce::ScopedTryLock sl(lock);
            gotLock = sl.isLocked();

            if (gotLock)
            {
                bool anySolo = false;
                for (auto& t : tracks)
                    if (t->solo.load()) anySolo = true;

                const int outCh = info.buffer->getNumChannels();

                for (auto& t : tracks)
                {
                    const bool audible = ! t->mute.load() && (! anySolo || t->solo.load());

                    for (auto& c : t->clips)
                    {
                        const juce::int64 cs = (juce::int64) std::llround(c->startSec  * sr);
                        const juce::int64 cl = (juce::int64) std::llround(c->lengthSec * sr);
                        maxEnd = juce::jmax(maxEnd, cs + cl);

                        if (! audible || c->muted)
                            continue;

                        const juce::int64 from = juce::jmax(start, cs);
                        const juce::int64 to   = juce::jmin(start + n, cs + cl);

                        if (to <= from)
                            continue;

                        // not contiguous with the previous block (seek / clip start) -> clear the tails
                        if (from != c->lastEndSample)
                        {
                            c->reverb.reset();
                            c->panner.reset();
                            c->hrir.reset();
                        }

                        const auto&  data   = *c->data;
                        const int    srcCh  = data.getNumChannels();
                        const int    srcLen = data.getNumSamples();
                        const double ratio  = (c->sampleRate / sr) * (double) c->speed;
                        const bool   useHrirNow = useHrir.load() && hrirDb != nullptr && c->hrir.isReady();

                        // ---- 3D spatialisation ------------------------------------------------
                        // angles + distance of this clip relative to the fixed listener
                        const auto angles = SpatialMath::getSourceAngles(
                            glm::vec3(c->position.x, c->position.y, c->position.z));

                        // ---- distance-based room reverb ---------------------------------------
                        // reverb send grows in direct proportion to distance, on top of the clip's
                        // own reverb amount. The reverb is fed the signal BEFORE distance
                        // attenuation, so far sources keep their reverb while the direct sound
                        // fades: the direct-to-reverb ratio is the main distance cue.
                        const float roomAdd = juce::jmin(roomReverbMaxAdd, roomReverbPerUnit * angles.distance)
                                              * roomAmount.load();
                        const float send    = juce::jlimit(0.0f, 1.0f, c->reverbMix + roomAdd);
                        const bool  wet     = send > 0.001f;
                        const float wetGain = c->volume / (1.0f + roomWetFalloff * angles.distance);

                        if (wet)
                            c->applyReverbSend(send);
                        // -----------------------------------------------------------------------

                        const float* s0 = data.getReadPointer(0);
                        const float* s1 = data.getReadPointer(srcCh > 1 ? 1 : 0);

                        for (juce::int64 cur = from; cur < to;)
                        {
                            const int m = (int) juce::jmin<juce::int64>(scratch.getNumSamples(), to - cur);
                            float* L = scratch.getWritePointer(0);
                            float* R = scratch.getWritePointer(1);

                            for (int i = 0; i < m; ++i)
                            {
                                const double pos = (double) (cur + i - cs) * ratio;
                                const int    i0  = juce::jmin(srcLen - 1, (int) pos);
                                const int    i1  = juce::jmin(srcLen - 1, i0 + 1);
                                const float  fr  = (float) (pos - (double) i0);

                                L[i] = s0[i0] + (s0[i1] - s0[i0]) * fr;
                                R[i] = s1[i0] + (s1[i1] - s1[i0]) * fr;
                            }

                            float* WL = wetScratch.getWritePointer(0);
                            float* WR = wetScratch.getWritePointer(1);

                            if (wet)                                    // reverb input = un-attenuated source
                            {
                                std::copy(L, L + m, WL);
                                std::copy(R, R + m, WR);
                            }

                            // direction + distance + volume: measured HRIRs if available, else the simple model
                            if (useHrirNow)
                                c->hrir.process(L, R, m, angles, c->volume);
                            else
                                c->panner.process(L, R, m, angles, c->volume);

                            if (wet)
                            {
                                c->reverb.processStereo(WL, WR, m);     // dryLevel = 0 -> wet signal only

                                for (int i = 0; i < m; ++i)
                                {
                                    L[i] += WL[i] * wetGain;
                                    R[i] += WR[i] * wetGain;
                                }
                            }

                            for (int ch = 0; ch < outCh; ++ch)
                            {
                                const float* src = scratch.getReadPointer(juce::jmin(ch, 1));
                                float* dst = info.buffer->getWritePointer(ch, info.startSample) + (cur - start);

                                for (int i = 0; i < m; ++i)
                                    dst[i] += src[i];
                            }

                            cur += m;
                        }

                        c->lastEndSample = to;
                    }
                }

                maxEnd = juce::jmax (maxEnd,
                                     (juce::int64) std::llround (
                                         animationEndSeconds.load() * sr));
                info.buffer->applyGain(info.startSample, n, masterGain.load());
            }
        }

        // only advance if the UI didn't seek in the meantime
        juce::int64 expected = start;
        playSample.compare_exchange_strong(expected, start + n);

        if (gotLock && start + n >= maxEnd)
            playing.store(false);                 // reached the end of the last clip
    }

    //==========================================================================
    // Painting
    //==========================================================================
    void paint(juce::Graphics& g) override
    {
        g.fillAll(TimelineTheme::panel);

        paintLanes(g);
        paintHeaders(g);
        paintRuler(g);
        paintToolbar(g);
        paintPlayhead(g);
    }

    void resized() override
    {
        int x = 92;
        for (auto* b : { &toStartButton, &playButton, &stopButton })
        {
            b->setBounds(x, (toolbarH - 24) / 2, 24, 24);
            x += 28;
        }

        x += 10;
        timeRect = { x, 6, 112, toolbarH - 12 };
        x += 112 + 10;
        addTrackButton.setBounds(x, (toolbarH - 24) / 2, 24, 24);

        zoomSlider.setBounds(getWidth() - 150, 0, 138, toolbarH);

        const auto lanes = lanesArea();

        vBar.setBounds(lanes.getRight(), lanes.getY(), vbarW, lanes.getHeight());
        hBar.setBounds(headerW, getHeight() - footerH + 4, lanes.getWidth(), 12);

        updateScrollBars();
    }

    //==========================================================================
    // Mouse / keyboard
    //==========================================================================
    void mouseDown(const juce::MouseEvent& e) override
    {
        grabKeyboardFocus();
        const auto p = e.getPosition();

        if (rulerArea().contains(p))
        {
            dragMode = DragMode::scrub;
            scrubTo(e.x);
            return;
        }

        if (headersArea().contains(p))
        {
            handleHeaderMouse(e);
            return;
        }

        if (lanesArea().contains(p))
        {
            if (auto clip = clipAt(p))
            {
                setSelected(clip);
                repaint();

                if (e.mods.isPopupMenu())
                {
                    showClipMenu(clip);
                    return;
                }

                dragMode = DragMode::clip;
                dragClip = clip;
                dragGrabOffset = xToTime((float) e.x) - clip->startSec;
            }
            else
            {
                setSelected(nullptr);
                repaint();

                if (! e.mods.isPopupMenu())
                {
                    dragMode = DragMode::scrub;
                    scrubTo(e.x);
                }
            }
        }
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (dragMode == DragMode::scrub)
        {
            scrubTo(e.x);
        }
        else if (dragMode == DragMode::clip && dragClip != nullptr && ! tracks.empty())
        {
            const double newStart = juce::jmax(0.0, xToTime((float) e.x) - dragGrabOffset);
            const int newTrack = juce::jlimit(0, (int) tracks.size() - 1, rawTrackIndexAtY(e.y));
            const int oldTrack = trackIndexOf(dragClip);
            bool changedTrack = false;

            {
                const juce::ScopedLock sl(lock);
                dragClip->startSec = newStart;

                if (oldTrack >= 0 && oldTrack != newTrack)
                {
                    auto& from = tracks[(size_t) oldTrack]->clips;
                    from.erase(std::remove(from.begin(), from.end(), dragClip), from.end());
                    tracks[(size_t) newTrack]->clips.push_back(dragClip);
                    changedTrack = true;
                }
            }

            if (changedTrack)
                notifyListChanged();

            updateScrollBars();
            repaint();
        }
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        if (dragMode == DragMode::clip && dragClip != nullptr)
            objectEdited(*dragClip);                    // start time changed

        dragMode = DragMode::none;
        dragClip = nullptr;
    }

    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        const auto p = e.getPosition();

        if (! (lanesArea().contains(p) || rulerArea().contains(p) || headersArea().contains(p)))
        {
            juce::Component::mouseWheelMove(e, w);
            return;
        }

        if (e.mods.isCtrlDown() || e.mods.isCommandDown())
        {
            zoomAround(pps * juce::jmax(0.5, 1.0 + (double) w.deltaY * 2.0), (float) juce::jmax(headerW, p.x));
            return;
        }

        if (e.mods.isShiftDown())
            viewStart -= (double) w.deltaY * 300.0 / pps;
        else
        {
            scrollY    -= (int) (w.deltaY * 300.0f);
            viewStart  -= (double) w.deltaX * 300.0 / pps;
        }

        clampView();
        updateScrollBars();
        repaint();
        viewBroadcaster.sendChangeMessage();
    }

    bool keyPressed(const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::spaceKey)   { togglePlay(); return true; }
        if (key == juce::KeyPress::homeKey)    { setPlayheadSeconds(0.0); return true; }

        if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
        {
            if (selectedClip != nullptr)
                removeObject(selectedClip);

            return true;
        }

        return false;
    }

    //==========================================================================
    // Drag & drop: files from the OS
    //==========================================================================
    bool isInterestedInFileDrag(const juce::StringArray& files) override
    {
        for (auto& f : files)
            if (isAudioFile(juce::File(f)))
                return true;

        return false;
    }

    void fileDragEnter(const juce::StringArray& files, int x, int y) override
    {
        for (auto& f : files)
            if (isAudioFile(juce::File(f))) { beginDropPreview(juce::File(f)); break; }

        updateDropPreview({ x, y });
    }

    void fileDragMove(const juce::StringArray&, int x, int y) override   { updateDropPreview({ x, y }); }
    void fileDragExit(const juce::StringArray&) override                 { endDropPreview(); }

    void filesDropped(const juce::StringArray& files, int x, int y) override
    {
        updateDropPreview({ x, y });

        int    track = dropTrack;
        double time  = dropTime;

        for (auto& f : files)
        {
            juce::File file(f);

            if (isAudioFile(file) && addAudioFile(file, track, time))
                ++track;                       // several files -> one per track
        }

        endDropPreview();
    }

    //==========================================================================
    // Drag & drop: from the in-app File Explorer (needs a DragAndDropContainer parent)
    //==========================================================================
    bool isInterestedInDragSource(const SourceDetails& d) override
    {
        return isAudioFile(fileFromDescription(d.description));
    }

    void itemDragEnter(const SourceDetails& d) override
    {
        beginDropPreview(fileFromDescription(d.description));
        updateDropPreview(d.localPosition);
    }

    void itemDragMove(const SourceDetails& d) override   { updateDropPreview(d.localPosition); }
    void itemDragExit(const SourceDetails&) override     { endDropPreview(); }
    bool shouldDrawDragImageWhenOver() override          { return true; }

    void itemDropped(const SourceDetails& d) override
    {
        updateDropPreview(d.localPosition);
        addAudioFile(fileFromDescription(d.description), dropTrack, dropTime);
        endDropPreview();
    }

private:
    //==========================================================================
    // Small vector-icon button (same look as the explorer's header buttons)
    //==========================================================================
    class TransportButton : public juce::Button
    {
    public:
        enum class Icon { toStart, play, pause, stop, addTrack };

        TransportButton(Icon i, const juce::String& tip) : juce::Button(tip), icon(i)
        {
            setTooltip(tip);
            setMouseCursor(juce::MouseCursor::PointingHandCursor);
        }

        void setIcon(Icon i) { if (icon != i) { icon = i; repaint(); } }

        void paintButton(juce::Graphics& g, bool over, bool down) override
        {
            auto bounds = getLocalBounds().toFloat();

            if (over || down)
            {
                g.setColour(juce::Colours::white.withAlpha(down ? 0.12f : 0.07f));
                g.fillRoundedRectangle(bounds.reduced(1.0f), 4.0f);
            }

            const bool accented = (icon == Icon::play || icon == Icon::pause);
            g.setColour(accented ? TimelineTheme::accent
                                 : (over ? juce::Colours::white : TimelineTheme::text.withAlpha(0.75f)));

            const auto c = bounds.getCentre();

            switch (icon)
            {
                case Icon::play:
                {
                    juce::Path p;
                    p.addTriangle(c.x - 4.0f, c.y - 6.0f, c.x - 4.0f, c.y + 6.0f, c.x + 6.0f, c.y);
                    g.fillPath(p);
                    break;
                }
                case Icon::pause:
                    g.fillRoundedRectangle(c.x - 5.0f, c.y - 6.0f, 3.5f, 12.0f, 1.0f);
                    g.fillRoundedRectangle(c.x + 1.5f, c.y - 6.0f, 3.5f, 12.0f, 1.0f);
                    break;

                case Icon::stop:
                    g.fillRoundedRectangle(c.x - 5.0f, c.y - 5.0f, 10.0f, 10.0f, 1.5f);
                    break;

                case Icon::toStart:
                {
                    g.fillRoundedRectangle(c.x - 6.5f, c.y - 6.0f, 2.0f, 12.0f, 1.0f);
                    juce::Path p;
                    p.addTriangle(c.x + 6.0f, c.y - 6.0f, c.x + 6.0f, c.y + 6.0f, c.x - 3.0f, c.y);
                    g.fillPath(p);
                    break;
                }
                case Icon::addTrack:
                    for (float dy : { -5.0f, 0.0f, 5.0f })
                        g.drawLine(c.x - 8.0f, c.y + dy, c.x + 1.0f, c.y + dy, 1.3f);

                    g.drawLine(c.x + 2.5f, c.y + 3.0f, c.x + 8.5f, c.y + 3.0f, 1.3f);
                    g.drawLine(c.x + 5.5f, c.y,        c.x + 5.5f, c.y + 6.0f, 1.3f);
                    break;
            }
        }

    private:
        Icon icon;
    };

    //==========================================================================
    // Data
    //==========================================================================
    struct Track
    {
        juce::String name;
        juce::Colour colour;
        std::atomic<bool> mute { false };
        std::atomic<bool> solo { false };
        std::vector<Ptr> clips;
    };

    enum class DragMode { none, scrub, clip };

    static constexpr int toolbarH = 36;
    static constexpr int rulerH   = 26;
    static constexpr int headerW  = 150;
    static constexpr int trackH   = 88;
    static constexpr int addRowH  = 34;
    static constexpr int footerH  = 16;
    static constexpr int vbarW    = 10;

    // distance-based reverb send: send = clip reverbMix + roomReverbPerUnit * distance (capped)
    static constexpr float roomReverbPerUnit = 0.005f;   // extra send per scene unit of distance
    static constexpr float roomReverbMaxAdd  = 0.70f;   // cap on the distance contribution
    static constexpr float roomWetFalloff    = 0.10f;   // wet level fades slowly with distance

    //==========================================================================
    // Notification helpers
    //==========================================================================
    void notifyListChanged()               { listBroadcaster.sendChangeMessage(); }
    void objectEdited(const AudioObject& o) { if (onObjectChanged) onObjectChanged(o); }

    void setSelected(const Ptr& p)
    {
        if (selectedClip == p)
            return;

        selectedClip = p;
        selectionBroadcaster.sendChangeMessage();
        repaint();
    }

    //==========================================================================
    // Geometry
    //==========================================================================
    juce::Rectangle<int> lanesArea() const
    {
        return { headerW, toolbarH + rulerH,
                 juce::jmax(0, getWidth() - headerW - vbarW),
                 juce::jmax(0, getHeight() - toolbarH - rulerH - footerH) };
    }

    juce::Rectangle<int> rulerArea() const   { return { headerW, toolbarH, lanesArea().getWidth(), rulerH }; }
    juce::Rectangle<int> headersArea() const { return { 0, toolbarH + rulerH, headerW, lanesArea().getHeight() }; }

    double visibleSeconds() const            { return (double) lanesArea().getWidth() / pps; }
    double xToTime(float x) const            { return ((double) x - headerW) / pps + viewStart; }
    float  timeToX(double t) const           { return (float) (headerW + (t - viewStart) * pps); }
    int    trackTop(int i) const             { return lanesArea().getY() + i * trackH - scrollY; }
    int    contentHeight() const             { return (int) tracks.size() * trackH + addRowH; }

    int rawTrackIndexAtY(int y) const
    {
        return (int) std::floor((double) (y - lanesArea().getY() + scrollY) / (double) trackH);
    }

    juce::Rectangle<int> muteRect(int i) const   { return { 14, trackTop(i) + trackH - 34, 28, 22 }; }
    juce::Rectangle<int> soloRect(int i) const   { return { 48, trackTop(i) + trackH - 34, 28, 22 }; }
    juce::Rectangle<int> removeRect(int i) const { return { headerW - 26, trackTop(i) + 8, 16, 16 }; }

    double getProjectEnd() const
    {
        double e = animationEndSeconds.load();
        for (auto& t : tracks)
            for (auto& c : t->clips)
                e = juce::jmax(e, c->startSec + c->lengthSec);
        return e;
    }

    void clampView()
    {
        const double total = juce::jmax(60.0, getProjectEnd() + 30.0);
        viewStart = juce::jlimit(0.0, juce::jmax(0.0, total - visibleSeconds()), viewStart);
        scrollY   = juce::jlimit(0, juce::jmax(0, contentHeight() - lanesArea().getHeight()), scrollY);
    }

    void updateScrollBars()
    {
        clampView();

        const double total = juce::jmax(juce::jmax(60.0, getProjectEnd() + 30.0), visibleSeconds());
        hBar.setRangeLimits(0.0, total);
        hBar.setCurrentRange(juce::Range<double>(viewStart, viewStart + visibleSeconds()), juce::dontSendNotification);

        vBar.setRangeLimits(0.0, (double) juce::jmax(contentHeight(), lanesArea().getHeight()));
        vBar.setCurrentRange(juce::Range<double>((double) scrollY, (double) (scrollY + lanesArea().getHeight())),
                             juce::dontSendNotification);
    }

    void scrollBarMoved(juce::ScrollBar* sb, double newStart) override
    {
        if (sb == &hBar)
        {
            viewStart = newStart;
            viewBroadcaster.sendChangeMessage();
        }
        else             scrollY   = (int) newStart;

        repaint();
    }

    void zoomAround(double newPps, float anchorX)
    {
        const double t = xToTime(anchorX);
        pps = juce::jlimit(10.0, 400.0, newPps);
        viewStart = t - ((double) anchorX - headerW) / pps;

        zoomSlider.setValue(pps, juce::dontSendNotification);
        updateScrollBars();
        repaint();
    }

    double tickStep() const
    {
        static const double steps[] { 0.05, 0.1, 0.25, 0.5, 1.0, 2.0, 5.0, 10.0, 15.0, 30.0, 60.0, 120.0, 300.0, 600.0 };

        for (double s : steps)
            if (s * pps >= 80.0)
                return s;

        return 600.0;
    }

    static juce::String formatTime(double t, bool ms)
    {
        t = juce::jmax(0.0, t);
        const int m = (int) (t / 60.0);
        const double s = t - m * 60.0;
        return ms ? juce::String::formatted("%02d:%06.3f", m, s)
                  : juce::String::formatted("%d:%02d", m, (int) s);
    }

    //==========================================================================
    // Painting helpers
    //==========================================================================
    void paintLanes(juce::Graphics& g)
    {
        const auto area = lanesArea();
        juce::Graphics::ScopedSaveState ss(g);
        g.reduceClipRegion(area);

        g.setColour(TimelineTheme::panel.darker(0.25f));
        g.fillRect(area);

        // rows
        for (int i = 0; i < (int) tracks.size(); ++i)
        {
            const int top = trackTop(i);

            if (top + trackH < area.getY() || top > area.getBottom())
                continue;

            g.setColour(i % 2 ? TimelineTheme::laneAlt : TimelineTheme::panel);
            g.fillRect(area.getX(), top, area.getWidth(), trackH);
            g.setColour(TimelineTheme::border);
            g.fillRect(area.getX(), top + trackH - 1, area.getWidth(), 1);
        }

        // vertical grid
        const double step = tickStep();
        g.setColour(TimelineTheme::border.withAlpha(0.55f));

        for (long long i = (long long) std::floor(viewStart / step);; ++i)
        {
            const float x = timeToX((double) i * step);
            if (x > (float) area.getRight()) break;
            g.fillRect((int) x, area.getY(), 1, area.getHeight());
        }

        // clips
        for (int ti = 0; ti < (int) tracks.size(); ++ti)
        {
            const auto& tr = *tracks[(size_t) ti];
            const int top = trackTop(ti);

            if (top + trackH < area.getY() || top > area.getBottom())
                continue;

            for (auto& clip : tr.clips)
            {
                const float x1 = timeToX(clip->startSec);
                const float x2 = timeToX(clip->startSec + clip->lengthSec);

                if (x2 < (float) area.getX() || x1 > (float) area.getRight())
                    continue;

                juce::Rectangle<float> r(x1, (float) top + 4.0f, juce::jmax(2.0f, x2 - x1), (float) trackH - 9.0f);
                const bool  sel = (clip == selectedClip);
                const float dim = clip->muted ? 0.35f : 1.0f;

                g.setColour(tr.colour.withAlpha((sel ? 0.30f : 0.16f) * dim));
                g.fillRoundedRectangle(r, 5.0f);

                // waveform (only the visible part). Timeline pixels -> source seconds via speed.
                auto vis = r.getIntersection(area.toFloat());

                if (clip->thumb != nullptr && vis.getWidth() >= 1.0f)
                {
                    const double t0 = (double) (vis.getX() - r.getX()) / pps * (double) clip->speed;
                    const double t1 = (double) (vis.getRight() - r.getX()) / pps * (double) clip->speed;

                    g.setColour(tr.colour.withAlpha(0.9f * dim));
                    clip->thumb->drawChannels(g, vis.withTrimmedTop(18.0f).reduced(0.0f, 2.0f).toNearestInt(),
                                              t0, t1, 1.0f);
                }

                g.setColour(sel ? juce::Colours::white.withAlpha(0.9f) : tr.colour.withAlpha(0.65f * dim));
                g.drawRoundedRectangle(r.reduced(0.5f), 5.0f, sel ? 1.6f : 1.2f);

                const float lx = juce::jmax(r.getX(), (float) area.getX()) + 6.0f;
                const float lw = r.getRight() - lx - 4.0f;

                if (lw > 12.0f)
                {
                    juce::String label = clip->name;

                    if (std::abs(clip->speed - 1.0f) > 0.001f)
                        label << "  " << juce::String(clip->speed, 2) << "x";

                    if (clip->muted)
                        label << "  (muted)";

                    g.setColour(TimelineTheme::text.withAlpha(dim < 1.0f ? 0.6f : 1.0f));
                    g.setFont(juce::Font(juce::FontOptions(11.5f)));
                    g.drawText(label,
                               juce::Rectangle<float>(lx, r.getY() + 2.0f, lw, 15.0f).toNearestInt(),
                               juce::Justification::centredLeft, true);
                }
            }
        }

        // drop preview
        if (dropActive)
        {
            const int top = trackTop(dropTrack);

            g.setColour(TimelineTheme::accent.withAlpha(0.06f));
            g.fillRect(area.getX(), top, area.getWidth(), trackH);

            juce::Rectangle<float> r(timeToX(dropTime), (float) top + 4.0f,
                                     (float) (dropDuration * pps), (float) trackH - 9.0f);

            g.setColour(TimelineTheme::accent.withAlpha(0.18f));
            g.fillRoundedRectangle(r, 5.0f);
            g.setColour(TimelineTheme::accent.withAlpha(0.9f));
            g.drawRoundedRectangle(r.reduced(0.5f), 5.0f, 1.4f);

            g.setFont(juce::Font(juce::FontOptions(11.5f)));
            g.drawText(dropName, r.reduced(6.0f, 2.0f).withHeight(15.0f).toNearestInt(),
                       juce::Justification::centredLeft, true);
        }
    }

    void paintHeaders(juce::Graphics& g)
    {
        // corner cell above the track list
        g.setColour(TimelineTheme::bar);
        g.fillRect(0, toolbarH, headerW, rulerH);
        g.setColour(TimelineTheme::textDim);
        g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
        g.drawText("TRACKS", juce::Rectangle<int>(12, toolbarH, headerW - 12, rulerH),
                   juce::Justification::centredLeft, true);

        const auto area = headersArea();
        juce::Graphics::ScopedSaveState ss(g);
        g.reduceClipRegion(area);

        g.setColour(TimelineTheme::bar);
        g.fillRect(area);

        for (int i = 0; i < (int) tracks.size(); ++i)
        {
            const auto& t = *tracks[(size_t) i];
            const int top = trackTop(i);

            if (top + trackH < area.getY() || top > area.getBottom())
                continue;

            g.setColour(t.colour);
            g.fillRect(0, top, 4, trackH - 1);

            g.setColour(TimelineTheme::text);
            g.setFont(juce::Font(juce::FontOptions(12.5f, juce::Font::bold)));
            g.drawText(t.name, juce::Rectangle<int>(14, top + 8, headerW - 44, 18),
                       juce::Justification::centredLeft, true);

            const int n = (int) t.clips.size();
            g.setColour(TimelineTheme::textDim);
            g.setFont(juce::Font(juce::FontOptions(11.0f)));
            g.drawText(juce::String(n) + (n == 1 ? " clip" : " clips"),
                       juce::Rectangle<int>(14, top + 28, headerW - 30, 16),
                       juce::Justification::centredLeft, true);

            drawToggle(g, muteRect(i), "M", t.mute.load(), TimelineTheme::muteCol);
            drawToggle(g, soloRect(i), "S", t.solo.load(), TimelineTheme::soloCol);

            // remove "x"
            const auto rr = removeRect(i).toFloat().reduced(4.5f);
            g.setColour(TimelineTheme::textDim);
            g.drawLine(rr.getX(), rr.getY(), rr.getRight(), rr.getBottom(), 1.2f);
            g.drawLine(rr.getX(), rr.getBottom(), rr.getRight(), rr.getY(), 1.2f);

            g.setColour(TimelineTheme::border);
            g.fillRect(0, top + trackH - 1, headerW, 1);
        }

        // "add track" row
        const int addTop = trackTop((int) tracks.size());
        g.setColour(TimelineTheme::textDim);
        g.setFont(juce::Font(juce::FontOptions(12.0f)));
        g.drawText("+  Add Track", juce::Rectangle<int>(0, addTop, headerW, addRowH), juce::Justification::centred);

        // divider
        g.setColour(TimelineTheme::border);
        g.fillRect(headerW - 1, area.getY(), 1, area.getHeight());
    }

    static void drawToggle(juce::Graphics& g, juce::Rectangle<int> r, const juce::String& label,
                           bool active, juce::Colour col)
    {
        if (active)
        {
            g.setColour(col);
            g.fillRoundedRectangle(r.toFloat(), 4.0f);
            g.setColour(TimelineTheme::bar);
        }
        else
        {
            g.setColour(TimelineTheme::border);
            g.drawRoundedRectangle(r.toFloat().reduced(0.5f), 4.0f, 1.0f);
            g.setColour(TimelineTheme::textDim);
        }

        g.setFont(juce::Font(juce::FontOptions(11.5f, juce::Font::bold)));
        g.drawText(label, r, juce::Justification::centred);
    }

    void paintRuler(juce::Graphics& g)
    {
        const auto r = rulerArea();
        juce::Graphics::ScopedSaveState ss(g);
        g.reduceClipRegion(r);

        g.setColour(TimelineTheme::bar);
        g.fillRect(r);
        g.setColour(TimelineTheme::border);
        g.fillRect(r.getX(), r.getBottom() - 1, r.getWidth(), 1);

        const double step = tickStep();
        const bool minor = step * pps / 4.0 >= 8.0;

        g.setFont(juce::Font(juce::FontOptions(11.0f)));

        for (long long i = (long long) std::floor(viewStart / step);; ++i)
        {
            const double t = (double) i * step;
            const float  x = timeToX(t);

            if (x > (float) r.getRight())
                break;

            g.setColour(TimelineTheme::textDim);
            g.fillRect((int) x, r.getBottom() - 10, 1, 10);

            const juce::String label = step < 1.0
                ? juce::String::formatted("%d:%04.1f", (int) (t / 60.0), t - std::floor(t / 60.0) * 60.0)
                : formatTime(t, false);

            g.drawText(label, juce::Rectangle<int>((int) x + 4, r.getY(), 70, r.getHeight() - 8),
                       juce::Justification::centredLeft, false);

            if (minor)
                for (int k = 1; k < 4; ++k)
                    g.fillRect((int) timeToX(t + step * k / 4.0), r.getBottom() - 5, 1, 5);
        }
    }

    void paintToolbar(juce::Graphics& g)
    {
        g.setColour(TimelineTheme::bar);
        g.fillRect(0, 0, getWidth(), toolbarH);
        g.setColour(TimelineTheme::border);
        g.fillRect(0, toolbarH - 1, getWidth(), 1);

        g.setColour(TimelineTheme::textDim);
        g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
        g.drawText("TIMELINE", juce::Rectangle<int>(12, 0, 76, toolbarH), juce::Justification::centredLeft, true);

        // time readout
        g.setColour(TimelineTheme::panel);
        g.fillRoundedRectangle(timeRect.toFloat(), 4.0f);
        g.setColour(TimelineTheme::border);
        g.drawRoundedRectangle(timeRect.toFloat().reduced(0.5f), 4.0f, 1.0f);

        g.setColour(TimelineTheme::accent);
        g.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 14.0f, juce::Font::bold)));
        g.drawText(formatTime(getPlayheadSeconds(), true), timeRect, juce::Justification::centred);

        g.setColour(TimelineTheme::textDim);
        g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
        g.drawText("ZOOM", juce::Rectangle<int>(getWidth() - 200, 0, 46, toolbarH), juce::Justification::centredRight);
    }

    void paintPlayhead(juce::Graphics& g)
    {
        const auto lanes = lanesArea();
        const float x = timeToX(getPlayheadSeconds());

        if (x < (float) lanes.getX() - 1.0f || x > (float) lanes.getRight())
            return;

        juce::Graphics::ScopedSaveState ss(g);
        g.reduceClipRegion(juce::Rectangle<int>(lanes.getX(), toolbarH, lanes.getWidth(), lanes.getBottom() - toolbarH));

        g.setColour(TimelineTheme::accent);
        g.fillRect(x - 0.75f, (float) (toolbarH + rulerH - 1), 1.5f, (float) (lanes.getBottom() - toolbarH - rulerH + 1));

        const float rb = (float) (toolbarH + rulerH);
        juce::Path tri;
        tri.addTriangle(x - 6.0f, rb - 13.0f, x + 6.0f, rb - 13.0f, x, rb - 1.0f);
        g.fillPath(tri);
    }

    void repaintPlayhead()
    {
        repaint(timeRect);

        const int x = (int) timeToX(getPlayheadSeconds());
        const int h = lanesArea().getBottom() - toolbarH;

        repaint(lastPlayheadX - 8, toolbarH, 16, h);
        repaint(x - 8, toolbarH, 16, h);
        lastPlayheadX = x;
    }

    void timerCallback() override
    {
        // The device sample rate changed: re-resample the HRIR for the new rate.
        // 'force' is required, otherwise loadHrirFile() would see the same file and skip the reload.
        if (hrirReloadPending.exchange(false) && hrirFile.existsAsFile())
            loadHrirFile(hrirFile, true);

        const bool nowPlaying = playing.load();

        if (nowPlaying != lastPlaying)
        {
            lastPlaying = nowPlaying;
            playButton.setIcon(nowPlaying ? TransportButton::Icon::pause : TransportButton::Icon::play);
        }

        const double t = getPlayheadSeconds();

        if (nowPlaying && dragMode != DragMode::scrub)
        {
            const double vis = visibleSeconds();

            if (t > viewStart + vis || t < viewStart)           // page-flip follow
            {
                viewStart = juce::jmax(0.0, t - vis * 0.1);
                updateScrollBars();
                repaint();
                viewBroadcaster.sendChangeMessage();
                lastPaintedTime = t;
                return;
            }
        }

        if (t != lastPaintedTime)
        {
            lastPaintedTime = t;
            repaintPlayhead();
        }
    }

    //==========================================================================
    // Interaction helpers
    //==========================================================================
    void scrubTo(int x)
    {
        setPlayheadSeconds(xToTime((float) x));
    }

    Ptr clipAt(juce::Point<int> p) const
    {
        const int ti = rawTrackIndexAtY(p.y);

        if (! juce::isPositiveAndBelow(ti, (int) tracks.size()))
            return nullptr;

        const auto& clips = tracks[(size_t) ti]->clips;

        for (auto it = clips.rbegin(); it != clips.rend(); ++it)
        {
            const float x1 = timeToX((*it)->startSec);
            const float x2 = timeToX((*it)->startSec + (*it)->lengthSec);

            if ((float) p.x >= x1 && (float) p.x <= x2)
                return *it;
        }

        return nullptr;
    }

    int trackIndexOf(const Ptr& clip) const
    {
        for (int i = 0; i < (int) tracks.size(); ++i)
            for (auto& c : tracks[(size_t) i]->clips)
                if (c == clip)
                    return i;

        return -1;
    }

    void removeTrack(int index)
    {
        if (! juce::isPositiveAndBelow(index, (int) tracks.size()))
            return;

        {
            const juce::ScopedLock sl(lock);
            tracks.erase(tracks.begin() + index);
        }

        if (selectedClip != nullptr && trackIndexOf(selectedClip) < 0)
            setSelected(nullptr);

        notifyListChanged();
        updateScrollBars();
        repaint();
    }

    void handleHeaderMouse(const juce::MouseEvent& e)
    {
        const auto p = e.getPosition();
        const int idx = rawTrackIndexAtY(p.y);

        if (idx == (int) tracks.size())
        {
            if (! e.mods.isPopupMenu())
                addTrack();

            return;
        }

        if (! juce::isPositiveAndBelow(idx, (int) tracks.size()))
            return;

        auto& t = *tracks[(size_t) idx];

        if (e.mods.isPopupMenu())
        {
            showTrackMenu(idx);
            return;
        }

        if      (muteRect(idx).contains(p))   t.mute.store(! t.mute.load());
        else if (soloRect(idx).contains(p))   t.solo.store(! t.solo.load());
        else if (removeRect(idx).contains(p)) { removeTrack(idx); return; }

        repaint(headersArea());
    }

    void showClipMenu(Ptr clip)
    {
        juce::PopupMenu m;
        m.addItem(1, "Duplicate");
        m.addItem(2, clip->muted ? "Unmute" : "Mute");
        m.addSeparator();
        m.addItem(3, "Remove Clip");

        juce::Component::SafePointer<Timeline> safe(this);

        m.showMenuAsync(juce::PopupMenu::Options(), [safe, clip](int result)
        {
            if (safe == nullptr)
                return;

            if (result == 3)
                safe->removeObject(clip);
            else if (result == 2)
                safe->setObjectMuted(clip, ! clip->muted);
            else if (result == 1)
            {
                const int ti = safe->trackIndexOf(clip);

                if (ti >= 0)
                {
                    auto copy = clip->clone();
                    copy->startSec = clip->startSec + clip->lengthSec;

                    {
                        const juce::ScopedLock sl(safe->lock);
                        safe->tracks[(size_t) ti]->clips.push_back(copy);
                    }

                    safe->setSelected(copy);
                    safe->notifyListChanged();
                    safe->updateScrollBars();
                    safe->repaint();
                }
            }
        });
    }

    void showTrackMenu(int index)
    {
        juce::PopupMenu m;
        m.addItem(1, "Clear Track");
        m.addItem(2, "Remove Track");

        juce::Component::SafePointer<Timeline> safe(this);

        m.showMenuAsync(juce::PopupMenu::Options(), [safe, index](int result)
        {
            if (safe == nullptr || ! juce::isPositiveAndBelow(index, (int) safe->tracks.size()))
                return;

            if (result == 1)
            {
                {
                    const juce::ScopedLock sl(safe->lock);
                    safe->tracks[(size_t) index]->clips.clear();
                }

                if (safe->selectedClip != nullptr && safe->trackIndexOf(safe->selectedClip) < 0)
                    safe->setSelected(nullptr);

                safe->notifyListChanged();
                safe->updateScrollBars();
                safe->repaint();
            }
            else if (result == 2)
                safe->removeTrack(index);
        });
    }

    //==========================================================================
    // Drop preview
    //==========================================================================
    static bool isAudioFile(const juce::File& f)
    {
        static const juce::StringArray exts { "wav", "aif", "aiff", "flac", "ogg", "mp3", "m4a", "wma" };
        return f.existsAsFile() && exts.contains(f.getFileExtension().trimCharactersAtStart(".").toLowerCase());
    }

    static juce::File fileFromDescription(const juce::var& d)
    {
        const auto s = d.toString();
        return juce::File::isAbsolutePath(s) ? juce::File(s) : juce::File();
    }

    void beginDropPreview(const juce::File& file)
    {
        dropName = file.getFileName();
        dropDuration = 3.0;

        std::unique_ptr<juce::AudioFormatReader> r(formatManager.createReaderFor(file));

        if (r != nullptr && r->sampleRate > 0.0)
            dropDuration = (double) r->lengthInSamples / r->sampleRate;

        dropActive = true;
    }

    void updateDropPreview(juce::Point<int> p)
    {
        const auto lanes = lanesArea();

        const int x = juce::jmax(p.x, lanes.getX());
        const int y = juce::jlimit(lanes.getY(), juce::jmax(lanes.getY(), lanes.getBottom() - 1), p.y);

        dropTime  = juce::jmax(0.0, xToTime((float) x));
        dropTrack = juce::jlimit(0, (int) tracks.size(), rawTrackIndexAtY(y));   // == size -> new track
        dropActive = true;
        repaint(lanes);
    }

    void endDropPreview()
    {
        dropActive = false;
        repaint(lanesArea());
    }

    //==========================================================================
    // Loading
    //==========================================================================
    Ptr createClip(const juce::File& file)
    {
        std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));

        if (reader == nullptr || reader->lengthInSamples <= 0 || reader->sampleRate <= 0.0)
            return nullptr;

        const int numCh = reader->numChannels >= 2 ? 2 : 1;
        const int len   = (int) juce::jmin<juce::int64>(reader->lengthInSamples, 0x7fffffff);

        auto clip = std::make_shared<AudioObject>();
        clip->name            = file.getFileNameWithoutExtension();
        clip->file            = file;
        clip->sampleRate      = reader->sampleRate;
        clip->sourceLengthSec = (double) len / reader->sampleRate;
        clip->lengthSec       = clip->sourceLengthSec;
        clip->data            = std::make_shared<juce::AudioBuffer<float>>(numCh, len);
        clip->setReverbSampleRate(deviceSR.load());
        clip->panner.prepare(deviceSR.load());
        clip->hrir.setDatabase(hrirDb.get(), deviceSR.load());

        if (hrirDb != nullptr && ! clip->hrir.isReady())
            hrirReloadPending.store(true);
        clip->updateReverbParams();

        reader->read(clip->data.get(), 0, len, 0, true, numCh > 1);

        clip->thumb = std::make_shared<juce::AudioThumbnail>(256, formatManager, thumbCache);
        clip->thumb->setSource(clip->data.get(), reader->sampleRate, file.hashCode64() + (++thumbCounter));

        return clip;
    }

    //==========================================================================
    // Members (declaration order matters for destruction)
    //==========================================================================
    juce::AudioFormatManager   formatManager;
    juce::AudioThumbnailCache  thumbCache { 32 };
    juce::CriticalSection      lock;

    std::vector<std::unique_ptr<Track>> tracks;

    std::unique_ptr<juce::AudioDeviceManager> ownedDeviceManager;
    juce::AudioDeviceManager*  deviceManager = nullptr;
    juce::AudioSourcePlayer    player;

    std::atomic<bool>          playing    { false };
    std::atomic<juce::int64>   playSample { 0 };
    std::atomic<double>        deviceSR   { 44100.0 };
    std::atomic<float>         masterGain { 1.0f };
    std::atomic<float>         roomAmount { 1.0f };     // global multiplier for the distance reverb

    std::shared_ptr<HrirDatabase> hrirDb;               // shared, read-only once prepared
    juce::File                 hrirFile;
    float                      hrirAzimuthSign = 1.0f;
    std::atomic<bool>          useHrir { true };
    std::atomic<bool>          hrirReloadPending { false };

    juce::AudioBuffer<float>   scratch    { 2, 4096 };  // per-clip render buffer (audio thread)
    juce::AudioBuffer<float>   wetScratch { 2, 4096 };  // reverb input/output (audio thread)

    TransportButton toStartButton  { TransportButton::Icon::toStart,  "Go to Start" };
    TransportButton playButton     { TransportButton::Icon::play,     "Play / Pause (Space)" };
    TransportButton stopButton     { TransportButton::Icon::stop,     "Stop" };
    TransportButton addTrackButton { TransportButton::Icon::addTrack, "Add Track" };
    juce::Slider    zoomSlider { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::ScrollBar hBar { false }, vBar { true };
    juce::Rectangle<int> timeRect;

    double pps       = 60.0;     // pixels per second
    double viewStart = 0.0;      // seconds at the left edge of the lanes
    std::atomic<double> animationEndSeconds { 0.0 };
    int    scrollY   = 0;

    DragMode dragMode = DragMode::none;
    Ptr dragClip, selectedClip;
    double dragGrabOffset = 0.0;

    bool   dropActive = false;
    int    dropTrack = 0;
    double dropTime = 0.0, dropDuration = 3.0;
    juce::String dropName;

    bool   lastPlaying = false;
    double lastPaintedTime = -1.0;
    int    lastPlayheadX = 0;
    int    trackCounter = 0;
    juce::int64 thumbCounter = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Timeline)
};
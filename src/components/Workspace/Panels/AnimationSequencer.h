#pragma once
#include <JuceHeader.h>
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <memory>
#include <vector>
#include "Timeline.h"

//==============================================================================
// Animation Sequencer
//
// A second timeline-style panel that animates the POSITION of audio objects.
// It reads / writes positions exactly like the Inspector does:
//      read : AudioObject::position
//      write: Timeline::setObjectPosition(obj, vec3)
//
//   - one lane per audio object (same order as the Timeline / Hierarchy)
//   - keyframes are drawn as diamonds, the X / Y / Z curves are drawn faintly behind them
//   - RECORD: arm it, press play (Space) and move the selected object (Inspector, gizmo,
//             anything that changes AudioObject::position). Keyframes are written while you move.
//             Recording freezes playback of the recorded object so it can't fight you,
//             all other objects keep playing their animation (layered recording).
//   - ADD KEY (K) writes a key at the playhead with the object's current position
//   - drag a key to move it in time, Delete removes it, right click for more
//   - double click a lane to add a key there
//   - between keys the position is interpolated linearly (before the first / after the last
//     key the object holds that key's value)
//
// Sync with the Timeline (both directions):
//   - ONE shared playhead / transport: scrubbing, play, pause and stop in either panel
//     act on the same playhead (the sequencer reads Timeline::getPlayheadSeconds())
//   - ONE shared view: zoom + horizontal scroll are mirrored (Timeline::setView /
//     Timeline::viewBroadcaster), and both panels use the same header width, so a given
//     time is at the same x position in both panels when they have the same width
//   - the animation length extends the project end (Timeline::setAnimationEnd) so
//     playback doesn't stop at the end of the last audio clip while keys remain
//
// NOTE: Declare Timeline BEFORE AnimationSequencer in the parent component so Timeline
//       outlives it during destruction (same rule as Inspector).
//
// Usage:   animationSequencer.setTimeline(&timeline);
//==============================================================================
class AnimationSequencer : public juce::Component,
                           private juce::ChangeListener,
                           private juce::Timer,
                           private juce::ScrollBar::Listener
{
public:
    // Fires on the message thread whenever any keyframe is added / moved / removed
    std::function<void()> onKeysChanged;

    //==========================================================================
    AnimationSequencer()
    {
        for (auto* b : { &toStartButton, &playButton, &stopButton, &recordButton, &addKeyButton, &delKeyButton })
            addAndMakeVisible(b);

        toStartButton.onClick = [this]
        {
            if (timeline == nullptr) return;
            timeline->setPlayheadSeconds(0.0);
            timeline->setView(0.0, timeline->getPixelsPerSecond());
            pullView();
        };
        playButton.onClick   = [this] { if (timeline != nullptr) timeline->togglePlay(); };
        stopButton.onClick   = [this] { if (timeline != nullptr) timeline->stop(); };
        recordButton.onClick = [this] { setRecording(! recording); };
        addKeyButton.onClick = [this] { addKeyForSelected(); };
        delKeyButton.onClick = [this] { deleteSelectedKey(); };

        vBar.setAutoHide(false);
        vBar.setColour(juce::ScrollBar::thumbColourId, juce::Colour(0xff3a3e4a));
        vBar.addListener(this);
        addAndMakeVisible(vBar);

        setWantsKeyboardFocus(true);
        startTimerHz(60);
    }

    ~AnimationSequencer() override
    {
        stopTimer();
        setTimeline(nullptr);
    }

    //==========================================================================
    void setTimeline(Timeline* newTimeline)
    {
        if (timeline == newTimeline)
            return;

        if (timeline != nullptr)
        {
            timeline->selectionBroadcaster.removeChangeListener(this);
            timeline->listBroadcaster.removeChangeListener(this);
            timeline->viewBroadcaster.removeChangeListener(this);
            timeline->setAnimationEnd(0.0);
        }

        timeline = newTimeline;
        objects.clear();

        if (timeline != nullptr)
        {
            timeline->selectionBroadcaster.addChangeListener(this);
            timeline->listBroadcaster.addChangeListener(this);
            timeline->viewBroadcaster.addChangeListener(this);

            refreshObjects();
            pullView();
            updateAnimationEnd();
            evalDirty = true;
        }

        updateScrollBars();
        repaint();
    }

    //==========================================================================
    // Public API
    //==========================================================================
    bool isRecording() const { return recording; }

    void setRecording(bool shouldRecord)
    {
        if (recording == shouldRecord)
            return;

        recording = shouldRecord;
        recordButton.setToggleState(recording, juce::dontSendNotification);
        recObj = nullptr;                               // re-baseline on the next tick

        if (! recording)
        {
            // thin out the dense recorded keys (collinear ones are removed)
            for (auto& entry : tracks)
                simplifyRecordedKeys(entry.second);

            validateSelection();
            updateAnimationEnd();
            evalDirty = true;

            if (onKeysChanged) onKeysChanged();
        }

        repaint();
    }

    // key at the playhead with the object's current position
    void addKeyForSelected()
    {
        if (timeline == nullptr) return;

        auto sel = timeline->getSelectedObject();
        if (sel == nullptr) return;

        const double t = timeline->getPlayheadSeconds();
        const int keyId = addOrReplaceKey(sel->id, t, positionOf(*sel), false);

        selTrack = sel->id;
        selKeyId = keyId;
        markChanged();
    }

    void deleteSelectedKey()
    {
        if (selKeyId < 0) return;

        auto it = tracks.find(selTrack);

        if (it != tracks.end())
        {
            auto& keys = it->second.keys;
            keys.erase(std::remove_if(keys.begin(), keys.end(),
                                      [this](const Key& k) { return k.id == selKeyId; }),
                       keys.end());
        }

        selKeyId = -1;
        markChanged();
    }

    int getNumKeys(const AudioObject::Ptr& o) const
    {
        if (o == nullptr) return 0;
        auto it = tracks.find(o->id);
        return it != tracks.end() ? (int) it->second.keys.size() : 0;
    }

    void clearAllAnimation()
    {
        tracks.clear();
        selKeyId = -1;
        markChanged();
    }

    //==========================================================================
    // Component
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

        for (auto* b : { &recordButton, &addKeyButton, &delKeyButton })
        {
            b->setBounds(x, (toolbarH - 24) / 2, 24, 24);
            x += 28;
        }

        statusX = x + 8;

        const auto lanes = lanesArea();
        vBar.setBounds(lanes.getRight(), lanes.getY(), vbarW, lanes.getHeight());

        updateScrollBars();
    }

    //==========================================================================
    // Mouse / keyboard
    //==========================================================================
    void mouseDown(const juce::MouseEvent& e) override
    {
        grabKeyboardFocus();

        if (timeline == nullptr)
            return;

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

        if (! lanesArea().contains(p))
            return;

        const int lane = laneAtY(p.y);

        if (! juce::isPositiveAndBelow(lane, (int) objects.size()))
        {
            selKeyId = -1;
            repaint();
            return;
        }

        auto obj = objects[(size_t) lane];
        int hitKey = -1;
        const bool onKey = keyAt(p, lane, hitKey);

        if (e.mods.isPopupMenu())
        {
            timeline->selectObject(obj);

            if (onKey)
            {
                selTrack = obj->id;
                selKeyId = hitKey;
                repaint();
                showKeyMenu(obj->id, hitKey);
            }
            else
                showLaneMenu(obj, xToTime((float) e.x));

            return;
        }

        timeline->selectObject(obj);

        if (onKey)
        {
            selTrack = obj->id;
            selKeyId = hitKey;

            if (auto* k = findKey(selTrack, selKeyId))
                dragGrabOffset = xToTime((float) e.x) - k->time;

            dragMode = DragMode::key;
            repaint();
        }
        else
        {
            selKeyId = -1;

            if (e.getNumberOfClicks() >= 2)
            {
                const double t = juce::jmax(0.0, xToTime((float) e.x));
                const int id = addOrReplaceKey(obj->id, t, positionOf(*obj), false);
                selTrack = obj->id;
                selKeyId = id;
                markChanged();
            }
            else
            {
                dragMode = DragMode::scrub;
                scrubTo(e.x);
            }

            repaint();
        }
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (timeline == nullptr)
            return;

        if (dragMode == DragMode::scrub)
        {
            scrubTo(e.x);
        }
        else if (dragMode == DragMode::key && selKeyId >= 0)
        {
            auto it = tracks.find(selTrack);
            if (it == tracks.end()) return;

            for (auto& k : it->second.keys)
                if (k.id == selKeyId)
                {
                    k.time = juce::jmax(0.0, xToTime((float) e.x) - dragGrabOffset);
                    break;
                }

            sortKeys(it->second.keys);
            markChanged();
        }
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        dragMode = DragMode::none;
    }

    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        if (timeline == nullptr)
            return;

        const auto p = e.getPosition();

        if (! (lanesArea().contains(p) || rulerArea().contains(p) || headersArea().contains(p)))
        {
            juce::Component::mouseWheelMove(e, w);
            return;
        }

        if (e.mods.isCtrlDown() || e.mods.isCommandDown())
        {
            zoomAround(pps * juce::jmax(0.5, 1.0 + (double) w.deltaY * 2.0), (float) juce::jmax(headerW(), p.x));
            return;
        }

        if (e.mods.isShiftDown())
        {
            viewStart -= (double) w.deltaY * 300.0 / pps;
            pushView();
        }
        else
        {
            scrollY -= (int) (w.deltaY * 300.0f);

            if (w.deltaX != 0.0f)
            {
                viewStart -= (double) w.deltaX * 300.0 / pps;
                pushView();
            }
        }

        updateScrollBars();
        repaint();
    }

    bool keyPressed(const juce::KeyPress& key) override
    {
        if (timeline == nullptr)
            return false;

        if (key == juce::KeyPress::spaceKey)  { timeline->togglePlay(); return true; }
        if (key == juce::KeyPress::homeKey)   { timeline->setPlayheadSeconds(0.0); return true; }

        if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
        {
            deleteSelectedKey();
            return true;
        }

        if (key.getTextCharacter() == 'k' || key.getTextCharacter() == 'K')
        {
            addKeyForSelected();
            return true;
        }

        if (key.getTextCharacter() == 'r' || key.getTextCharacter() == 'R')
        {
            setRecording(! recording);
            return true;
        }

        return false;
    }

private:
    //==========================================================================
    // Small types
    //==========================================================================
    struct Vec3f { float x = 0.0f, y = 0.0f, z = 0.0f; };

    struct Key
    {
        int    id       = 0;
        double time     = 0.0;
        Vec3f  pos;
        bool   recorded = false;      // created by the recorder (eligible for simplification)
    };

    struct ObjectTrack
    {
        std::vector<Key> keys;        // always sorted by time
    };

    enum class DragMode { none, scrub, key };

    //==========================================================================
    // Transport / tool button
    //==========================================================================
    class SeqButton : public juce::Button
    {
    public:
        enum class Icon { toStart, play, pause, stop, record, addKey, delKey };

        SeqButton(Icon i, const juce::String& tip) : juce::Button(tip), icon(i)
        {
            setTooltip(tip);
            setMouseCursor(juce::MouseCursor::PointingHandCursor);
        }

        void setIcon(Icon i) { if (icon != i) { icon = i; repaint(); } }

        void paintButton(juce::Graphics& g, bool over, bool down) override
        {
            auto bounds = getLocalBounds().toFloat();
            const bool armed = (icon == Icon::record && getToggleState());

            if (over || down || armed)
            {
                g.setColour(armed ? recordCol.withAlpha(0.20f)
                                  : juce::Colours::white.withAlpha(down ? 0.12f : 0.07f));
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
                case Icon::record:
                    g.setColour(recordCol.withAlpha(armed ? 1.0f : (over ? 0.95f : 0.75f)));
                    g.fillEllipse(c.x - 5.5f, c.y - 5.5f, 11.0f, 11.0f);

                    if (! armed)
                    {
                        g.setColour(TimelineTheme::bar);
                        g.fillEllipse(c.x - 2.5f, c.y - 2.5f, 5.0f, 5.0f);
                    }
                    break;

                case Icon::addKey:
                {
                    juce::Path d;
                    d.addQuadrilateral(c.x - 2.0f, c.y - 6.0f, c.x + 4.0f, c.y, c.x - 2.0f, c.y + 6.0f, c.x - 8.0f, c.y);
                    g.fillPath(d);
                    g.setColour(TimelineTheme::accent);
                    g.drawLine(c.x + 4.0f, c.y - 6.0f, c.x + 4.0f, c.y - 1.0f, 1.4f);
                    g.drawLine(c.x + 1.5f, c.y - 3.5f, c.x + 6.5f, c.y - 3.5f, 1.4f);
                    break;
                }
                case Icon::delKey:
                {
                    juce::Path d;
                    d.addQuadrilateral(c.x - 2.0f, c.y - 6.0f, c.x + 4.0f, c.y, c.x - 2.0f, c.y + 6.0f, c.x - 8.0f, c.y);
                    g.fillPath(d);
                    g.setColour(TimelineTheme::muteCol);
                    g.drawLine(c.x + 2.0f, c.y - 6.0f, c.x + 7.0f, c.y - 1.0f, 1.4f);
                    g.drawLine(c.x + 7.0f, c.y - 6.0f, c.x + 2.0f, c.y - 1.0f, 1.4f);
                    break;
                }
            }
        }

        static inline const juce::Colour recordCol { 0xffff4d4d };

    private:
        Icon icon;
    };

    //==========================================================================
    // Constants
    //==========================================================================
    static constexpr int toolbarH = 36;
    static constexpr int rulerH   = 26;
    static constexpr int laneH    = 64;
    static constexpr int vbarW    = 10;

    static constexpr double keyMergeGap = 0.02;    // keys closer than this (seconds) are merged
    static constexpr double holdGap     = 0.15;    // idle time after which the recorder writes a "hold" key
    static constexpr double holdOffset  = 0.05;    // ... this long before the new move

    static int headerW() { return Timeline::getHeaderWidth(); }

    //==========================================================================
    // Helpers: vectors, keys, interpolation
    //==========================================================================
    static Vec3f positionOf(const AudioObject& o)
    {
        return { (float) o.position.x, (float) o.position.y, (float) o.position.z };
    }

    static Vec3f lerp(const Vec3f& a, const Vec3f& b, float f)
    {
        return { a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * f, a.z + (b.z - a.z) * f };
    }

    static bool nearlyEqual(const Vec3f& a, const Vec3f& b, float eps = 1.0e-4f)
    {
        return std::abs(a.x - b.x) <= eps && std::abs(a.y - b.y) <= eps && std::abs(a.z - b.z) <= eps;
    }

    static void sortKeys(std::vector<Key>& keys)
    {
        std::stable_sort(keys.begin(), keys.end(),
                         [](const Key& a, const Key& b) { return a.time < b.time; });
    }

    static bool sampleTrack(const ObjectTrack& tr, double t, Vec3f& out)
    {
        const auto& k = tr.keys;

        if (k.empty())              return false;
        if (t <= k.front().time)    { out = k.front().pos; return true; }
        if (t >= k.back().time)     { out = k.back().pos;  return true; }

        auto it = std::upper_bound(k.begin(), k.end(), t,
                                   [](double v, const Key& key) { return v < key.time; });

        const Key& b = *it;
        const Key& a = *(it - 1);
        const double span = b.time - a.time;
        const float  f = span > 1.0e-9 ? (float) ((t - a.time) / span) : 1.0f;

        out = lerp(a.pos, b.pos, f);
        return true;
    }

    // Adds a key, or replaces the nearest one when it is closer than keyMergeGap.
    // Returns the id of the key that now exists at that time.
    int addOrReplaceKey(const juce::Uuid& objId, double time, const Vec3f& pos, bool recorded)
    {
        auto& keys = tracks[objId].keys;
        time = juce::jmax(0.0, time);

        int best = -1;
        double bestD = keyMergeGap;

        for (int i = 0; i < (int) keys.size(); ++i)
        {
            const double d = std::abs(keys[(size_t) i].time - time);

            if (d <= bestD)
            {
                bestD = d;
                best = i;
            }
        }

        int id;

        if (best >= 0)
        {
            auto& k = keys[(size_t) best];
            k.time = time;
            k.pos = pos;
            k.recorded = recorded;
            id = k.id;
        }
        else
        {
            Key k;
            k.id = nextKeyId++;
            k.time = time;
            k.pos = pos;
            k.recorded = recorded;
            keys.push_back(k);
            id = k.id;
        }

        sortKeys(keys);
        return id;
    }

    // removes recorded keys that lie (almost) on the line between their neighbours
    static void simplifyRecordedKeys(ObjectTrack& tr, float eps = 0.01f)
    {
        auto& k = tr.keys;

        if (k.size() < 3)
            return;

        std::vector<Key> out;
        out.reserve(k.size());
        out.push_back(k.front());

        for (size_t i = 1; i + 1 < k.size(); ++i)
        {
            const Key& prev = out.back();
            const Key& cur  = k[i];
            const Key& next = k[i + 1];

            if (cur.recorded && next.time - prev.time > 1.0e-9)
            {
                const float f = (float) ((cur.time - prev.time) / (next.time - prev.time));

                if (nearlyEqual(cur.pos, lerp(prev.pos, next.pos, f), eps))
                    continue;
            }

            out.push_back(cur);
        }

        out.push_back(k.back());
        k.swap(out);
    }

    Key* findKey(const juce::Uuid& objId, int keyId)
    {
        auto it = tracks.find(objId);

        if (it == tracks.end())
            return nullptr;

        for (auto& k : it->second.keys)
            if (k.id == keyId)
                return &k;

        return nullptr;
    }

    AudioObject::Ptr objectById(const juce::Uuid& id) const
    {
        for (auto& o : objects)
            if (o != nullptr && o->id == id)
                return o;

        return nullptr;
    }

    void validateSelection()
    {
        if (selKeyId >= 0 && findKey(selTrack, selKeyId) == nullptr)
            selKeyId = -1;
    }

    void updateAnimationEnd()
    {
        if (timeline == nullptr)
            return;

        double end = 0.0;

        for (auto& entry : tracks)
            if (! entry.second.keys.empty())
                end = juce::jmax(end, entry.second.keys.back().time);

        timeline->setAnimationEnd(end);
    }

    // call after any change to the keys
    void markChanged()
    {
        updateAnimationEnd();
        evalDirty = true;
        repaint();

        if (onKeysChanged)
            onKeysChanged();
    }

    //==========================================================================
    // Timeline sync
    //==========================================================================
    void changeListenerCallback(juce::ChangeBroadcaster* source) override
    {
        if (timeline == nullptr)
            return;

        if (source == &timeline->viewBroadcaster)
            pullView();
        else if (source == &timeline->listBroadcaster)
            refreshObjects();

        repaint();
    }

    void refreshObjects()
    {
        objects = timeline != nullptr ? timeline->getAudioObjects() : std::vector<AudioObject::Ptr> {};

        // forget animation of objects that no longer exist
        for (auto it = tracks.begin(); it != tracks.end();)
        {
            if (objectById(it->first) == nullptr)
                it = tracks.erase(it);
            else
                ++it;
        }

        validateSelection();
        updateAnimationEnd();
        updateScrollBars();
    }

    void pullView()
    {
        if (timeline == nullptr)
            return;

        viewStart = timeline->getViewStart();
        pps       = timeline->getPixelsPerSecond();
        repaint();
    }

    void pushView()
    {
        if (timeline == nullptr)
            return;

        timeline->setView(viewStart, pps);
        pullView();                                  // the timeline may have clamped it
    }

    void zoomAround(double newPps, float anchorX)
    {
        const double t = xToTime(anchorX);
        pps = juce::jlimit(10.0, 400.0, newPps);
        viewStart = juce::jmax(0.0, t - ((double) anchorX - headerW()) / pps);
        pushView();
        updateScrollBars();
    }

    //==========================================================================
    // Playback / recording tick (message thread, 60 Hz)
    //==========================================================================
    void timerCallback() override
    {
        if (timeline == nullptr)
            return;

        const bool nowPlaying = timeline->isPlaying();

        if (nowPlaying != lastPlaying)
        {
            lastPlaying = nowPlaying;
            playButton.setIcon(nowPlaying ? SeqButton::Icon::pause : SeqButton::Icon::play);

            if (recording)
                repaint(statusRect());
        }

        const double t = timeline->getPlayheadSeconds();
        const auto sel = timeline->getSelectedObject();

        if (recording)
            recordTick(sel, t, nowPlaying);

        const bool timeMoved = (t != lastEvalTime);

        if (timeMoved || evalDirty)
        {
            evaluateAt(t, recording ? sel : AudioObject::Ptr {});
            lastEvalTime = t;
            evalDirty = false;
        }

        if (timeMoved)
            repaintPlayhead(t);
    }

    void recordTick(const AudioObject::Ptr& sel, double t, bool nowPlaying)
    {
        if (sel == nullptr)
        {
            recObj = nullptr;
            return;
        }

        const Vec3f cur = positionOf(*sel);

        if (sel != recObj)                           // record just started / selection changed
        {
            recObj = sel;
            lastRec = cur;
            lastChangeTime = t;

            if (nowPlaying)                          // anchor: where the object is when recording begins
            {
                addOrReplaceKey(sel->id, t, cur, true);
                markChanged();
            }

            return;
        }

        if (t < lastChangeTime)
            lastChangeTime = t;

        if (! nearlyEqual(cur, lastRec))
        {
            // object sat still for a while, then moved: pin it in place first so it
            // doesn't drift slowly from the previous key towards the new one
            if (nowPlaying && t - lastChangeTime > holdGap && t > holdOffset)
                addOrReplaceKey(sel->id, t - holdOffset, lastRec, true);

            const int id = addOrReplaceKey(sel->id, t, cur, true);
            juce::ignoreUnused(id);

            lastRec = cur;
            lastChangeTime = t;
            markChanged();
        }
    }

    void evaluateAt(double t, const AudioObject::Ptr& skip)
    {
        for (auto& o : objects)
        {
            if (o == nullptr || o == skip)
                continue;

            auto it = tracks.find(o->id);

            if (it == tracks.end())
                continue;

            Vec3f p;

            if (! sampleTrack(it->second, t, p))
                continue;

            if (! nearlyEqual(positionOf(*o), p, 1.0e-5f))
                timeline->setObjectPosition(o, p);    // same call the Inspector uses
        }
    }

    //==========================================================================
    // Geometry (mirrors Timeline so x positions line up)
    //==========================================================================
    juce::Rectangle<int> lanesArea() const
    {
        return { headerW(), toolbarH + rulerH,
                 juce::jmax(0, getWidth() - headerW() - vbarW),
                 juce::jmax(0, getHeight() - toolbarH - rulerH) };
    }

    juce::Rectangle<int> rulerArea() const   { return { headerW(), toolbarH, lanesArea().getWidth(), rulerH }; }
    juce::Rectangle<int> headersArea() const { return { 0, toolbarH + rulerH, headerW(), lanesArea().getHeight() }; }
    juce::Rectangle<int> statusRect() const  { return { statusX, 0, 140, toolbarH }; }

    double visibleSeconds() const            { return (double) lanesArea().getWidth() / pps; }
    double xToTime(float x) const            { return ((double) x - headerW()) / pps + viewStart; }
    float  timeToX(double t) const           { return (float) (headerW() + (t - viewStart) * pps); }
    int    laneTop(int i) const              { return lanesArea().getY() + i * laneH - scrollY; }
    int    contentHeight() const             { return (int) objects.size() * laneH; }

    int laneAtY(int y) const
    {
        return (int) std::floor((double) (y - lanesArea().getY() + scrollY) / (double) laneH);
    }

    juce::Rectangle<int> clearRect(int i) const { return { headerW() - 26, laneTop(i) + 8, 16, 16 }; }

    void updateScrollBars()
    {
        scrollY = juce::jlimit(0, juce::jmax(0, contentHeight() - lanesArea().getHeight()), scrollY);

        vBar.setRangeLimits(0.0, (double) juce::jmax(contentHeight(), lanesArea().getHeight()));
        vBar.setCurrentRange(juce::Range<double>((double) scrollY, (double) (scrollY + lanesArea().getHeight())),
                             juce::dontSendNotification);
    }

    void scrollBarMoved(juce::ScrollBar*, double newStart) override
    {
        scrollY = (int) newStart;
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
    // Hit testing
    //==========================================================================
    bool keyAt(juce::Point<int> p, int lane, int& outKeyId) const
    {
        if (! juce::isPositiveAndBelow(lane, (int) objects.size()))
            return false;

        auto it = tracks.find(objects[(size_t) lane]->id);

        if (it == tracks.end())
            return false;

        const float cy = (float) laneTop(lane) + (float) laneH * 0.5f;

        if (std::abs((float) p.y - cy) > 11.0f)
            return false;

        double bestD = 8.0;
        bool found = false;

        for (auto& k : it->second.keys)
        {
            const double d = std::abs((double) timeToX(k.time) - (double) p.x);

            if (d <= bestD)
            {
                bestD = d;
                outKeyId = k.id;
                found = true;
            }
        }

        return found;
    }

    void scrubTo(int x)
    {
        if (timeline != nullptr)
            timeline->setPlayheadSeconds(xToTime((float) x));
    }

    void handleHeaderMouse(const juce::MouseEvent& e)
    {
        const auto p = e.getPosition();
        const int lane = laneAtY(p.y);

        if (! juce::isPositiveAndBelow(lane, (int) objects.size()))
            return;

        auto obj = objects[(size_t) lane];

        if (! e.mods.isPopupMenu() && clearRect(lane).contains(p))
        {
            tracks.erase(obj->id);
            validateSelection();
            markChanged();
            return;
        }

        timeline->selectObject(obj);
        repaint();
    }

    //==========================================================================
    // Context menus
    //==========================================================================
    void showKeyMenu(const juce::Uuid& objId, int keyId)
    {
        juce::PopupMenu m;
        m.addItem(1, "Set Key To Current Position");
        m.addItem(2, "Delete Key");

        juce::Component::SafePointer<AnimationSequencer> safe(this);

        m.showMenuAsync(juce::PopupMenu::Options(), [safe, objId, keyId](int result)
        {
            if (safe == nullptr || result == 0)
                return;

            if (result == 1)
            {
                auto obj = safe->objectById(objId);
                auto* k = safe->findKey(objId, keyId);

                if (obj != nullptr && k != nullptr)
                {
                    k->pos = positionOf(*obj);
                    safe->markChanged();
                }
            }
            else if (result == 2)
            {
                safe->selTrack = objId;
                safe->selKeyId = keyId;
                safe->deleteSelectedKey();
            }
        });
    }

    void showLaneMenu(const AudioObject::Ptr& obj, double time)
    {
        juce::PopupMenu m;
        m.addItem(1, "Add Key Here");
        m.addItem(2, "Add Key At Playhead");
        m.addSeparator();
        m.addItem(3, "Clear Animation");

        juce::Component::SafePointer<AnimationSequencer> safe(this);

        m.showMenuAsync(juce::PopupMenu::Options(), [safe, obj, time](int result)
        {
            if (safe == nullptr || result == 0 || obj == nullptr)
                return;

            if (result == 1 || result == 2)
            {
                const double t = result == 1 ? time : safe->timeline->getPlayheadSeconds();
                safe->selTrack = obj->id;
                safe->selKeyId = safe->addOrReplaceKey(obj->id, t, positionOf(*obj), false);
                safe->markChanged();
            }
            else if (result == 3)
            {
                safe->tracks.erase(obj->id);
                safe->validateSelection();
                safe->markChanged();
            }
        });
    }

    //==========================================================================
    // Painting
    //==========================================================================
    static void drawDiamond(juce::Graphics& g, float cx, float cy, float r,
                            juce::Colour fill, juce::Colour outline)
    {
        juce::Path d;
        d.addQuadrilateral(cx, cy - r, cx + r, cy, cx, cy + r, cx - r, cy);
        g.setColour(fill);
        g.fillPath(d);
        g.setColour(outline);
        g.strokePath(d, juce::PathStrokeType(1.2f));
    }

    void drawCurves(juce::Graphics& g, const ObjectTrack& tr, juce::Rectangle<float> lane,
                    juce::Rectangle<float> visible) const
    {
        const auto& k = tr.keys;

        if (k.size() < 2)
            return;

        float lo = 1.0e30f, hi = -1.0e30f;

        for (auto& key : k)
            for (float v : { key.pos.x, key.pos.y, key.pos.z })
            {
                lo = juce::jmin(lo, v);
                hi = juce::jmax(hi, v);
            }

        if (hi - lo < 1.0e-4f)
            hi = lo + 1.0f;

        const float top = lane.getY() + 9.0f;
        const float bot = lane.getBottom() - 9.0f;

        auto yOf = [&](float v) { return bot - (v - lo) / (hi - lo) * (bot - top); };

        static const juce::Colour cols[3] { juce::Colour(0xffe06c75), juce::Colour(0xff98c379), juce::Colour(0xff61afef) };

        for (int c = 0; c < 3; ++c)
        {
            auto val = [c](const Key& key) { return c == 0 ? key.pos.x : (c == 1 ? key.pos.y : key.pos.z); };

            juce::Path p;
            p.startNewSubPath(juce::jmin(visible.getX(), timeToX(k.front().time)), yOf(val(k.front())));

            for (auto& key : k)
                p.lineTo(timeToX(key.time), yOf(val(key)));

            p.lineTo(juce::jmax(visible.getRight(), timeToX(k.back().time)), yOf(val(k.back())));

            g.setColour(cols[c].withAlpha(0.5f));
            g.strokePath(p, juce::PathStrokeType(1.1f));
        }
    }

    void paintLanes(juce::Graphics& g)
    {
        const auto area = lanesArea();
        juce::Graphics::ScopedSaveState ss(g);
        g.reduceClipRegion(area);

        g.setColour(TimelineTheme::panel.darker(0.25f));
        g.fillRect(area);

        const auto sel = timeline != nullptr ? timeline->getSelectedObject() : AudioObject::Ptr {};

        // rows
        for (int i = 0; i < (int) objects.size(); ++i)
        {
            const int top = laneTop(i);

            if (top + laneH < area.getY() || top > area.getBottom())
                continue;

            g.setColour(i % 2 ? TimelineTheme::laneAlt : TimelineTheme::panel);
            g.fillRect(area.getX(), top, area.getWidth(), laneH);

            if (objects[(size_t) i] == sel)
            {
                g.setColour((recording ? juce::Colour(0xffff4d4d) : TimelineTheme::accent).withAlpha(0.07f));
                g.fillRect(area.getX(), top, area.getWidth(), laneH);
            }

            g.setColour(TimelineTheme::border);
            g.fillRect(area.getX(), top + laneH - 1, area.getWidth(), 1);
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

        if (objects.empty())
        {
            g.setColour(TimelineTheme::textDim);
            g.setFont(juce::Font(juce::FontOptions(12.0f)));
            g.drawFittedText("No audio objects yet.\nDrop audio onto the Timeline, then animate it here.",
                             area.reduced(16), juce::Justification::centred, 3);
            return;
        }

        // lanes content
        for (int i = 0; i < (int) objects.size(); ++i)
        {
            const int top = laneTop(i);

            if (top + laneH < area.getY() || top > area.getBottom())
                continue;

            auto it = tracks.find(objects[(size_t) i]->id);

            if (it == tracks.end() || it->second.keys.empty())
                continue;

            const auto& tr = it->second;
            const auto objCol = timeline->getObjectColour(objects[(size_t) i]);
            const juce::Rectangle<float> lane((float) area.getX(), (float) top, (float) area.getWidth(), (float) laneH);

            // animated range
            const float x1 = timeToX(tr.keys.front().time);
            const float x2 = timeToX(tr.keys.back().time);

            if (x2 > x1)
            {
                g.setColour(objCol.withAlpha(0.10f));
                g.fillRoundedRectangle(x1, (float) top + 5.0f, x2 - x1, (float) laneH - 11.0f, 4.0f);
                g.setColour(objCol.withAlpha(0.35f));
                g.drawRoundedRectangle(x1 + 0.5f, (float) top + 5.5f, x2 - x1 - 1.0f, (float) laneH - 12.0f, 4.0f, 1.0f);
            }

            drawCurves(g, tr, lane, lane);

            // keys
            const float cy = (float) top + (float) laneH * 0.5f;

            for (auto& k : tr.keys)
            {
                const float x = timeToX(k.time);

                if (x < (float) area.getX() - 8.0f || x > (float) area.getRight() + 8.0f)
                    continue;

                const bool isSel = (selKeyId == k.id && selTrack == objects[(size_t) i]->id);

                drawDiamond(g, x, cy, isSel ? 7.5f : 6.0f,
                            isSel ? juce::Colours::white : (k.recorded ? objCol.withAlpha(0.85f) : objCol),
                            isSel ? TimelineTheme::accent : TimelineTheme::bar);
            }
        }
    }

    void paintHeaders(juce::Graphics& g)
    {
        // corner cell above the lane list
        g.setColour(TimelineTheme::bar);
        g.fillRect(0, toolbarH, headerW(), rulerH);
        g.setColour(TimelineTheme::textDim);
        g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
        g.drawText("OBJECTS", juce::Rectangle<int>(12, toolbarH, headerW() - 12, rulerH),
                   juce::Justification::centredLeft, true);

        const auto area = headersArea();
        juce::Graphics::ScopedSaveState ss(g);
        g.reduceClipRegion(area);

        g.setColour(TimelineTheme::bar);
        g.fillRect(area);

        const auto sel = timeline != nullptr ? timeline->getSelectedObject() : AudioObject::Ptr {};

        for (int i = 0; i < (int) objects.size(); ++i)
        {
            const int top = laneTop(i);

            if (top + laneH < area.getY() || top > area.getBottom())
                continue;

            auto& o = objects[(size_t) i];
            const bool isSel = (o == sel);

            if (isSel)
            {
                g.setColour((recording ? juce::Colour(0xffff4d4d) : TimelineTheme::accent).withAlpha(0.10f));
                g.fillRect(0, top, headerW() - 1, laneH - 1);
            }

            g.setColour(timeline->getObjectColour(o));
            g.fillRect(0, top, 4, laneH - 1);

            g.setColour(TimelineTheme::text);
            g.setFont(juce::Font(juce::FontOptions(12.5f, juce::Font::bold)));
            g.drawText(o->name, juce::Rectangle<int>(14, top + 8, headerW() - 44, 18),
                       juce::Justification::centredLeft, true);

            const int n = getNumKeys(o);
            g.setColour(TimelineTheme::textDim);
            g.setFont(juce::Font(juce::FontOptions(11.0f)));
            g.drawText(n == 0 ? juce::String("no keys") : juce::String(n) + (n == 1 ? " key" : " keys"),
                       juce::Rectangle<int>(14, top + 28, headerW() - 30, 16),
                       juce::Justification::centredLeft, true);

            if (recording && isSel)
            {
                g.setColour(juce::Colour(0xffff4d4d));
                g.fillEllipse(16.0f, (float) top + 47.0f, 8.0f, 8.0f);
                g.setFont(juce::Font(juce::FontOptions(10.5f, juce::Font::bold)));
                g.drawText("REC", juce::Rectangle<int>(28, top + 44, 60, 14), juce::Justification::centredLeft);
            }

            // clear "x"
            if (n > 0)
            {
                const auto rr = clearRect(i).toFloat().reduced(4.5f);
                g.setColour(TimelineTheme::textDim);
                g.drawLine(rr.getX(), rr.getY(), rr.getRight(), rr.getBottom(), 1.2f);
                g.drawLine(rr.getX(), rr.getBottom(), rr.getRight(), rr.getY(), 1.2f);
            }

            g.setColour(TimelineTheme::border);
            g.fillRect(0, top + laneH - 1, headerW(), 1);
        }

        g.setColour(TimelineTheme::border);
        g.fillRect(headerW() - 1, area.getY(), 1, area.getHeight());
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
        g.drawText("ANIMATION", juce::Rectangle<int>(12, 0, 80, toolbarH), juce::Justification::centredLeft, true);

        // time readout (same playhead as the Timeline)
        g.setColour(TimelineTheme::panel);
        g.fillRoundedRectangle(timeRect.toFloat(), 4.0f);
        g.setColour(TimelineTheme::border);
        g.drawRoundedRectangle(timeRect.toFloat().reduced(0.5f), 4.0f, 1.0f);

        const double t = timeline != nullptr ? timeline->getPlayheadSeconds() : 0.0;

        g.setColour(TimelineTheme::accent);
        g.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 14.0f, juce::Font::bold)));
        g.drawText(formatTime(t, true), timeRect, juce::Justification::centred);

        // record status
        if (recording)
        {
            const bool live = timeline != nullptr && timeline->isPlaying();
            g.setColour(juce::Colour(0xffff4d4d));
            g.fillEllipse((float) statusX, (float) toolbarH * 0.5f - 4.0f, 8.0f, 8.0f);
            g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
            g.drawText(live ? "RECORDING" : "ARMED - press play",
                       juce::Rectangle<int>(statusX + 14, 0, 130, toolbarH), juce::Justification::centredLeft);
        }
    }

    void paintPlayhead(juce::Graphics& g)
    {
        if (timeline == nullptr)
            return;

        const auto lanes = lanesArea();
        const float x = timeToX(timeline->getPlayheadSeconds());

        if (x < (float) lanes.getX() - 1.0f || x > (float) lanes.getRight())
            return;

        juce::Graphics::ScopedSaveState ss(g);
        g.reduceClipRegion(juce::Rectangle<int>(lanes.getX(), toolbarH, lanes.getWidth(), lanes.getBottom() - toolbarH));

        g.setColour(recording ? juce::Colour(0xffff4d4d) : TimelineTheme::accent);
        g.fillRect(x - 0.75f, (float) (toolbarH + rulerH - 1), 1.5f, (float) (lanes.getBottom() - toolbarH - rulerH + 1));

        const float rb = (float) (toolbarH + rulerH);
        juce::Path tri;
        tri.addTriangle(x - 6.0f, rb - 13.0f, x + 6.0f, rb - 13.0f, x, rb - 1.0f);
        g.fillPath(tri);
    }

    void repaintPlayhead(double t)
    {
        repaint(timeRect);

        const int x = (int) timeToX(t);
        const int h = lanesArea().getBottom() - toolbarH;

        repaint(lastPlayheadX - 8, toolbarH, 16, h);
        repaint(x - 8, toolbarH, 16, h);
        lastPlayheadX = x;
    }

    //==========================================================================
    // Members
    //==========================================================================
    Timeline* timeline = nullptr;
    std::vector<AudioObject::Ptr> objects;               // lane order = Timeline order

    std::map<juce::Uuid, ObjectTrack> tracks;            // keyframes per audio object
    int nextKeyId = 1;

    // view (mirrors the Timeline's)
    double pps       = 60.0;
    double viewStart = 0.0;
    int    scrollY   = 0;

    // key selection / dragging
    DragMode   dragMode = DragMode::none;
    juce::Uuid selTrack;
    int        selKeyId = -1;
    double     dragGrabOffset = 0.0;

    // recording
    bool             recording = false;
    AudioObject::Ptr recObj;
    Vec3f            lastRec;
    double           lastChangeTime = 0.0;

    // evaluation
    double lastEvalTime = -1.0;
    bool   evalDirty    = true;

    bool lastPlaying   = false;
    int  lastPlayheadX = 0;
    int  statusX       = 0;

    SeqButton toStartButton { SeqButton::Icon::toStart, "Go to Start" };
    SeqButton playButton    { SeqButton::Icon::play,    "Play / Pause (Space)" };
    SeqButton stopButton    { SeqButton::Icon::stop,    "Stop" };
    SeqButton recordButton  { SeqButton::Icon::record,  "Record motion of the selected object (R)" };
    SeqButton addKeyButton  { SeqButton::Icon::addKey,  "Add key at playhead (K)" };
    SeqButton delKeyButton  { SeqButton::Icon::delKey,  "Delete selected key (Del)" };

    juce::ScrollBar vBar { true };
    juce::Rectangle<int> timeRect;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnimationSequencer)
};
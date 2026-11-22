#pragma once
#include <JuceHeader.h>
#include "Timeline.h"

//==============================================================================
// Lists every audio object (clip) on the timeline.
//   - click            : select (also selects it in the timeline + inspector)
//   - double-click/F2  : rename (Enter = confirm, Esc = cancel)
//   - right-click      : Rename / Mute / Delete
//   - M / X icons      : mute toggle / delete
//   - Delete key       : delete selected
//
// NOTE: the Timeline must outlive this panel (declare the Timeline BEFORE it in the
//       parent so it is destroyed AFTER it).
//==============================================================================
class Hierarchy : public juce::Component,
                  private juce::ListBoxModel,
                  private juce::ChangeListener,
                  private juce::Label::Listener
{
public:
    Hierarchy()
    {
        list.setModel(this);
        list.setRowHeight(rowH);
        list.setOutlineThickness(0);
        list.setColour(juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        addAndMakeVisible(list);

        renameLabel.setFont(juce::Font(juce::FontOptions(12.5f)));
        renameLabel.setColour(juce::Label::textWhenEditingColourId, TimelineTheme::text);
        renameLabel.setColour(juce::Label::backgroundWhenEditingColourId, TimelineTheme::bar);
        renameLabel.setColour(juce::Label::outlineWhenEditingColourId, TimelineTheme::accent);
        renameLabel.addListener(this);
        addChildComponent(renameLabel);

        refresh();
    }

    ~Hierarchy() override
    {
        setTimeline(nullptr);
        list.setModel(nullptr);
    }

    void setTimeline(Timeline* newTimeline)
    {
        if (timeline == newTimeline)
            return;

        if (timeline != nullptr)
        {
            timeline->listBroadcaster.removeChangeListener(this);
            timeline->selectionBroadcaster.removeChangeListener(this);
        }

        timeline = newTimeline;

        if (timeline != nullptr)
        {
            timeline->listBroadcaster.addChangeListener(this);
            timeline->selectionBroadcaster.addChangeListener(this);
        }
        else
        {
            renameLabel.setVisible(false);
            renaming = nullptr;
        }

        refresh();
    }

    // the same list, in case you need it elsewhere
    const std::vector<AudioObject::Ptr>& getItems() const { return items; }

    //==========================================================================
    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff202630));

        auto header = getLocalBounds().removeFromTop(headerH);
        g.setColour(TimelineTheme::bar);
        g.fillRect(header);
        g.setColour(TimelineTheme::border);
        g.fillRect(0, headerH - 1, getWidth(), 1);

        g.setColour(TimelineTheme::textDim);
        g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
        g.drawText("HIERARCHY", header.withTrimmedLeft(12), juce::Justification::centredLeft, true);

        g.setFont(juce::Font(juce::FontOptions(11.0f)));
        g.drawText(juce::String((int) items.size()) + (items.size() == 1 ? " object" : " objects"),
                   header.withTrimmedRight(12), juce::Justification::centredRight, true);

        if (items.empty())
        {
            g.setColour(TimelineTheme::textDim);
            g.setFont(juce::Font(juce::FontOptions(12.0f)));
            g.drawFittedText("No audio objects yet.\nDrop audio onto the timeline.",
                             getLocalBounds().withTrimmedTop(headerH).reduced(16),
                             juce::Justification::centred, 3);
        }
    }

    void resized() override
    {
        list.setBounds(getLocalBounds().withTrimmedTop(headerH));
    }

    bool keyPressed(const juce::KeyPress& k) override
    {
        if (k == juce::KeyPress::F2Key)
        {
            beginRename(list.getSelectedRow());
            return true;
        }

        return false;
    }

private:
    static constexpr int headerH = 28;
    static constexpr int rowH    = 30;

    //==========================================================================
    // ListBoxModel
    //==========================================================================
    int getNumRows() override { return (int) items.size(); }

    static juce::Rectangle<int> muteRect(int w)   { return { w - 58, (rowH - 20) / 2, 22, 20 }; }
    static juce::Rectangle<int> deleteRect(int w) { return { w - 30, (rowH - 20) / 2, 20, 20 }; }

    void paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected) override
    {
        if (timeline == nullptr || ! juce::isPositiveAndBelow(row, (int) items.size()))
            return;

        const auto& o = items[(size_t) row];

        if (selected)
        {
            g.setColour(TimelineTheme::accent.withAlpha(0.14f));
            g.fillRect(0, 0, w, h);
            g.setColour(TimelineTheme::accent);
            g.fillRect(0, 0, 2, h);
        }
        else if (row % 2)
        {
            g.setColour(juce::Colours::white.withAlpha(0.02f));
            g.fillRect(0, 0, w, h);
        }

        // colour chip (track colour)
        g.setColour(timeline->getObjectColour(o).withAlpha(o->muted ? 0.4f : 1.0f));
        g.fillRoundedRectangle(10.0f, (float) h * 0.5f - 6.0f, 12.0f, 12.0f, 3.0f);

        // name + track
        const int textRight = w - 66;
        const juce::String track = timeline->getObjectTrackName(o);
        const float nameAlpha = o->muted ? 0.45f : 1.0f;

        g.setColour(TimelineTheme::text.withAlpha(nameAlpha));
        g.setFont(juce::Font(juce::FontOptions(12.5f)));
        const int nameW = juce::jmax(10, (textRight - 30) - 60);
        g.drawText(o->name, juce::Rectangle<int>(30, 0, nameW, h), juce::Justification::centredLeft, true);

        g.setColour(TimelineTheme::textDim);
        g.setFont(juce::Font(juce::FontOptions(10.5f)));
        g.drawText(track, juce::Rectangle<int>(30 + nameW, 0, 60, h), juce::Justification::centredRight, true);

        // mute
        const auto mr = muteRect(w);
        if (o->muted)
        {
            g.setColour(TimelineTheme::muteCol);
            g.fillRoundedRectangle(mr.toFloat(), 4.0f);
            g.setColour(TimelineTheme::bar);
        }
        else
        {
            g.setColour(TimelineTheme::border);
            g.drawRoundedRectangle(mr.toFloat().reduced(0.5f), 4.0f, 1.0f);
            g.setColour(TimelineTheme::textDim);
        }
        g.setFont(juce::Font(juce::FontOptions(10.5f, juce::Font::bold)));
        g.drawText("M", mr, juce::Justification::centred);

        // delete "x"
        const auto dr = deleteRect(w).toFloat().reduced(6.0f);
        g.setColour(TimelineTheme::textDim);
        g.drawLine(dr.getX(), dr.getY(), dr.getRight(), dr.getBottom(), 1.3f);
        g.drawLine(dr.getX(), dr.getBottom(), dr.getRight(), dr.getY(), 1.3f);
    }

    void listBoxItemClicked(int row, const juce::MouseEvent& e) override
    {
        if (! juce::isPositiveAndBelow(row, (int) items.size()))
            return;

        const auto o = items[(size_t) row];
        const int  w = list.getVisibleRowWidth();

        if (e.mods.isPopupMenu())
        {
            showMenu(row);
            return;
        }

        if (timeline == nullptr)
            return;

        if (deleteRect(w).contains(e.getPosition()))      { timeline->removeObject(o); return; }
        if (muteRect(w).contains(e.getPosition()))        { timeline->setObjectMuted(o, ! o->muted); return; }
    }

    void listBoxItemDoubleClicked(int row, const juce::MouseEvent& e) override
    {
        const int w = list.getVisibleRowWidth();

        if (muteRect(w).contains(e.getPosition()) || deleteRect(w).contains(e.getPosition()))
            return;

        beginRename(row);
    }

    void selectedRowsChanged(int lastRow) override
    {
        if (syncing)
            return;

        if (timeline == nullptr)
            return;

        if (juce::isPositiveAndBelow(lastRow, (int) items.size()))
            timeline->selectObject(items[(size_t) lastRow]);
        else
            timeline->selectObject(nullptr);
    }

    void deleteKeyPressed(int lastRow) override
    {
        if (timeline != nullptr && juce::isPositiveAndBelow(lastRow, (int) items.size()))
            timeline->removeObject(items[(size_t) lastRow]);
    }

    //==========================================================================
    // Rename
    //==========================================================================
    void beginRename(int row)
    {
        if (! juce::isPositiveAndBelow(row, (int) items.size()))
            return;

        renaming = items[(size_t) row];

        auto r = list.getRowPosition(row, true) + list.getPosition();
        renameLabel.setBounds(r.withTrimmedLeft(28).withTrimmedRight(66).reduced(0, 3));
        renameLabel.setText(renaming->name, juce::dontSendNotification);
        renameLabel.setVisible(true);
        renameLabel.showEditor();
    }

    void labelTextChanged(juce::Label* l) override
    {
        if (l == &renameLabel && timeline != nullptr && renaming != nullptr)
            timeline->renameObject(renaming, renameLabel.getText());
    }

    void editorHidden(juce::Label* l, juce::TextEditor&) override
    {
        if (l == &renameLabel)
        {
            renameLabel.setVisible(false);
            renaming = nullptr;
        }
    }

    void showMenu(int row)
    {
        if (! juce::isPositiveAndBelow(row, (int) items.size()))
            return;

        const auto o = items[(size_t) row];

        juce::PopupMenu m;
        m.addItem(1, "Rename");
        m.addItem(2, o->muted ? "Unmute" : "Mute");
        m.addSeparator();
        m.addItem(3, "Delete");

        juce::Component::SafePointer<Hierarchy> safe(this);

        m.showMenuAsync(juce::PopupMenu::Options(), [safe, row, o](int r)
        {
            if (safe == nullptr)
                return;

            if      (r == 1) safe->beginRename(row);
            else if (safe->timeline != nullptr && r == 2)
                safe->timeline->setObjectMuted(o, ! o->muted);
            else if (safe->timeline != nullptr && r == 3)
                safe->timeline->removeObject(o);
        });
    }

    //==========================================================================
    // Sync with the timeline
    //==========================================================================
    void changeListenerCallback(juce::ChangeBroadcaster*) override { refresh(); }

    void refresh()
    {
        items = timeline != nullptr ? timeline->getAudioObjects()
                                    : std::vector<AudioObject::Ptr> {};

        syncing = true;
        list.updateContent();
        updateSelectedRow();
        syncing = false;

        list.repaint();
        repaint();
    }

    void updateSelectedRow()
    {
        const auto sel = timeline != nullptr ? timeline->getSelectedObject()
                                             : AudioObject::Ptr {};
        int idx = -1;

        for (int i = 0; i < (int) items.size(); ++i)
            if (items[(size_t) i] == sel) { idx = i; break; }

        if (idx >= 0)
        {
            list.selectRow(idx, true, true);
            list.scrollToEnsureRowIsOnscreen(idx);
        }
        else
        {
            list.deselectAllRows();
        }
    }

    //==========================================================================
    Timeline* timeline = nullptr;
    std::vector<AudioObject::Ptr> items;
    AudioObject::Ptr renaming;
    bool syncing = false;

    juce::ListBox list { "audio objects" };
    juce::Label   renameLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Hierarchy)
};
#pragma once
#include <JuceHeader.h>
#include "Timeline.h"
#include "../../knob.h"
#include <cmath>

//==============================================================================
// Unity-style Inspector for the currently selected AudioObject in Timeline.
// Syncs via Timeline::selectionBroadcaster and Timeline::listBroadcaster.
//
// NOTE: Declare Timeline BEFORE Inspector in the parent component so Timeline
//       outlives Inspector during destruction.
//==============================================================================
class Inspector : public juce::Component,
                  private juce::ChangeListener,
                  private juce::TextEditor::Listener
{
public:
    Inspector()
    {
        viewport.setViewedComponent(&content, false);
        viewport.setScrollBarsShown(true, false);
        addAndMakeVisible(viewport);

        buildHeaderSection();
        buildTransformSection();
        buildAudioSection();
        buildClipInfoSection();

        setObject(nullptr);
    }

    ~Inspector() override
    {
        setTimeline(nullptr);
    }

    void setTimeline(Timeline* newTimeline)
    {
        if (timeline == newTimeline)
            return;

        if (timeline != nullptr)
        {
            timeline->selectionBroadcaster.removeChangeListener(this);
            timeline->listBroadcaster.removeChangeListener(this);
            timeline->transformBroadcaster.removeChangeListener(this);
        }

        timeline = newTimeline;

        if (timeline != nullptr)
        {
            timeline->selectionBroadcaster.addChangeListener(this);
            timeline->listBroadcaster.addChangeListener(this);
            timeline->transformBroadcaster.addChangeListener(this);
            setObject(timeline->getSelectedObject());
        }
        else
        {
            setObject(nullptr);
        }
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff202630));

        auto header = getLocalBounds().removeFromTop(topBarH);
        g.setColour(TimelineTheme::bar);
        g.fillRect(header);
        g.setColour(TimelineTheme::border);
        g.fillRect(0, topBarH - 1, getWidth(), 1);

        g.setColour(TimelineTheme::textDim);
        g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
        g.drawText("INSPECTOR", header.withTrimmedLeft(12), juce::Justification::centredLeft, true);

        if (currentObj == nullptr)
        {
            g.setColour(TimelineTheme::textDim);
            g.setFont(juce::Font(juce::FontOptions(12.0f)));
            g.drawFittedText("No audio object selected.\nClick an object in the Hierarchy or Timeline.",
                             getLocalBounds().withTrimmedTop(topBarH).reduced(16),
                             juce::Justification::centred, 3);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds();
        area.removeFromTop(topBarH);
        viewport.setBounds(area);
        layoutContent(area.getWidth());
    }

private:
    static constexpr int topBarH    = 28;
    static constexpr int sectionH   = 24;
    static constexpr int rowH       = 24;
    static constexpr int labelW     = 84;
    static constexpr int padX       = 10;

    //==========================================================================
    // Section & Row Container
    //==========================================================================
    struct SectionBlock : public juce::Component
    {
        juce::String title;
        bool collapsed = false;
        std::function<void()> onToggle;
        std::function<void()> onReset;
        bool hasReset = false;

        void paint(juce::Graphics& g) override
        {
            auto hdr = getLocalBounds().removeFromTop(sectionH);
            g.setColour(TimelineTheme::bar.brighter(0.06f));
            g.fillRect(hdr);
            g.setColour(TimelineTheme::border);
            g.fillRect(0, 0, getWidth(), 1);
            g.fillRect(0, sectionH - 1, getWidth(), 1);

            // Foldout chevron
            const float cx = 14.0f;
            const float cy = (float) sectionH * 0.5f;
            juce::Path tri;
            if (collapsed)
            {
                tri.addTriangle(cx - 3.0f, cy - 4.0f, cx - 3.0f, cy + 4.0f, cx + 3.0f, cy);
            }
            else
            {
                tri.addTriangle(cx - 4.0f, cy - 2.5f, cx + 4.0f, cy - 2.5f, cx, cy + 3.5f);
            }
            g.setColour(TimelineTheme::textDim);
            g.fillPath(tri);

            g.setColour(TimelineTheme::text);
            g.setFont(juce::Font(juce::FontOptions(11.5f, juce::Font::bold)));
            g.drawText(title, hdr.withTrimmedLeft(24), juce::Justification::centredLeft, true);

            if (hasReset && ! collapsed)
            {
                auto r = getResetBounds();
                g.setColour(TimelineTheme::textDim);
                g.setFont(juce::Font(juce::FontOptions(10.0f)));
                g.drawText("Reset", r, juce::Justification::centredRight);
            }
        }

        juce::Rectangle<int> getResetBounds() const
        {
            return { getWidth() - 48, 2, 40, sectionH - 4 };
        }

        void mouseDown(const juce::MouseEvent& e) override
        {
            if (e.y > sectionH)
                return;

            if (hasReset && ! collapsed && getResetBounds().contains(e.getPosition()))
            {
                if (onReset) onReset();
                return;
            }

            collapsed = ! collapsed;
            if (onToggle) onToggle();
        }
    };

    struct Vec3Row : public juce::Component
    {
        juce::Label rowLabel;
        juce::TextEditor xBox, yBox, zBox;
        juce::Label xLbl, yLbl, zLbl;

        Vec3Row(const juce::String& name, juce::Colour axisCol, juce::TextEditor::Listener* listener)
        {
            rowLabel.setText(name, juce::dontSendNotification);
            rowLabel.setFont(juce::Font(juce::FontOptions(11.5f)));
            rowLabel.setColour(juce::Label::textColourId, TimelineTheme::textDim);
            addAndMakeVisible(rowLabel);

            auto setupAxis = [&](juce::Label& lbl, juce::TextEditor& box, const juce::String& axisChar)
            {
                lbl.setText(axisChar, juce::dontSendNotification);
                lbl.setFont(juce::Font(juce::FontOptions(10.5f, juce::Font::bold)));
                lbl.setColour(juce::Label::textColourId, axisCol);
                lbl.setJustificationType(juce::Justification::centred);
                addAndMakeVisible(lbl);

                box.setInputRestrictions(10, "-0123456789.");
                box.setFont(juce::Font(juce::FontOptions(11.5f)));
                box.setJustification(juce::Justification::centredRight);
                box.setColour(juce::TextEditor::backgroundColourId, TimelineTheme::bar.darker(0.2f));
                box.setColour(juce::TextEditor::textColourId, TimelineTheme::text);
                box.setColour(juce::TextEditor::outlineColourId, TimelineTheme::border);
                box.setColour(juce::TextEditor::focusedOutlineColourId, TimelineTheme::accent);
                box.addListener(listener);
                addAndMakeVisible(box);
            };

            setupAxis(xLbl, xBox, "X");
            setupAxis(yLbl, yBox, "Y");
            setupAxis(zLbl, zBox, "Z");
        }

        void resized() override
        {
            auto r = getLocalBounds();
            rowLabel.setBounds(r.removeFromLeft(labelW));

            const int gap = 4;
            const int badgeW = 14;
            const int totalFieldsW = r.getWidth() - gap * 2;
            const int eachW = juce::jmax(32, totalFieldsW / 3);

            auto layoutField = [&](juce::Rectangle<int> cell, juce::Label& lbl, juce::TextEditor& box)
            {
                lbl.setBounds(cell.removeFromLeft(badgeW));
                box.setBounds(cell);
            };

            layoutField(r.removeFromLeft(eachW), xLbl, xBox);
            r.removeFromLeft(gap);
            layoutField(r.removeFromLeft(eachW), yLbl, yBox);
            r.removeFromLeft(gap);
            layoutField(r.removeFromLeft(eachW), zLbl, zBox);
        }
    };

    //==========================================================================
    // Build UI Sections
    //==========================================================================
    void buildHeaderSection()
    {
        muteToggle.setButtonText("Muted");
        muteToggle.setColour(juce::ToggleButton::textColourId, TimelineTheme::text);
        muteToggle.setColour(juce::ToggleButton::tickColourId, TimelineTheme::muteCol);
        muteToggle.onClick = [this]()
        {
            if (updatingUI || currentObj == nullptr || timeline == nullptr)
                return;

            timeline->setObjectMuted(currentObj, muteToggle.getToggleState());
        };
        content.addAndMakeVisible(muteToggle);

        nameEditor.setMultiLine(false);
        nameEditor.setReturnKeyStartsNewLine(false);
        nameEditor.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
        nameEditor.setColour(juce::TextEditor::backgroundColourId, TimelineTheme::bar.darker(0.2f));
        nameEditor.setColour(juce::TextEditor::textColourId, TimelineTheme::text);
        nameEditor.setColour(juce::TextEditor::outlineColourId, TimelineTheme::border);
        nameEditor.setColour(juce::TextEditor::focusedOutlineColourId, TimelineTheme::accent);
        nameEditor.addListener(this);
        content.addAndMakeVisible(nameEditor);

        idBadge.setFont(juce::Font(juce::FontOptions(10.0f)));
        idBadge.setColour(juce::Label::textColourId, TimelineTheme::textDim);
        content.addAndMakeVisible(idBadge);
    }

    void buildTransformSection()
    {
        transformSection.title = "Transform";
        transformSection.hasReset = true;
        transformSection.onToggle = [this]() { layoutContent(viewport.getWidth()); };
        transformSection.onReset  = [this]()
        {
            if (currentObj == nullptr) return;
            glm::vec3 zero;
            if (timeline == nullptr) return;
            timeline->setObjectPosition(currentObj, zero);
            timeline->setObjectRotation(currentObj, zero);
            syncFromObject();
        };
        content.addAndMakeVisible(transformSection);

        auto* textEditorListener = static_cast<juce::TextEditor::Listener*>(this);
        positionRow = std::make_unique<Vec3Row>("Position", juce::Colour(0xffe06c75), textEditorListener);
        rotationRow = std::make_unique<Vec3Row>("Rotation", juce::Colour(0xff98c379), textEditorListener);
        transformSection.addAndMakeVisible(*positionRow);
        transformSection.addAndMakeVisible(*rotationRow);
    }

    void buildAudioSection()
    {
        audioSection.title = "Audio & Spatial";
        audioSection.hasReset = true;
        audioSection.onToggle = [this]() { layoutContent(viewport.getWidth()); };
        audioSection.onReset  = [this]()
        {
            if (currentObj == nullptr) return;
            if (timeline == nullptr) return;
            timeline->setObjectVolume(currentObj, 1.0f);
            timeline->setObjectSpeed(currentObj, 1.0f);
            timeline->setObjectReverb(currentObj, 0.0f);
            syncFromObject();
        };
        content.addAndMakeVisible(audioSection);

        volumeKnob.setRange(0.0, 2.0, 0.01);
        volumeKnob.setValue(1.0);
        volumeKnob.setOnValueChange([this](double v)
        {
            if (updatingUI || currentObj == nullptr || timeline == nullptr)
                return;

            timeline->setObjectVolume(currentObj, static_cast<float>(v));
        });
        audioSection.addAndMakeVisible(volumeKnob);

        speedKnob.setRange(0.25, 4.0, 0.01);
        speedKnob.setValue(1.0);
        speedKnob.setOnValueChange([this](double v)
        {
            if (updatingUI || currentObj == nullptr || timeline == nullptr)
                return;

            timeline->setObjectSpeed(currentObj, static_cast<float>(v));
            timingLabel.setText("Start: " + juce::String(currentObj->startSec, 2) + "s  |  "
                                + "Clip: " + juce::String(currentObj->lengthSec, 2) + "s  |  "
                                + "Orig: " + juce::String(currentObj->sourceLengthSec, 2) + "s",
                                juce::dontSendNotification);
        });
        audioSection.addAndMakeVisible(speedKnob);

        reverbKnob.setRange(0.0, 1.0, 0.01);
        reverbKnob.setValue(0.0);
        reverbKnob.setOnValueChange([this](double v)
        {
            if (updatingUI || currentObj == nullptr || timeline == nullptr)
                return;

            timeline->setObjectReverb(currentObj, static_cast<float>(v));
        });
        audioSection.addAndMakeVisible(reverbKnob);
    }

    void buildClipInfoSection()
    {
        infoSection.title = "Clip Info";
        infoSection.hasReset = false;
        infoSection.onToggle = [this]() { layoutContent(viewport.getWidth()); };
        content.addAndMakeVisible(infoSection);

        auto prepInfoLabel = [this](juce::Label& lbl)
        {
            lbl.setFont(juce::Font(juce::FontOptions(11.0f)));
            lbl.setColour(juce::Label::textColourId, TimelineTheme::textDim);
            infoSection.addAndMakeVisible(lbl);
        };

        prepInfoLabel(fileLabel);
        prepInfoLabel(timingLabel);
        prepInfoLabel(formatLabel);
    }

    //==========================================================================
    // Selection & State Sync
    //==========================================================================
    void changeListenerCallback(juce::ChangeBroadcaster*) override
    {
        setObject(timeline != nullptr ? timeline->getSelectedObject() : AudioObject::Ptr {});
    }

    void setObject(AudioObject::Ptr obj)
    {
        currentObj = obj;
        const bool hasObj = (currentObj != nullptr);

        viewport.setVisible(hasObj);
        if (hasObj)
            syncFromObject();

        repaint();
    }

    void syncFromObject()
    {
        if (currentObj == nullptr)
            return;

        updatingUI = true;

        nameEditor.setText(currentObj->name, juce::dontSendNotification);
        idBadge.setText("ID: " + currentObj->id.toString().substring(0, 8), juce::dontSendNotification);
        muteToggle.setToggleState(currentObj->muted, juce::dontSendNotification);

        positionRow->xBox.setText(juce::String(currentObj->position.x, 2), juce::dontSendNotification);
        positionRow->yBox.setText(juce::String(currentObj->position.y, 2), juce::dontSendNotification);
        positionRow->zBox.setText(juce::String(currentObj->position.z, 2), juce::dontSendNotification);

        rotationRow->xBox.setText(juce::String(currentObj->rotation.x, 1), juce::dontSendNotification);
        rotationRow->yBox.setText(juce::String(currentObj->rotation.y, 1), juce::dontSendNotification);
        rotationRow->zBox.setText(juce::String(currentObj->rotation.z, 1), juce::dontSendNotification);

        volumeKnob.setValue(currentObj->volume);
        speedKnob.setValue(currentObj->speed);
        reverbKnob.setValue(currentObj->reverbMix);

        const juce::String fileName = currentObj->file.existsAsFile()
            ? currentObj->file.getFileName()
            : juce::String("(In-memory / Unsaved)");
        fileLabel.setText("Source: " + fileName, juce::dontSendNotification);

        timingLabel.setText("Start: " + juce::String(currentObj->startSec, 2) + "s  |  "
                            + "Clip: " + juce::String(currentObj->lengthSec, 2) + "s  |  "
                            + "Orig: " + juce::String(currentObj->sourceLengthSec, 2) + "s",
                            juce::dontSendNotification);

        formatLabel.setText("Sample Rate: " + juce::String((int) currentObj->sampleRate) + " Hz",
                            juce::dontSendNotification);

        updatingUI = false;
    }

    void textEditorReturnKeyPressed(juce::TextEditor& editor) override
    {
        commitVec3IfMatch(editor);
        editor.giveAwayKeyboardFocus();
    }

    void textEditorFocusLost(juce::TextEditor& editor) override
    {
        commitNameIfMatch(editor);
        commitVec3IfMatch(editor);
    }

    void commitNameIfMatch(juce::TextEditor& editor)
    {
        if (updatingUI || currentObj == nullptr || timeline == nullptr || &editor != &nameEditor)
            return;

        timeline->renameObject(currentObj, editor.getText());
        syncFromObject();
    }

    void commitVec3IfMatch(juce::TextEditor& editor)
    {
        if (updatingUI || currentObj == nullptr || timeline == nullptr)
            return;

        auto parseFloat = [](const juce::TextEditor& te, float fallback) -> float
        {
            const float v = te.getText().trim().getFloatValue();
            return std::isfinite(v) ? v : fallback;
        };

        if (&editor == &positionRow->xBox || &editor == &positionRow->yBox || &editor == &positionRow->zBox)
        {
            glm::vec3 pos {
                parseFloat(positionRow->xBox, currentObj->position.x),
                parseFloat(positionRow->yBox, currentObj->position.y),
                parseFloat(positionRow->zBox, currentObj->position.z)
            };
            timeline->setObjectPosition(currentObj, pos);
        }
        else if (&editor == &rotationRow->xBox || &editor == &rotationRow->yBox || &editor == &rotationRow->zBox)
        {
            glm::vec3 rot {
                parseFloat(rotationRow->xBox, currentObj->rotation.x),
                parseFloat(rotationRow->yBox, currentObj->rotation.y),
                parseFloat(rotationRow->zBox, currentObj->rotation.z)
            };
            timeline->setObjectRotation(currentObj, rot);
        }
    }

    //==========================================================================
    // Layout
    //==========================================================================
    void layoutContent(int width)
    {
        const int w = juce::jmax(180, width);
        int y = 8;

        // Header block
        const int headerBlockH = 54;
        auto hdrArea = juce::Rectangle<int>(padX, y, w - padX * 2, headerBlockH);
        muteToggle.setBounds(hdrArea.getRight() - 68, hdrArea.getY(), 68, 24);
        nameEditor.setBounds(hdrArea.getX(), hdrArea.getY(), hdrArea.getWidth() - 74, 24);
        idBadge.setBounds(hdrArea.getX(), hdrArea.getY() + 28, hdrArea.getWidth(), 18);
        y += headerBlockH + 6;

        // Transform section
        if (transformSection.collapsed)
        {
            transformSection.setBounds(0, y, w, sectionH);
            positionRow->setVisible(false);
            rotationRow->setVisible(false);
            y += sectionH + 6;
        }
        else
        {
            const int bodyH = rowH * 2 + 18;
            transformSection.setBounds(0, y, w, sectionH + bodyH);
            positionRow->setVisible(true);
            rotationRow->setVisible(true);

            int sy = sectionH + 6;
            positionRow->setBounds(padX, sy, w - padX * 2, rowH);
            sy += rowH + 6;
            rotationRow->setBounds(padX, sy, w - padX * 2, rowH);

            y += sectionH + bodyH + 6;
        }

        // Audio section
        if (audioSection.collapsed)
        {
            audioSection.setBounds(0, y, w, sectionH);
            volumeKnob.setVisible(false);
            speedKnob.setVisible(false);
            reverbKnob.setVisible(false);
            y += sectionH + 6;
        }
        else
        {
            const int knobAreaH = 104;
            const int bodyH = knobAreaH + 12;
            audioSection.setBounds(0, y, w, sectionH + bodyH);

            auto knobArea = juce::Rectangle<int>(padX, sectionH + 6,
                                                  w - padX * 2, knobAreaH).reduced(4, 0);
            const int knobWidth = knobArea.getWidth() / 3;

            for (auto* knob : { &volumeKnob, &speedKnob, &reverbKnob })
            {
                knob->setVisible(true);
                knob->setBounds(knobArea.removeFromLeft(knobWidth).reduced(2, 0));
            }

            y += sectionH + bodyH + 6;
        }

        // Clip Info section
        if (infoSection.collapsed)
        {
            infoSection.setBounds(0, y, w, sectionH);
            fileLabel.setVisible(false);
            timingLabel.setVisible(false);
            formatLabel.setVisible(false);
            y += sectionH + 8;
        }
        else
        {
            const int bodyH = 62;
            infoSection.setBounds(0, y, w, sectionH + bodyH);
            fileLabel.setVisible(true);
            timingLabel.setVisible(true);
            formatLabel.setVisible(true);

            int sy = sectionH + 6;
            fileLabel.setBounds(padX, sy, w - padX * 2, 18);
            sy += 18;
            timingLabel.setBounds(padX, sy, w - padX * 2, 18);
            sy += 18;
            formatLabel.setBounds(padX, sy, w - padX * 2, 18);

            y += sectionH + bodyH + 8;
        }

        content.setSize(w, y);
    }

    //==========================================================================
    Timeline* timeline = nullptr;
    AudioObject::Ptr currentObj;
    bool updatingUI = false;

    juce::Viewport viewport;
    juce::Component content;

    // Header
    juce::TextEditor nameEditor;
    juce::Label idBadge;
    juce::ToggleButton muteToggle;

    // Transform
    SectionBlock transformSection;
    std::unique_ptr<Vec3Row> positionRow;
    std::unique_ptr<Vec3Row> rotationRow;

    // Audio
    SectionBlock audioSection;
    CustomKnob volumeKnob { "Volume" };
    CustomKnob speedKnob { "Speed" };
    CustomKnob reverbKnob { "Reverb Mix" };

    // Clip info
    SectionBlock infoSection;
    juce::Label fileLabel;
    juce::Label timingLabel;
    juce::Label formatLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Inspector)
};
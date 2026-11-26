#pragma once

#include <JuceHeader.h>
#include "../../AetherState.h"

#include <functional>
#include <memory>
#include <vector>

class DockTab : public juce::Component
{
public:
    explicit DockTab(juce::String tabTitle);

    void setActive(bool shouldBeActive);
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseEnter(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent& event) override;

    std::function<void()> onSelect;
    std::function<void()> onClose;
    std::function<void(juce::Point<int>)> onDragStart;
    std::function<void(juce::Point<int>)> onDragMove;
    std::function<void(juce::Point<int>)> onDragEnd;

private:
    juce::Rectangle<int> getCloseBounds() const;

    juce::String title;
    juce::Point<int> mouseDownScreenPosition;
    bool active = false;
    bool hovered = false;
    bool closeHovered = false;
    bool pressedClose = false;
    bool dragging = false;
};

class DockSplitter : public juce::Component
{
public:
    explicit DockSplitter(bool resizesHorizontally);

    void paint(juce::Graphics& g) override;
    void mouseEnter(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent& event) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;

    std::function<void()> onDragStart;
    std::function<void(int)> onDragDelta;

private:
    bool horizontal;
    bool hovered = false;
    bool dragging = false;
};

class DockArea : public juce::Component
{
public:
    explicit DockArea(juce::String id);
    ~DockArea() override;

    void addPanel(juce::Component& panel, const juce::String& panelName);
    void removePanel(juce::Component* panel);
    bool containsPanel(const juce::Component* panel) const;
    void setActivePanel(juce::Component* panel);
    juce::Component* getActivePanel() const;
    int getPanelCount() const;
    const juce::String& getDockId() const noexcept { return dockId; }

    void paint(juce::Graphics& g) override;
    void resized() override;

    std::function<void(juce::Component*)> onCloseRequested;
    std::function<void(juce::Component*, juce::Point<int>)> onTabDragStart;
    std::function<void(juce::Component*, juce::Point<int>)> onTabDragMove;
    std::function<void(juce::Component*, juce::Point<int>)> onTabDragEnd;
    std::function<void(DockArea*)> onActivated;

private:
    struct TabEntry
    {
        juce::Component* panel = nullptr;
        juce::String name;
        std::unique_ptr<DockTab> tab;
    };

    static constexpr int tabBarHeight = 32;

    juce::String dockId;
    std::vector<TabEntry> tabs;
    int activeIndex = -1;
};

class DockPreview : public juce::Component,
                    private juce::Timer
{
public:
    void show(juce::Image image, juce::Rectangle<int> bounds);
    void moveTowards(juce::Rectangle<int> bounds);
    void animateTo(juce::Rectangle<int> bounds);
    void hide();
    void paint(juce::Graphics& g) override;

private:
    void timerCallback() override;

    juce::Image snapshot;
    juce::Rectangle<int> targetBounds;
    bool dismissWhenSettled = false;
};

class DropOverlay : public juce::Component
{
public:
    void showZone(juce::Rectangle<int> zone);
    void hideZone();
    void paint(juce::Graphics& g) override;
};

class Workspace : public juce::Component
{
public:
    Workspace();

    juce::AudioDeviceManager& getAudioDeviceManager() noexcept { return audioDeviceManager; }
    AetherState& getProjectState() noexcept { return projectState; }
    AetherState captureProjectState();
    bool exportAudio(const juce::File& file, juce::String& errorMessage);
    bool loadProjectState(const AetherState& state, juce::String& errorMessage);
    void setProjectFolder(juce::File folder);
    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    struct PanelType
    {
        juce::String typeId;
        juce::String displayName;
        std::function<std::unique_ptr<juce::Component>()> create;
    };

    struct PanelInstance
    {
        int id = -1;
        juce::String typeId;
        juce::String name;
        std::unique_ptr<juce::Component> component;
    };

    struct DockSlot
    {
        std::unique_ptr<DockArea> area;
        double size = 0.0;
    };

    struct DockColumn
    {
        double size = 0.0;
        std::vector<DockSlot> slots;
        std::vector<std::unique_ptr<DockSplitter>> rowSplitters;
    };

    enum class DropSide
    {
        left,
        right,
        top,
        bottom,
        center
    };

    struct DropTarget
    {
        bool valid = false;
        DropSide side = DropSide::center;
        DockArea* anchor = nullptr;
        juce::Rectangle<int> highlight;
    };

    static constexpr int toolbarHeight = 36;
    static constexpr int splitterThickness = 5;
    static constexpr int minDockWidth = 120;
    static constexpr int minDockHeight = 90;
    static constexpr int newColumnWidth = 260;
    static constexpr int workspaceEdgeZone = 36;
    static constexpr int resetLayoutMenuId = 10000;
    static constexpr float dropEdgeFraction = 0.25f;

    void registerPanelType(
        juce::String typeId,
        juce::String displayName,
        std::function<std::unique_ptr<juce::Component>()> factory);
    const PanelType* findPanelType(const juce::String& typeId) const;
    juce::String makeUniquePanelName(const PanelType& type) const;
    PanelInstance* createPanel(const juce::String& typeId, DockArea* dock);
    PanelInstance* findInstance(const juce::Component* panel) const;
    PanelInstance* findInstanceById(int id) const;
    void requestClosePanel(juce::Component* panel);
    void closePanelInstance(int panelId);
    void showPanelsMenu();
    void resetLayout();

    void wireDock(DockArea& area);
    void buildStartupLayout();
    DockArea* createDock(DockArea* anchor, DropSide side);
    void removeDock(DockArea* dock);
    DockArea* findDockById(const juce::String& id) const;
    DockArea* findDockAreaFor(const juce::Component* panel) const;
    DockArea* getDockAreaAt(juce::Point<int> position) const;
    DockArea* getDockForNewPanel();
    bool locateDock(const DockArea* dock, int& columnIndex, int& slotIndex) const;
    bool isDockAlive(const DockArea* dock) const;

    void rebuildSplitters();
    void layoutDocks();
    void beginColumnResize(int columnIndex);
    void resizeColumns(int columnIndex, int delta);
    void beginRowResize(int columnIndex, int slotIndex);
    void resizeRows(int columnIndex, int slotIndex, int delta);

    juce::Rectangle<int> getPanelBoundsInWorkspace(juce::Component* panel) const;
    DropTarget computeDropTarget(juce::Point<int> position) const;
    void beginPanelTabDrag(juce::Component* panel, juce::Point<int> screenPosition);
    void updatePanelTabDrag(juce::Point<int> screenPosition);
    void finishPanelTabDrag(juce::Point<int> screenPosition);
    void applyPanelDrop(int panelId, DockArea* source, DropTarget target);

    juce::File projectFolder;
    AetherState projectState;
    juce::TextButton panelsButton { "Panels +" };
    DropOverlay dropOverlay;
    DockPreview dockPreview;

    juce::AudioDeviceManager audioDeviceManager;
    std::vector<PanelType> panelTypes;
    std::vector<std::unique_ptr<PanelInstance>> panelInstances;
    std::vector<DockColumn> columns;
    std::vector<std::unique_ptr<DockSplitter>> columnSplitters;

    DockArea* lastActiveDock = nullptr;
    DockArea* sourceDockArea = nullptr;
    DropTarget dropTarget;
    int nextPanelId = 1;
    int nextDockNumber = 1;
    int draggedPanelId = -1;
    double resizeStartA = 0.0;
    double resizeStartB = 0.0;
    bool draggingTab = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Workspace)
};

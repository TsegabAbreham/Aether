#include "Workspace.h"

#include "Panels/Viewport.h"
#include "Panels/Timeline.h"
#include "Panels/Inspector.h"
#include "Panels/FileExplorer.h"
#include "Panels/Hierarchy.h"

#include <algorithm>
#include <cmath>
#include <utility>

//==============================================================================
//  STARTUP CONFIGURATION
//
//  Edit this block to change the initial docks. Nothing else in the file knows
//  about "left", "right" or "bottom" - docks are just cells in a grid.
//
//  column / row : position in the grid (columns go left -> right, rows go
//                 top -> bottom inside a column)
//  width        : preferred width of the column  (0 = flexible, takes the rest)
//  height       : preferred height of the dock   (0 = flexible, takes the rest)
//
//  Panel types: "file_explorer", "viewport", "inspector", "timeline"
//  (register more in Workspace::Workspace()).
//==============================================================================
namespace StartupConfig
{
    struct DockConfig
    {
        juce::String id;
        int column;
        int row;
        int width;
        int height;
    };

    struct StartupPanel
    {
        juce::String dockId;
        juce::String panelType;
    };

    std::vector<DockConfig> getDocks()
    {
        return {
            //  id             column row  width height
            { "left_dock",     0,     0,   220,  0   },
            { "center_dock",   1,     0,   0,    0   },
            { "bottom_dock",   1,     1,   0,    250 },
            { "right_dock",    2,     0,   250,  0   },
        };
    }

    std::vector<StartupPanel> getPanels()
    {
        return {
            { "left_dock",   "hiery" },
            { "center_dock", "viewport"      },
            { "right_dock",  "hierarchy"     },
            { "bottom_dock", "timeline"      },
        };
    }
}

namespace
{
    //==========================================================================
    // Colours

    const juce::Colour backgroundColour       { 0xff101318 };
    const juce::Colour toolbarColour          { 0xff171b22 };
    const juce::Colour dockColour             { 0xff151a21 };
    const juce::Colour tabBarColour           { 0xff191e26 };
    const juce::Colour inactiveTabColour      { 0xff202630 };
    const juce::Colour activeTabColour        { 0xff2b3542 };
    const juce::Colour hoverTabColour         { 0xff303946 };
    const juce::Colour accentColour           { 0xff75baff };
    const juce::Colour borderColour           { 0xff303945 };
    const juce::Colour textColour             { 0xffe0e6ef };
    const juce::Colour secondaryTextColour    { 0xff8793a3 };
    const juce::Colour splitterColour         { 0xff252c36 };
    const juce::Colour splitterLineColour     { 0xff394453 };


    //==========================================================================
    // Fits a row of sizes into the available space.
    //
    // - sizes <= 0 are "flexible" and receive whatever the fixed ones leave
    // - every size is kept >= minSize
    // - any surplus/deficit goes to the largest entry (like an IDE: the main
    //   editor absorbs window resizing, side panels keep their width)
    //
    // `sizes` is updated in place so the stored layout always matches what is
    // on screen. The returned integer pixel sizes add up exactly to `available`.

    std::vector<int> fitSizes(std::vector<double>& sizes, int available, int minSize)
    {
        const int n = static_cast<int>(sizes.size());
        std::vector<int> result(static_cast<size_t>(n), 0);

        if (n == 0)
            return result;

        available = juce::jmax(0, available);

        // Not enough room for the minimums: share evenly for now, but leave
        // the stored sizes alone so the layout comes back when the window grows.
        if (available < n * minSize)
        {
            int used = 0;

            for (int i = 0; i < n - 1; ++i)
            {
                result[static_cast<size_t>(i)] = available / n;
                used += result[static_cast<size_t>(i)];
            }

            result[static_cast<size_t>(n - 1)] = available - used;
            return result;
        }

        // 1. Flexible entries take what the fixed ones leave.
        double fixedTotal = 0.0;
        int flexibleCount = 0;

        for (auto size : sizes)
        {
            if (size > 0.0)
                fixedTotal += size;
            else
                ++flexibleCount;
        }

        if (flexibleCount > 0)
        {
            const double share = juce::jmax(
                static_cast<double>(minSize),
                (available - fixedTotal) / flexibleCount);

            for (auto& size : sizes)
                if (size <= 0.0)
                    size = share;
        }

        // 2. Enforce minimums.
        for (auto& size : sizes)
            size = juce::jmax(size, static_cast<double>(minSize));

        // 3. Hand the difference to the largest entry.
        double total = 0.0;

        for (auto size : sizes)
            total += size;

        const double difference = available - total;

        if (difference > 0.0)
        {
            auto largest = std::max_element(sizes.begin(), sizes.end());
            *largest += difference;
        }
        else if (difference < 0.0)
        {
            double excess = -difference;

            while (excess > 0.5)
            {
                int index = -1;
                double best = static_cast<double>(minSize) + 0.5;

                for (int i = 0; i < n; ++i)
                {
                    if (sizes[static_cast<size_t>(i)] > best)
                    {
                        best = sizes[static_cast<size_t>(i)];
                        index = i;
                    }
                }

                if (index < 0)
                    break;

                const double take = juce::jmin(
                    excess,
                    sizes[static_cast<size_t>(index)] - minSize);

                sizes[static_cast<size_t>(index)] -= take;
                excess -= take;
            }
        }

        // 4. Integer pixels that add up exactly.
        int used = 0;

        for (int i = 0; i < n - 1; ++i)
        {
            result[static_cast<size_t>(i)] =
                juce::roundToInt(sizes[static_cast<size_t>(i)]);

            used += result[static_cast<size_t>(i)];
        }

        result[static_cast<size_t>(n - 1)] = available - used;

        return result;
    }
}


//==============================================================================
// DockTab
//==============================================================================

DockTab::DockTab(juce::String tabTitle)
    : title(std::move(tabTitle))
{
    setMouseCursor(juce::MouseCursor::DraggingHandCursor);
}


juce::Rectangle<int> DockTab::getCloseBounds() const
{
    return {
        juce::jmax(0, getWidth() - 28),
        0,
        28,
        getHeight()
    };
}


void DockTab::setActive(bool shouldBeActive)
{
    if (active != shouldBeActive)
    {
        active = shouldBeActive;
        repaint();
    }
}


void DockTab::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced(1.0f, 1.0f);

    juce::Colour tabColour = inactiveTabColour;

    if (active)
        tabColour = activeTabColour;
    else if (hovered)
        tabColour = hoverTabColour;

    g.setColour(tabColour);
    g.fillRoundedRectangle(bounds, 5.0f);

    if (active)
    {
        g.setColour(accentColour);

        g.fillRoundedRectangle(
            juce::Rectangle<float>(
                bounds.getX() + 7.0f,
                bounds.getBottom() - 3.0f,
                bounds.getWidth() - 14.0f,
                2.0f),
            1.0f);
    }

    auto textBounds = getLocalBounds()
        .reduced(10, 0)
        .withTrimmedRight(29);

    g.setColour(active ? textColour : secondaryTextColour);

    g.setFont(juce::Font(
        12.5f,
        active ? juce::Font::bold : juce::Font::plain));

    g.drawText(
        title,
        textBounds,
        juce::Justification::centredLeft,
        true);

    if (active || hovered)
    {
        const auto closeArea = getCloseBounds().toFloat();

        const float centerX = closeArea.getCentreX();
        const float centerY = closeArea.getCentreY();
        const float iconSize = 4.0f;

        g.setColour(closeHovered ? textColour : secondaryTextColour);

        g.drawLine(
            centerX - iconSize, centerY - iconSize,
            centerX + iconSize, centerY + iconSize,
            1.5f);

        g.drawLine(
            centerX + iconSize, centerY - iconSize,
            centerX - iconSize, centerY + iconSize,
            1.5f);
    }
}


void DockTab::mouseDown(const juce::MouseEvent& event)
{
    mouseDownScreenPosition = event.getScreenPosition();

    pressedClose = getCloseBounds().contains(event.getPosition());

    dragging = false;

    if (!pressedClose && onSelect)
        onSelect();
}


void DockTab::mouseDrag(const juce::MouseEvent& event)
{
    if (pressedClose)
        return;

    const auto currentScreenPosition = event.getScreenPosition();

    const int dx = std::abs(currentScreenPosition.x - mouseDownScreenPosition.x);
    const int dy = std::abs(currentScreenPosition.y - mouseDownScreenPosition.y);

    if (!dragging && dx + dy > 7)
    {
        dragging = true;

        if (onDragStart)
            onDragStart(currentScreenPosition);
    }

    if (dragging && onDragMove)
        onDragMove(currentScreenPosition);
}


void DockTab::mouseUp(const juce::MouseEvent& event)
{
    const auto screenPosition = event.getScreenPosition();

    const bool wasDragging = dragging;
    const bool wasPressedClose = pressedClose;
    const bool releasedOnClose = getCloseBounds().contains(event.getPosition());

    // Reset state BEFORE calling back: the callbacks may lead to this tab
    // being destroyed, so no member may be touched afterwards.
    dragging = false;
    pressedClose = false;

    if (wasDragging)
    {
        if (onDragEnd)
            onDragEnd(screenPosition);
    }
    else if (wasPressedClose && releasedOnClose)
    {
        if (onClose)
            onClose();
    }
}


void DockTab::mouseMove(const juce::MouseEvent& event)
{
    const bool shouldBeHovered = getLocalBounds().contains(event.getPosition());
    const bool shouldCloseBeHovered = getCloseBounds().contains(event.getPosition());

    if (hovered != shouldBeHovered || closeHovered != shouldCloseBeHovered)
    {
        hovered = shouldBeHovered;
        closeHovered = shouldCloseBeHovered;
        repaint();
    }
}


void DockTab::mouseEnter(const juce::MouseEvent&)
{
    hovered = true;
    repaint();
}


void DockTab::mouseExit(const juce::MouseEvent&)
{
    hovered = false;
    closeHovered = false;
    repaint();
}


//==============================================================================
// DockSplitter
//==============================================================================

DockSplitter::DockSplitter(bool resizesHorizontally)
    : horizontal(resizesHorizontally)
{
    setOpaque(true);

    setMouseCursor(horizontal
        ? juce::MouseCursor::LeftRightResizeCursor
        : juce::MouseCursor::UpDownResizeCursor);
}


void DockSplitter::paint(juce::Graphics& g)
{
    g.fillAll(splitterColour);

    if (hovered || dragging)
        g.fillAll(accentColour.withAlpha(dragging ? 0.55f : 0.30f));

    g.setColour(splitterLineColour);

    if (horizontal)
        g.fillRect(0, 0, 1, getHeight());
    else
        g.fillRect(0, 0, getWidth(), 1);
}


void DockSplitter::mouseEnter(const juce::MouseEvent&)
{
    hovered = true;
    repaint();
}


void DockSplitter::mouseExit(const juce::MouseEvent&)
{
    hovered = false;
    repaint();
}


void DockSplitter::mouseDown(const juce::MouseEvent&)
{
    dragging = true;
    repaint();

    if (onDragStart)
        onDragStart();
}


void DockSplitter::mouseDrag(const juce::MouseEvent& event)
{
    // Screen coordinates, because this component moves while being dragged.
    // Component-relative offsets would feed back on themselves.
    const int delta = horizontal
        ? event.getScreenX() - event.getMouseDownScreenX()
        : event.getScreenY() - event.getMouseDownScreenY();

    if (onDragDelta)
        onDragDelta(delta);
}


void DockSplitter::mouseUp(const juce::MouseEvent&)
{
    dragging = false;
    repaint();
}


//==============================================================================
// DockArea
//==============================================================================

DockArea::DockArea(juce::String id)
    : dockId(std::move(id))
{
    setOpaque(true);
}


DockArea::~DockArea()
{
    // Panels are owned by Workspace - just detach them.
    for (auto& entry : tabs)
    {
        if (entry.panel != nullptr
            && entry.panel->getParentComponent() == this)
        {
            entry.panel->setVisible(false);
            removeChildComponent(entry.panel);
        }

        if (entry.tab != nullptr)
            removeChildComponent(entry.tab.get());
    }
}


void DockArea::addPanel(juce::Component& panel, const juce::String& panelName)
{
    if (containsPanel(&panel))
    {
        setActivePanel(&panel);
        return;
    }

    if (auto* previousParent = panel.getParentComponent())
        previousParent->removeChildComponent(&panel);

    addAndMakeVisible(panel);

    TabEntry entry;

    entry.panel = &panel;
    entry.name = panelName;
    entry.tab = std::make_unique<DockTab>(panelName);

    auto* panelPointer = &panel;

    entry.tab->onSelect = [this, panelPointer]
    {
        setActivePanel(panelPointer);
    };

    entry.tab->onClose = [this, panelPointer]
    {
        if (onCloseRequested)
            onCloseRequested(panelPointer);
    };

    entry.tab->onDragStart = [this, panelPointer](juce::Point<int> position)
    {
        if (onTabDragStart)
            onTabDragStart(panelPointer, position);
    };

    entry.tab->onDragMove = [this, panelPointer](juce::Point<int> position)
    {
        if (onTabDragMove)
            onTabDragMove(panelPointer, position);
    };

    entry.tab->onDragEnd = [this, panelPointer](juce::Point<int> position)
    {
        if (onTabDragEnd)
            onTabDragEnd(panelPointer, position);
    };

    addAndMakeVisible(entry.tab.get());

    tabs.push_back(std::move(entry));

    setActivePanel(&panel);
}


void DockArea::removePanel(juce::Component* panel)
{
    if (panel == nullptr)
        return;

    auto iterator = std::find_if(
        tabs.begin(),
        tabs.end(),
        [panel](const TabEntry& entry) { return entry.panel == panel; });

    if (iterator == tabs.end())
        return;

    const int removedIndex =
        static_cast<int>(std::distance(tabs.begin(), iterator));

    if (iterator->tab != nullptr)
        removeChildComponent(iterator->tab.get());

    if (panel->getParentComponent() == this)
    {
        panel->setVisible(false);
        removeChildComponent(panel);
    }

    tabs.erase(iterator);

    if (tabs.empty())
    {
        activeIndex = -1;
    }
    else
    {
        if (removedIndex < activeIndex)
            --activeIndex;
        else if (removedIndex == activeIndex)
            activeIndex = juce::jmin(removedIndex, static_cast<int>(tabs.size()) - 1);

        setActivePanel(tabs[static_cast<size_t>(activeIndex)].panel);
    }

    resized();
    repaint();
}


bool DockArea::containsPanel(const juce::Component* panel) const
{
    return std::any_of(
        tabs.begin(),
        tabs.end(),
        [panel](const TabEntry& entry) { return entry.panel == panel; });
}


void DockArea::setActivePanel(juce::Component* panel)
{
    bool found = false;

    for (int i = 0; i < static_cast<int>(tabs.size()); ++i)
    {
        auto& entry = tabs[static_cast<size_t>(i)];

        const bool selected = entry.panel == panel;

        if (selected)
        {
            activeIndex = i;
            found = true;
        }

        if (entry.panel != nullptr)
            entry.panel->setVisible(selected);

        if (entry.tab != nullptr)
            entry.tab->setActive(selected);
    }

    if (!found)
        return;

    resized();
    repaint();

    if (onActivated)
        onActivated(this);
}


juce::Component* DockArea::getActivePanel() const
{
    if (activeIndex < 0 || activeIndex >= static_cast<int>(tabs.size()))
        return nullptr;

    return tabs[static_cast<size_t>(activeIndex)].panel;
}


int DockArea::getPanelCount() const
{
    return static_cast<int>(tabs.size());
}


void DockArea::paint(juce::Graphics& g)
{
    g.fillAll(dockColour);

    g.setColour(tabBarColour);
    g.fillRect(0, 0, getWidth(), tabBarHeight);

    g.setColour(borderColour);

    g.drawHorizontalLine(
        tabBarHeight - 1,
        0.0f,
        static_cast<float>(getWidth()));

    g.drawRect(getLocalBounds(), 1);
}


void DockArea::resized()
{
    const int count = static_cast<int>(tabs.size());

    if (count == 0)
        return;

    const int gap = 4;
    const int leftPadding = 8;

    const int availableWidth =
        juce::jmax(0, getWidth() - leftPadding * 2 - gap * (count - 1));

    const int tabWidth = juce::jlimit(48, 170, availableWidth / count);

    int x = leftPadding;

    for (auto& entry : tabs)
    {
        if (entry.tab != nullptr)
            entry.tab->setBounds(x, 4, tabWidth, tabBarHeight - 8);

        x += tabWidth + gap;
    }

    // Only the visible panel is laid out. Hidden panels are resized when they
    // become active (setActivePanel calls resized()), which keeps resizing cheap.
    if (auto* panel = getActivePanel())
    {
        panel->setBounds(
            getLocalBounds()
                .withTrimmedTop(tabBarHeight)
                .reduced(1));
    }
}


//==============================================================================
// DockPreview
//==============================================================================

void DockPreview::show(juce::Image image, juce::Rectangle<int> bounds)
{
    snapshot = std::move(image);

    targetBounds = bounds;
    dismissWhenSettled = false;

    setBounds(bounds);
    setVisible(true);
    toFront(false);

    startTimerHz(60);
}


void DockPreview::moveTowards(juce::Rectangle<int> bounds)
{
    targetBounds = bounds;
}


void DockPreview::animateTo(juce::Rectangle<int> bounds)
{
    if (!isVisible())
        return;

    targetBounds = bounds;
    dismissWhenSettled = true;
}


void DockPreview::hide()
{
    stopTimer();
    setVisible(false);
}


void DockPreview::paint(juce::Graphics& g)
{
    if (!snapshot.isValid())
        return;

    g.setOpacity(0.48f);

    g.drawImageWithin(
        snapshot,
        0, 0,
        getWidth(), getHeight(),
        juce::RectanglePlacement::stretchToFit);

    g.setOpacity(1.0f);

    g.setColour(accentColour.withAlpha(0.9f));

    g.drawRoundedRectangle(
        getLocalBounds().toFloat().reduced(0.5f),
        5.0f,
        2.0f);
}


void DockPreview::timerCallback()
{
    const auto current = getBounds();

    const auto blend = [](int from, int to)
    {
        return from + static_cast<int>(std::round((to - from) * 0.32));
    };

    const juce::Rectangle<int> next(
        blend(current.getX(), targetBounds.getX()),
        blend(current.getY(), targetBounds.getY()),
        blend(current.getWidth(), targetBounds.getWidth()),
        blend(current.getHeight(), targetBounds.getHeight()));

    if (dismissWhenSettled
        && std::abs(next.getX() - targetBounds.getX()) <= 1
        && std::abs(next.getY() - targetBounds.getY()) <= 1
        && std::abs(next.getWidth() - targetBounds.getWidth()) <= 1
        && std::abs(next.getHeight() - targetBounds.getHeight()) <= 1)
    {
        hide();
        return;
    }

    setBounds(next);
}


//==============================================================================
// DropOverlay
//==============================================================================

void DropOverlay::showZone(juce::Rectangle<int> zone)
{
    if (getBounds() != zone)
        setBounds(zone);

    if (!isVisible())
        setVisible(true);

    toFront(false);
}


void DropOverlay::hideZone()
{
    if (isVisible())
        setVisible(false);
}


void DropOverlay::paint(juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat().reduced(2.0f);

    g.setColour(accentColour.withAlpha(0.18f));
    g.fillRoundedRectangle(area, 4.0f);

    g.setColour(accentColour.withAlpha(0.9f));
    g.drawRoundedRectangle(area, 4.0f, 2.0f);
}


//==============================================================================
// Workspace
//==============================================================================

Workspace::Workspace()
    : projectFolder(juce::File::getSpecialLocation(
          juce::File::userDocumentsDirectory))
{
    setOpaque(true);

    //==========================================================================
    // Panel types. Add new panel kinds here.

    registerPanelType("file_explorer", "File Explorer",
        [this]() -> std::unique_ptr<juce::Component>
        {
            return std::make_unique<FileExplorer>(projectFolder);
        });

    registerPanelType("viewport", "Viewport",
        []() -> std::unique_ptr<juce::Component>
        {
            return std::make_unique<ViewportPanel>();
        });

    registerPanelType("inspector", "Inspector",
        []() -> std::unique_ptr<juce::Component>
        {
            return std::make_unique<Inspector>();
        });

    registerPanelType("timeline", "Timeline",
        []() -> std::unique_ptr<juce::Component>
        {
            return std::make_unique<Timeline>();
        });

    registerPanelType("hierarchy", "Hierarchy",
        []() -> std::unique_ptr<juce::Component>
        {
            return std::make_unique<Hierarchy>();
        });

    //==========================================================================
    // Toolbar button.

    addAndMakeVisible(panelsButton);

    panelsButton.setColour(juce::TextButton::buttonColourId,   juce::Colour(0xff293442));
    panelsButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff35485d));
    panelsButton.setColour(juce::TextButton::textColourOffId,  textColour);
    panelsButton.setColour(juce::TextButton::textColourOnId,   textColour);

    panelsButton.setClickingTogglesState(false);

    panelsButton.onClick = [this] { showPanelsMenu(); };

    //==========================================================================
    // Overlays (always on top).

    addChildComponent(dropOverlay);
    addChildComponent(dockPreview);

    buildStartupLayout();
}


//==============================================================================
// Project folder
//==============================================================================

void Workspace::setProjectFolder(juce::File folder)
{
    projectFolder = folder;

    for (auto& instance : panelInstances)
    {
        if (auto* explorer = dynamic_cast<FileExplorer*>(instance->component.get()))
            explorer->setRootFolder(folder);
    }
}


//==============================================================================
// Painting / resizing
//==============================================================================

void Workspace::paint(juce::Graphics& g)
{
    g.fillAll(backgroundColour);

    g.setColour(toolbarColour);
    g.fillRect(0, 0, getWidth(), toolbarHeight);

    g.setColour(borderColour);

    g.drawHorizontalLine(
        toolbarHeight - 1,
        0.0f,
        static_cast<float>(getWidth()));

    g.setColour(accentColour);
    g.fillRoundedRectangle(10.0f, 11.0f, 4.0f, 14.0f, 2.0f);

    g.setColour(textColour);
    g.setFont(juce::Font(13.0f, juce::Font::bold));

    g.drawText("AETHER", 22, 0, 58, toolbarHeight, juce::Justification::centredLeft);

    g.setColour(secondaryTextColour);
    g.setFont(juce::Font(11.0f));

    g.drawText("WORKSPACE", 82, 0, 100, toolbarHeight, juce::Justification::centredLeft);

    if (columns.empty())
    {
        g.setColour(secondaryTextColour);
        g.setFont(juce::Font(13.0f));

        g.drawText(
            "No panels open - use \"Panels +\" to add one",
            getLocalBounds().withTrimmedTop(toolbarHeight),
            juce::Justification::centred);
    }
}


void Workspace::resized()
{
    panelsButton.setBounds(
        juce::jmax(8, getWidth() - 112),
        5,
        100,
        26);

    // No repaint() here: children repaint themselves when their bounds change.
    layoutDocks();
}


//==============================================================================
// Panel types and instances
//==============================================================================

void Workspace::registerPanelType(
    juce::String typeId,
    juce::String displayName,
    std::function<std::unique_ptr<juce::Component>()> factory)
{
    panelTypes.push_back({ std::move(typeId),
                           std::move(displayName),
                           std::move(factory) });
}


const Workspace::PanelType* Workspace::findPanelType(const juce::String& typeId) const
{
    for (const auto& type : panelTypes)
        if (type.typeId == typeId)
            return &type;

    return nullptr;
}


juce::String Workspace::makeUniquePanelName(const PanelType& type) const
{
    const auto inUse = [this](const juce::String& name)
    {
        for (const auto& instance : panelInstances)
            if (instance->name == name)
                return true;

        return false;
    };

    if (!inUse(type.displayName))
        return type.displayName;

    for (int n = 2;; ++n)
    {
        const auto candidate = type.displayName + " " + juce::String(n);

        if (!inUse(candidate))
            return candidate;
    }
}


Workspace::PanelInstance* Workspace::createPanel(const juce::String& typeId, DockArea* dock)
{
    const auto* type = findPanelType(typeId);

    if (type == nullptr || !type->create)
        return nullptr;

    auto instance = std::make_unique<PanelInstance>();

    instance->id = nextPanelId++;
    instance->typeId = typeId;
    instance->name = makeUniquePanelName(*type);
    instance->component = type->create();

    if (instance->component == nullptr)
        return nullptr;

    auto* instancePointer = instance.get();
    auto* component = instance->component.get();
    const auto name = instance->name;

    panelInstances.push_back(std::move(instance));

    if (auto* timelinePanel = dynamic_cast<Timeline*>(component))
    {
        for (const auto& panel : panelInstances)
        {
            if (auto* hierarchy = dynamic_cast<Hierarchy*>(panel->component.get()))
                hierarchy->setTimeline(timelinePanel);

            if (auto* inspector = dynamic_cast<Inspector*>(panel->component.get()))
                inspector->setTimeline(timelinePanel);

            if (auto* viewport = dynamic_cast<ViewportPanel*>(panel->component.get()))
                viewport->setTimeline(timelinePanel);
        }
    }
    else if (auto* hierarchy = dynamic_cast<Hierarchy*>(component))
    {
        for (auto panel = panelInstances.rbegin(); panel != panelInstances.rend(); ++panel)
            if (auto* availableTimeline = dynamic_cast<Timeline*>((*panel)->component.get()))
            {
                hierarchy->setTimeline(availableTimeline);
                break;
            }
    }
    else if (auto* inspector = dynamic_cast<Inspector*>(component))
    {
        for (auto panel = panelInstances.rbegin(); panel != panelInstances.rend(); ++panel)
            if (auto* availableTimeline = dynamic_cast<Timeline*>((*panel)->component.get()))
            {
                inspector->setTimeline(availableTimeline);
                break;
            }
    }
    else if (auto* viewport = dynamic_cast<ViewportPanel*>(component))
    {
        for (auto panel = panelInstances.rbegin(); panel != panelInstances.rend(); ++panel)
            if (auto* availableTimeline = dynamic_cast<Timeline*>((*panel)->component.get()))
            {
                viewport->setTimeline(availableTimeline);
                break;
            }
    }

    if (dock == nullptr)
        dock = getDockForNewPanel();

    dock->addPanel(*component, name);

    layoutDocks();

    return instancePointer;
}


Workspace::PanelInstance* Workspace::findInstance(const juce::Component* panel) const
{
    for (const auto& instance : panelInstances)
        if (instance->component.get() == panel)
            return instance.get();

    return nullptr;
}


Workspace::PanelInstance* Workspace::findInstanceById(int id) const
{
    for (const auto& instance : panelInstances)
        if (instance->id == id)
            return instance.get();

    return nullptr;
}


void Workspace::requestClosePanel(juce::Component* panel)
{
    auto* instance = findInstance(panel);

    if (instance == nullptr)
        return;

    const int panelId = instance->id;

    // Deferred: this is called from inside the tab's own mouse handler, and
    // closing destroys that tab.
    juce::Component::SafePointer<Workspace> safeThis(this);

    juce::MessageManager::callAsync([safeThis, panelId]
    {
        if (safeThis != nullptr)
            safeThis->closePanelInstance(panelId);
    });
}


void Workspace::closePanelInstance(int panelId)
{
    auto iterator = std::find_if(
        panelInstances.begin(),
        panelInstances.end(),
        [panelId](const std::unique_ptr<PanelInstance>& instance)
        {
            return instance->id == panelId;
        });

    if (iterator == panelInstances.end())
        return;

    auto* component = (*iterator)->component.get();

    if (dynamic_cast<Timeline*>(component) != nullptr)
    {
        Timeline* replacement = nullptr;

        for (auto panel = panelInstances.rbegin(); panel != panelInstances.rend(); ++panel)
            if (panel->get() != iterator->get())
                if (auto* timeline = dynamic_cast<Timeline*>((*panel)->component.get()))
                {
                    replacement = timeline;
                    break;
                }

        for (const auto& panel : panelInstances)
        {
            if (auto* hierarchy = dynamic_cast<Hierarchy*>(panel->component.get()))
                hierarchy->setTimeline(replacement);

            if (auto* inspector = dynamic_cast<Inspector*>(panel->component.get()))
                inspector->setTimeline(replacement);

            if (auto* viewport = dynamic_cast<ViewportPanel*>(panel->component.get()))
                viewport->setTimeline(replacement);
        }
    }

    if (auto* area = findDockAreaFor(component))
    {
        area->removePanel(component);

        // An empty dock disappears and its neighbour takes the space.
        if (area->getPanelCount() == 0)
            removeDock(area);
    }

    panelInstances.erase(iterator);

    layoutDocks();
    repaint();
}


//==============================================================================
// Panels menu
//==============================================================================

void Workspace::showPanelsMenu()
{
    juce::PopupMenu menu;

    menu.addSectionHeader("Add Panel");

    for (int i = 0; i < static_cast<int>(panelTypes.size()); ++i)
        menu.addItem(i + 1, "New " + panelTypes[static_cast<size_t>(i)].displayName);

    menu.addSeparator();
    menu.addItem(resetLayoutMenuId, "Reset Layout");

    juce::Component::SafePointer<Workspace> safeThis(this);

    menu.showMenuAsync(
        juce::PopupMenu::Options()
            .withTargetComponent(&panelsButton)
            .withMinimumWidth(200),

        [safeThis](int result)
        {
            if (safeThis == nullptr || result <= 0)
                return;

            if (result == resetLayoutMenuId)
            {
                safeThis->resetLayout();
                return;
            }

            const int typeIndex = result - 1;

            if (typeIndex < static_cast<int>(safeThis->panelTypes.size()))
            {
                safeThis->createPanel(
                    safeThis->panelTypes[static_cast<size_t>(typeIndex)].typeId,
                    nullptr);
            }
        });
}


void Workspace::resetLayout()
{
    dockPreview.hide();
    dropOverlay.hideZone();

    draggingTab = false;
    draggedPanelId = -1;
    sourceDockArea = nullptr;
    dropTarget = {};
    lastActiveDock = nullptr;

    // Docks first (they detach their panels), panels second.
    columnSplitters.clear();
    columns.clear();

    for (const auto& panel : panelInstances)
    {
        if (auto* hierarchy = dynamic_cast<Hierarchy*>(panel->component.get()))
            hierarchy->setTimeline(nullptr);

        if (auto* inspector = dynamic_cast<Inspector*>(panel->component.get()))
            inspector->setTimeline(nullptr);

        if (auto* viewport = dynamic_cast<ViewportPanel*>(panel->component.get()))
            viewport->setTimeline(nullptr);
    }

    panelInstances.clear();

    buildStartupLayout();

    repaint();
}


//==============================================================================
// Dock grid
//==============================================================================

void Workspace::wireDock(DockArea& area)
{
    area.onCloseRequested = [this](juce::Component* panel)
    {
        requestClosePanel(panel);
    };

    area.onTabDragStart = [this](juce::Component* panel, juce::Point<int> position)
    {
        beginPanelTabDrag(panel, position);
    };

    area.onTabDragMove = [this](juce::Component*, juce::Point<int> position)
    {
        updatePanelTabDrag(position);
    };

    area.onTabDragEnd = [this](juce::Component*, juce::Point<int> position)
    {
        finishPanelTabDrag(position);
    };

    area.onActivated = [this](DockArea* activated)
    {
        lastActiveDock = activated;
    };
}


void Workspace::buildStartupLayout()
{
    auto docks = StartupConfig::getDocks();

    std::stable_sort(
        docks.begin(),
        docks.end(),
        [](const StartupConfig::DockConfig& a, const StartupConfig::DockConfig& b)
        {
            return a.column != b.column ? a.column < b.column
                                        : a.row < b.row;
        });

    int currentColumn = 0;

    for (const auto& config : docks)
    {
        if (columns.empty() || config.column != currentColumn)
        {
            columns.emplace_back();
            currentColumn = config.column;
        }

        auto& column = columns.back();

        if (column.size <= 0.0 && config.width > 0)
            column.size = config.width;

        DockSlot slot;

        slot.area = std::make_unique<DockArea>(config.id);
        slot.size = config.height;

        wireDock(*slot.area);
        addAndMakeVisible(*slot.area);

        column.slots.push_back(std::move(slot));
    }

    rebuildSplitters();

    for (const auto& startup : StartupConfig::getPanels())
    {
        if (auto* dock = findDockById(startup.dockId))
            createPanel(startup.panelType, dock);
    }

    layoutDocks();
}


DockArea* Workspace::createDock(DockArea* anchor, DropSide side)
{
    auto area = std::make_unique<DockArea>("dock_" + juce::String(nextDockNumber++));
    auto* raw = area.get();

    wireDock(*raw);
    addAndMakeVisible(*raw);

    int columnIndex = -1;
    int slotIndex = -1;

    const bool hasAnchor =
        anchor != nullptr && locateDock(anchor, columnIndex, slotIndex);

    const auto insertColumn = [&](int index, double size)
    {
        DockColumn column;
        column.size = size;

        DockSlot slot;
        slot.area = std::move(area);
        column.slots.push_back(std::move(slot));

        columns.insert(columns.begin() + index, std::move(column));
    };

    if (!hasAnchor)
    {
        if (columns.empty())
            insertColumn(0, 0.0);
        else if (side == DropSide::left)
            insertColumn(0, newColumnWidth);
        else
            insertColumn(static_cast<int>(columns.size()), newColumnWidth);
    }
    else if (side == DropSide::left || side == DropSide::right)
    {
        // Split the anchor's column: the new column takes half of it (max 260px).
        auto& column = columns[static_cast<size_t>(columnIndex)];

        const double stolen = juce::jmin(
            static_cast<double>(newColumnWidth),
            column.size * 0.5);

        column.size -= stolen;

        insertColumn(side == DropSide::left ? columnIndex : columnIndex + 1, stolen);
    }
    else if (side == DropSide::top || side == DropSide::bottom)
    {
        // Split the anchor dock vertically: the new dock takes half of it.
        auto& slots = columns[static_cast<size_t>(columnIndex)].slots;

        auto& anchorSlot = slots[static_cast<size_t>(slotIndex)];

        const double stolen = anchorSlot.size * 0.5;
        anchorSlot.size -= stolen;

        DockSlot slot;
        slot.area = std::move(area);
        slot.size = stolen;

        slots.insert(
            slots.begin() + (side == DropSide::top ? slotIndex : slotIndex + 1),
            std::move(slot));
    }
    else
    {
        return nullptr;   // "center" with an anchor is not a new dock
    }

    rebuildSplitters();
    layoutDocks();

    return raw;
}


void Workspace::removeDock(DockArea* dock)
{
    int columnIndex = -1;
    int slotIndex = -1;

    if (!locateDock(dock, columnIndex, slotIndex))
        return;

    if (lastActiveDock == dock)
        lastActiveDock = nullptr;

    auto& column = columns[static_cast<size_t>(columnIndex)];

    if (column.slots.size() > 1)
    {
        // The dock above (or below, for the first dock) absorbs the space.
        const size_t neighbour = slotIndex > 0
            ? static_cast<size_t>(slotIndex - 1)
            : static_cast<size_t>(slotIndex + 1);

        column.slots[neighbour].size +=
            column.slots[static_cast<size_t>(slotIndex)].size;

        column.slots.erase(column.slots.begin() + slotIndex);
    }
    else
    {
        // Last dock in the column: the column goes, a neighbouring column
        // absorbs its width.
        if (columns.size() > 1)
        {
            const size_t neighbour = columnIndex > 0
                ? static_cast<size_t>(columnIndex - 1)
                : static_cast<size_t>(columnIndex + 1);

            columns[neighbour].size += column.size;
        }

        columns.erase(columns.begin() + columnIndex);
    }

    rebuildSplitters();
    layoutDocks();
    repaint();
}


DockArea* Workspace::findDockById(const juce::String& id) const
{
    for (const auto& column : columns)
        for (const auto& slot : column.slots)
            if (slot.area->getDockId() == id)
                return slot.area.get();

    return nullptr;
}


DockArea* Workspace::findDockAreaFor(const juce::Component* panel) const
{
    if (panel == nullptr)
        return nullptr;

    for (const auto& column : columns)
        for (const auto& slot : column.slots)
            if (slot.area->containsPanel(panel))
                return slot.area.get();

    return nullptr;
}


DockArea* Workspace::getDockAreaAt(juce::Point<int> position) const
{
    for (const auto& column : columns)
        for (const auto& slot : column.slots)
            if (slot.area->getBounds().contains(position))
                return slot.area.get();

    return nullptr;
}


DockArea* Workspace::getDockForNewPanel()
{
    if (lastActiveDock != nullptr && isDockAlive(lastActiveDock))
        return lastActiveDock;

    if (!columns.empty() && !columns.front().slots.empty())
        return columns.front().slots.front().area.get();

    // Nothing is open at all: create a fresh dock.
    return createDock(nullptr, DropSide::center);
}


bool Workspace::locateDock(const DockArea* dock, int& columnIndex, int& slotIndex) const
{
    for (int c = 0; c < static_cast<int>(columns.size()); ++c)
    {
        const auto& slots = columns[static_cast<size_t>(c)].slots;

        for (int s = 0; s < static_cast<int>(slots.size()); ++s)
        {
            if (slots[static_cast<size_t>(s)].area.get() == dock)
            {
                columnIndex = c;
                slotIndex = s;
                return true;
            }
        }
    }

    return false;
}


bool Workspace::isDockAlive(const DockArea* dock) const
{
    int c = 0;
    int s = 0;

    return dock != nullptr && locateDock(dock, c, s);
}


//==============================================================================
// Splitters and layout
//==============================================================================

void Workspace::rebuildSplitters()
{
    columnSplitters.clear();

    const int columnCount = static_cast<int>(columns.size());

    for (int c = 0; c + 1 < columnCount; ++c)
    {
        auto splitter = std::make_unique<DockSplitter>(true);

        splitter->onDragStart = [this, c] { beginColumnResize(c); };
        splitter->onDragDelta = [this, c](int delta) { resizeColumns(c, delta); };

        addAndMakeVisible(*splitter);
        columnSplitters.push_back(std::move(splitter));
    }

    for (int c = 0; c < columnCount; ++c)
    {
        auto& column = columns[static_cast<size_t>(c)];

        column.rowSplitters.clear();

        for (int s = 0; s + 1 < static_cast<int>(column.slots.size()); ++s)
        {
            auto splitter = std::make_unique<DockSplitter>(false);

            splitter->onDragStart = [this, c, s] { beginRowResize(c, s); };
            splitter->onDragDelta = [this, c, s](int delta) { resizeRows(c, s, delta); };

            addAndMakeVisible(*splitter);
            column.rowSplitters.push_back(std::move(splitter));
        }
    }

    dropOverlay.toFront(false);
    dockPreview.toFront(false);
}


void Workspace::layoutDocks()
{
    const auto content = getLocalBounds().withTrimmedTop(toolbarHeight);

    const int columnCount = static_cast<int>(columns.size());

    // Nothing to lay out yet (e.g. during construction, before the first resize).
    if (columnCount == 0 || content.isEmpty())
        return;

    std::vector<double> widths;

    for (const auto& column : columns)
        widths.push_back(column.size);

    const auto widthPixels = fitSizes(
        widths,
        content.getWidth() - (columnCount - 1) * splitterThickness,
        minDockWidth);

    int x = content.getX();

    for (int c = 0; c < columnCount; ++c)
    {
        auto& column = columns[static_cast<size_t>(c)];

        column.size = widths[static_cast<size_t>(c)];

        const int width = widthPixels[static_cast<size_t>(c)];
        const int slotCount = static_cast<int>(column.slots.size());

        std::vector<double> heights;

        for (const auto& slot : column.slots)
            heights.push_back(slot.size);

        const auto heightPixels = fitSizes(
            heights,
            content.getHeight() - (slotCount - 1) * splitterThickness,
            minDockHeight);

        int y = content.getY();

        for (int s = 0; s < slotCount; ++s)
        {
            auto& slot = column.slots[static_cast<size_t>(s)];

            slot.size = heights[static_cast<size_t>(s)];

            const int height = heightPixels[static_cast<size_t>(s)];

            slot.area->setBounds(x, y, width, height);

            y += height;

            if (s < static_cast<int>(column.rowSplitters.size()))
            {
                column.rowSplitters[static_cast<size_t>(s)]->setBounds(
                    x, y, width, splitterThickness);

                y += splitterThickness;
            }
        }

        x += width;

        if (c < static_cast<int>(columnSplitters.size()))
        {
            columnSplitters[static_cast<size_t>(c)]->setBounds(
                x, content.getY(), splitterThickness, content.getHeight());

            x += splitterThickness;
        }
    }
}


void Workspace::beginColumnResize(int columnIndex)
{
    if (columnIndex < 0 || columnIndex + 1 >= static_cast<int>(columns.size()))
        return;

    resizeStartA = columns[static_cast<size_t>(columnIndex)].size;
    resizeStartB = columns[static_cast<size_t>(columnIndex + 1)].size;
}


void Workspace::resizeColumns(int columnIndex, int delta)
{
    if (columnIndex < 0 || columnIndex + 1 >= static_cast<int>(columns.size()))
        return;

    const double low = minDockWidth - resizeStartA;
    const double high = juce::jmax(low, resizeStartB - minDockWidth);

    const double d = juce::jlimit(low, high, static_cast<double>(delta));

    columns[static_cast<size_t>(columnIndex)].size = resizeStartA + d;
    columns[static_cast<size_t>(columnIndex + 1)].size = resizeStartB - d;

    layoutDocks();
}


void Workspace::beginRowResize(int columnIndex, int slotIndex)
{
    if (columnIndex < 0 || columnIndex >= static_cast<int>(columns.size()))
        return;

    const auto& slots = columns[static_cast<size_t>(columnIndex)].slots;

    if (slotIndex < 0 || slotIndex + 1 >= static_cast<int>(slots.size()))
        return;

    resizeStartA = slots[static_cast<size_t>(slotIndex)].size;
    resizeStartB = slots[static_cast<size_t>(slotIndex + 1)].size;
}


void Workspace::resizeRows(int columnIndex, int slotIndex, int delta)
{
    if (columnIndex < 0 || columnIndex >= static_cast<int>(columns.size()))
        return;

    auto& slots = columns[static_cast<size_t>(columnIndex)].slots;

    if (slotIndex < 0 || slotIndex + 1 >= static_cast<int>(slots.size()))
        return;

    const double low = minDockHeight - resizeStartA;
    const double high = juce::jmax(low, resizeStartB - minDockHeight);

    const double d = juce::jlimit(low, high, static_cast<double>(delta));

    slots[static_cast<size_t>(slotIndex)].size = resizeStartA + d;
    slots[static_cast<size_t>(slotIndex + 1)].size = resizeStartB - d;

    layoutDocks();
}


//==============================================================================
// Tab dragging
//==============================================================================

juce::Rectangle<int> Workspace::getPanelBoundsInWorkspace(juce::Component* panel) const
{
    if (panel == nullptr)
        return {};

    const auto position = getLocalPoint(panel, juce::Point<int>(0, 0));

    return { position.x, position.y, panel->getWidth(), panel->getHeight() };
}


Workspace::DropTarget Workspace::computeDropTarget(juce::Point<int> position) const
{
    DropTarget target;

    const auto content = getLocalBounds().withTrimmedTop(toolbarHeight);

    if (!content.contains(position))
        return target;

    // Empty workspace: dropping anywhere creates the first dock.
    if (columns.empty())
    {
        target.valid = true;
        target.side = DropSide::center;
        target.highlight = content;
        return target;
    }

    // Far left / far right of the workspace: new column.
    if (position.x < content.getX() + workspaceEdgeZone)
    {
        target.valid = true;
        target.side = DropSide::left;
        target.highlight = content.withWidth(newColumnWidth);
        return target;
    }

    if (position.x >= content.getRight() - workspaceEdgeZone)
    {
        target.valid = true;
        target.side = DropSide::right;
        target.highlight = content.withLeft(content.getRight() - newColumnWidth);
        return target;
    }

    auto* area = getDockAreaAt(position);

    if (area == nullptr)
        return target;   // over a splitter

    const bool isSource = area == sourceDockArea;

    // A dock holding only the dragged tab can't be dropped onto itself.
    if (isSource && area->getPanelCount() <= 1)
        return target;

    const auto bounds = area->getBounds();

    const float fx = static_cast<float>(position.x - bounds.getX())
                     / static_cast<float>(juce::jmax(1, bounds.getWidth()));

    const float fy = static_cast<float>(position.y - bounds.getY())
                     / static_cast<float>(juce::jmax(1, bounds.getHeight()));

    const float toLeft = fx;
    const float toRight = 1.0f - fx;
    const float toTop = fy;
    const float toBottom = 1.0f - fy;

    const float nearest = juce::jmin(juce::jmin(toLeft, toRight),
                                     juce::jmin(toTop, toBottom));

    target.anchor = area;

    auto zone = bounds;

    if (nearest > dropEdgeFraction)
    {
        if (isSource)
            return target;   // dropping into its own tab strip: nothing to do

        target.valid = true;
        target.side = DropSide::center;
        target.highlight = bounds;
        return target;
    }

    target.valid = true;

    if (nearest == toLeft)
    {
        target.side = DropSide::left;
        target.highlight = zone.removeFromLeft(bounds.getWidth() / 2);
    }
    else if (nearest == toRight)
    {
        target.side = DropSide::right;
        target.highlight = zone.removeFromRight(bounds.getWidth() / 2);
    }
    else if (nearest == toTop)
    {
        target.side = DropSide::top;
        target.highlight = zone.removeFromTop(bounds.getHeight() / 2);
    }
    else
    {
        target.side = DropSide::bottom;
        target.highlight = zone.removeFromBottom(bounds.getHeight() / 2);
    }

    return target;
}


void Workspace::beginPanelTabDrag(juce::Component* panel, juce::Point<int> screenPosition)
{
    auto* instance = findInstance(panel);

    if (instance == nullptr)
        return;

    sourceDockArea = findDockAreaFor(panel);

    if (sourceDockArea == nullptr)
        return;

    draggedPanelId = instance->id;
    draggingTab = true;
    dropTarget = {};

    // Half-size snapshot keeps the drag cheap, even for heavy panels.
    auto snapshot = panel->createComponentSnapshot(
        panel->getLocalBounds(), true, 0.5f);

    if (snapshot.isValid())
    {
        const auto position = getLocalPoint(nullptr, screenPosition);

        dockPreview.show(
            snapshot,
            { position.x + 14,
              position.y + 14,
              juce::jmax(60, panel->getWidth() / 2),
              juce::jmax(40, panel->getHeight() / 2) });
    }

    updatePanelTabDrag(screenPosition);
}


void Workspace::updatePanelTabDrag(juce::Point<int> screenPosition)
{
    if (!draggingTab)
        return;

    const auto position = getLocalPoint(nullptr, screenPosition);

    auto previewBounds = dockPreview.getBounds();
    previewBounds.setPosition(position.x + 14, position.y + 14);
    dockPreview.moveTowards(previewBounds);

    dropTarget = computeDropTarget(position);

    if (dropTarget.valid)
        dropOverlay.showZone(dropTarget.highlight);
    else
        dropOverlay.hideZone();

    dockPreview.toFront(false);
}


void Workspace::finishPanelTabDrag(juce::Point<int> screenPosition)
{
    if (!draggingTab)
        return;

    updatePanelTabDrag(screenPosition);

    const int panelId = draggedPanelId;
    auto* source = sourceDockArea;
    const auto target = dropTarget;

    draggingTab = false;
    draggedPanelId = -1;
    sourceDockArea = nullptr;
    dropTarget = {};

    dropOverlay.hideZone();

    if (!target.valid)
    {
        dockPreview.hide();
        return;
    }

    // Deferred: we are inside the dragged tab's mouseUp, and moving the panel
    // destroys that tab.
    juce::Component::SafePointer<Workspace> safeThis(this);

    juce::MessageManager::callAsync([safeThis, panelId, source, target]
    {
        if (safeThis != nullptr)
            safeThis->applyPanelDrop(panelId, source, target);
    });
}


void Workspace::applyPanelDrop(int panelId, DockArea* source, DropTarget target)
{
    auto* instance = findInstanceById(panelId);

    if (instance == nullptr || !isDockAlive(source))
    {
        dockPreview.hide();
        return;
    }

    auto* panel = instance->component.get();

    if (!source->containsPanel(panel))
    {
        dockPreview.hide();
        return;
    }

    DockArea* destination = nullptr;

    if (target.side == DropSide::center && target.anchor != nullptr)
    {
        // Drop onto the middle of a dock: add as a tab.
        if (isDockAlive(target.anchor))
            destination = target.anchor;
    }
    else if (target.anchor == nullptr || isDockAlive(target.anchor))
    {
        // Drop on an edge: create a new dock there.
        destination = createDock(target.anchor, target.side);
    }

    if (destination == nullptr || destination == source)
    {
        dockPreview.hide();
        return;
    }

    source->removePanel(panel);
    destination->addPanel(*panel, instance->name);

    // The source dock vanishes once its last tab has left.
    if (source->getPanelCount() == 0)
        removeDock(source);

    layoutDocks();

    dockPreview.animateTo(getPanelBoundsInWorkspace(panel));
}
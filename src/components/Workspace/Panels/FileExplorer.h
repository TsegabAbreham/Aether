#pragma once

#include <JuceHeader.h>
#include <algorithm>
#include <functional>
#include <memory>
#include <vector>

#include "Panel.h"

//==============================================================================
namespace ExplorerTheme
{
    const juce::Colour bar     { 0xff14161a };
    const juce::Colour panel   { 0xff1b1d23 };
    const juce::Colour border  { 0xff2b2e36 };
    const juce::Colour text    { 0xffd5d8de };
    const juce::Colour textDim { 0xff6b7080 };
    const juce::Colour accent  { 0xff3ddcff };

    // file-type colours
    const juce::Colour folder  { 0xffd9a95b };
    const juce::Colour audio   { 0xff3ddcff };
    const juce::Colour model   { 0xffff9f4a };
    const juce::Colour code    { 0xff6cb6ff };
    const juce::Colour image   { 0xffa78bfa };
    const juce::Colour data    { 0xfff2c94c };
    const juce::Colour other   { 0xff8b92a5 };
}

//==============================================================================
// Asset-browser style file explorer:
//   [ header buttons ]
//   [ Project > Folder > Subfolder ]   <- clickable breadcrumbs
//   [ grid of big icons            ]   <- double-click a folder to enter it
//   [ item count        size slider]
//==============================================================================
class FileExplorer : public Panel
{
public:
    std::function<void(juce::File)> onFileSelected;      // single select (files and folders)
    std::function<void(juce::File)> onFileOpened;        // double-click / Enter on a file
    std::function<void(juce::File)> onFolderChanged;     // user picked a new ROOT folder
    std::function<void(juce::File)> onDirectoryChanged;  // user navigated to another folder inside the root

    //==========================================================================
    explicit FileExplorer(juce::File initialFolder = {})
        : Panel("File Explorer")
    {
        viewport.setViewedComponent(&grid, false);
        viewport.setScrollBarsShown(true, false);
        viewport.setScrollBarThickness(10);
        viewport.getVerticalScrollBar().setColour(juce::ScrollBar::thumbColourId,
                                                  juce::Colour(0xff3a3e4a));
        addChildComponent(viewport);
        addChildComponent(breadcrumbs);

        addAndMakeVisible(openFolderButton);
        addChildComponent(upButton);
        addChildComponent(newFileButton);
        addChildComponent(newFolderButton);
        addChildComponent(refreshButton);

        openFolderButton.onClick = [this] { chooseFolder(); };
        upButton.onClick         = [this] { goUp(); };
        newFileButton.onClick    = [this] { createFile(); };
        newFolderButton.onClick  = [this] { createFolder(); };
        refreshButton.onClick    = [this] { refreshItems(); };

        iconSlider.setRange(56.0, 144.0, 1.0);
        iconSlider.setValue((double) iconSize, juce::dontSendNotification);
        iconSlider.setColour(juce::Slider::thumbColourId, ExplorerTheme::accent);
        iconSlider.setColour(juce::Slider::trackColourId, ExplorerTheme::accent.withAlpha(0.35f));
        iconSlider.setColour(juce::Slider::backgroundColourId, ExplorerTheme::border);
        iconSlider.setTooltip("Icon size (Ctrl + mouse wheel)");
        iconSlider.onValueChange = [this]
        {
            iconSize = (int) iconSlider.getValue();
            grid.relayout();
            grid.repaint();
        };
        addChildComponent(iconSlider);

        emptyOpenButton.setButtonText("Open Folder");
        emptyOpenButton.setColour(juce::TextButton::buttonColourId, ExplorerTheme::accent.withAlpha(0.18f));
        emptyOpenButton.setColour(juce::TextButton::textColourOffId, ExplorerTheme::accent);
        emptyOpenButton.onClick = [this] { chooseFolder(); };
        addChildComponent(emptyOpenButton);

        if (initialFolder.isDirectory())
            setRootFolder(initialFolder);
        else
            updateLayoutState();
    }

    ~FileExplorer() override
    {
        viewport.setViewedComponent(nullptr, false);
    }

    //==========================================================================
    // Sets the project root. Navigation is limited to this folder and below.
    // Call this from your menu's "Open Folder" action as well.
    void setRootFolder(juce::File folder)
    {
        if (! folder.isDirectory())
            return;

        rootFolder    = folder;
        currentFolder = folder;
        selectedFile  = juce::File();

        refreshItems();
        viewport.setViewPosition(0, 0);
        updateLayoutState();
    }

    // The project root (what the user picked with "Open Folder")
    juce::File getCurrentFolder() const { return rootFolder; }

    // The folder currently being displayed in the grid
    juce::File getBrowsedFolder() const { return currentFolder; }

    juce::File getSelectedFile() const  { return selectedFile; }

    // Navigate to a folder inside the root
    void navigateTo(const juce::File& dir)
    {
        if (! dir.isDirectory() || ! isInsideRoot(dir))
            return;

        currentFolder = dir;
        selectedFile  = juce::File();

        refreshItems();
        viewport.setViewPosition(0, 0);

        if (onDirectoryChanged)
            onDirectoryChanged(currentFolder);
    }

    void goUp()
    {
        if (currentFolder != rootFolder)
            navigateTo(currentFolder.getParentDirectory());
    }

    //==========================================================================
    void paint(juce::Graphics& g) override
    {
        g.fillAll(ExplorerTheme::panel);

        // ---- header
        auto header = juce::Rectangle<int>(0, 0, getWidth(), headerHeight);
        g.setColour(ExplorerTheme::bar);
        g.fillRect(header);
        g.setColour(ExplorerTheme::border);
        g.fillRect(0, headerHeight - 1, getWidth(), 1);

        g.setColour(ExplorerTheme::textDim);
        g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
        g.drawText("EXPLORER",
                   juce::Rectangle<int>(12, 0, juce::jmax(0, titleMaxRight - 12), headerHeight),
                   juce::Justification::centredLeft, true);

        if (rootFolder.isDirectory())
        {
            // ---- footer
            auto footer = juce::Rectangle<int>(0, getHeight() - footerHeight, getWidth(), footerHeight);
            g.setColour(ExplorerTheme::bar);
            g.fillRect(footer);
            g.setColour(ExplorerTheme::border);
            g.fillRect(footer.getX(), footer.getY(), footer.getWidth(), 1);

            juce::String info = juce::String((int) items.size()) + (items.size() == 1 ? " item" : " items");

            if (selectedFile != juce::File())
                info << "   |   " << selectedFile.getFileName();

            g.setColour(ExplorerTheme::textDim);
            g.setFont(juce::Font(juce::FontOptions(11.5f)));
            g.drawText(info,
                       footer.withTrimmedLeft(12).withTrimmedRight(140),
                       juce::Justification::centredLeft, true);
        }
        else
        {
            // ---- empty state
            const int y = emptyOpenButton.getY();
            g.setColour(ExplorerTheme::text);
            g.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
            g.drawText("No folder opened",
                       juce::Rectangle<int>(0, y - 48, getWidth(), 20),
                       juce::Justification::centred);

            g.setColour(ExplorerTheme::textDim);
            g.setFont(juce::Font(juce::FontOptions(12.0f)));
            g.drawText("Open a project folder to browse its files.",
                       juce::Rectangle<int>(8, y - 26, getWidth() - 16, 18),
                       juce::Justification::centred, true);
        }
    }

    void resized() override
    {
        auto area   = getLocalBounds();
        auto header = area.removeFromTop(headerHeight);
        header.removeFromRight(6);

        for (auto* b : { &refreshButton, &newFolderButton, &newFileButton,
                         &upButton, &openFolderButton })
            b->setBounds(header.removeFromRight(26).withSizeKeepingCentre(24, 24));

        titleMaxRight = header.getRight();

        if (rootFolder.isDirectory())
        {
            breadcrumbs.setBounds(area.removeFromTop(crumbHeight));

            auto footer = area.removeFromBottom(footerHeight);
            iconSlider.setBounds(footer.removeFromRight(130).reduced(8, 4));
        }

        viewport.setBounds(area);
        grid.relayout();

        emptyOpenButton.setBounds(juce::Rectangle<int>(150, 30).withCentre(area.getCentre()));
    }

private:
    //==========================================================================
    // Small vector icon button for the header
    //==========================================================================
    class IconButton : public juce::Button
    {
    public:
        enum class Icon { newFile, newFolder, refresh, up, openFolder };

        IconButton(Icon i, const juce::String& tooltip)
            : juce::Button(tooltip), icon(i)
        {
            setTooltip(tooltip);
            setMouseCursor(juce::MouseCursor::PointingHandCursor);
        }

        void paintButton(juce::Graphics& g, bool over, bool down) override
        {
            auto bounds = getLocalBounds().toFloat();

            if (isEnabled() && (over || down))
            {
                g.setColour(juce::Colours::white.withAlpha(down ? 0.12f : 0.07f));
                g.fillRoundedRectangle(bounds.reduced(1.0f), 4.0f);
            }

            if (! isEnabled())
                g.setColour(ExplorerTheme::text.withAlpha(0.25f));
            else
                g.setColour(over ? juce::Colours::white : ExplorerTheme::text.withAlpha(0.75f));

            const auto c = bounds.getCentre();
            const juce::PathStrokeType stroke(1.3f, juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded);

            switch (icon)
            {
                case Icon::newFile:
                {
                    g.drawRoundedRectangle(c.x - 7.0f, c.y - 7.0f, 8.0f, 11.0f, 1.5f, 1.2f);
                    g.drawLine(c.x + 4.5f, c.y + 1.5f, c.x + 4.5f, c.y + 7.5f, 1.2f);
                    g.drawLine(c.x + 1.5f, c.y + 4.5f, c.x + 7.5f, c.y + 4.5f, 1.2f);
                    break;
                }

                case Icon::newFolder:
                {
                    g.fillRoundedRectangle(c.x - 8.0f, c.y - 7.0f, 4.0f, 2.5f, 1.0f);
                    g.drawRoundedRectangle(c.x - 8.0f, c.y - 5.0f, 10.0f, 8.0f, 1.5f, 1.2f);
                    g.drawLine(c.x + 5.0f, c.y + 1.5f, c.x + 5.0f, c.y + 7.5f, 1.2f);
                    g.drawLine(c.x + 2.0f, c.y + 4.5f, c.x + 8.0f, c.y + 4.5f, 1.2f);
                    break;
                }

                case Icon::refresh:
                {
                    const float r = 5.5f, start = 0.9f, end = 5.5f;

                    juce::Path arc;
                    arc.addCentredArc(c.x, c.y, r, r, 0.0f, start, end, true);
                    g.strokePath(arc, stroke);

                    const float ex = c.x + r * std::sin(end);
                    const float ey = c.y - r * std::cos(end);
                    const float tx = std::cos(end), ty = std::sin(end);   // tangent
                    const float nx = -ty,           ny = tx;              // normal

                    juce::Path head;
                    head.addTriangle(ex + tx * 3.0f,                 ey + ty * 3.0f,
                                     ex - tx * 0.5f + nx * 3.0f,     ey - ty * 0.5f + ny * 3.0f,
                                     ex - tx * 0.5f - nx * 3.0f,     ey - ty * 0.5f - ny * 3.0f);
                    g.fillPath(head);
                    break;
                }

                case Icon::up:
                {
                    juce::Path arrow;
                    arrow.startNewSubPath(c.x - 4.5f, c.y - 1.0f);
                    arrow.lineTo(c.x, c.y - 5.5f);
                    arrow.lineTo(c.x + 4.5f, c.y - 1.0f);
                    g.strokePath(arrow, stroke);
                    g.drawLine(c.x, c.y - 5.5f, c.x, c.y + 6.0f, 1.3f);
                    break;
                }

                case Icon::openFolder:
                {
                    g.fillRoundedRectangle(c.x - 7.0f, c.y - 6.0f, 5.0f, 2.5f, 1.0f);
                    g.drawRoundedRectangle(c.x - 7.0f, c.y - 4.0f, 14.0f, 10.0f, 1.8f, 1.2f);
                    break;
                }
            }
        }

    private:
        Icon icon;
    };

    //==========================================================================
    // Data
    //==========================================================================
    enum class Kind { folder, audio, model, code, image, data, other };

    struct Item
    {
        juce::File file;
        bool       isDir;
        Kind       kind;
    };

    static constexpr int headerHeight = 34;
    static constexpr int crumbHeight  = 30;
    static constexpr int footerHeight = 28;
    static constexpr int gridPad      = 12;

    //==========================================================================
    // Breadcrumb bar:  Project > Folder > Subfolder
    //==========================================================================
    class BreadcrumbBar : public juce::Component
    {
    public:
        explicit BreadcrumbBar(FileExplorer& o) : owner(o) {}

        void rebuild()
        {
            crumbs.clear();
            hovered = -1;

            const auto root = owner.rootFolder;
            const auto cur  = owner.currentFolder;

            if (! root.isDirectory())
            {
                repaint();
                return;
            }

            std::vector<juce::File> chain;

            for (auto f = cur;; f = f.getParentDirectory())
            {
                chain.push_back(f);

                if (f == root || f.getParentDirectory() == f)
                    break;
            }

            std::reverse(chain.begin(), chain.end());

            const juce::Font normalFont(juce::FontOptions(12.5f));
            const juce::Font boldFont(juce::FontOptions(12.5f, juce::Font::bold));

            const int padX = 8, sepW = 16, ellW = 26, margin = 10;
            const int n = (int) chain.size();

            auto nameOf = [](const juce::File& f)
            {
                auto s = f.getFileName();
                return s.isEmpty() ? f.getFullPathName() : s;
            };

            std::vector<int> widths;
            for (int i = 0; i < n; ++i)
                widths.push_back(juce::GlyphArrangement::getStringWidthInt(i == n - 1 ? boldFont : normalFont,
                                                                           nameOf(chain[(size_t) i])) + padX * 2);

            const int avail = juce::jmax(0, getWidth() - margin * 2);

            int used  = widths.back();
            int first = n - 1;

            for (int i = n - 2; i >= 0; --i)
            {
                const int need    = widths[(size_t) i] + sepW;
                const int reserve = i > 0 ? ellW + sepW : 0;

                if (used + need + reserve > avail)
                    break;

                used += need;
                first = i;
            }

            int x = margin;

            if (first > 0)
            {
                crumbs.push_back({ chain[(size_t) first - 1], "...",
                                   { x, 0, ellW, getHeight() }, true });
                x += ellW + sepW;
            }

            for (int i = first; i < n; ++i)
            {
                const int w = juce::jmin(widths[(size_t) i], juce::jmax(24, getWidth() - x - margin));
                crumbs.push_back({ chain[(size_t) i], nameOf(chain[(size_t) i]),
                                   { x, 0, w, getHeight() }, false });
                x += w + sepW;
            }

            repaint();
        }

        void resized() override { rebuild(); }

        void paint(juce::Graphics& g) override
        {
            g.fillAll(ExplorerTheme::bar.brighter(0.03f));
            g.setColour(ExplorerTheme::border);
            g.fillRect(0, getHeight() - 1, getWidth(), 1);

            const int last = (int) crumbs.size() - 1;

            for (int i = 0; i <= last; ++i)
            {
                const auto& c = crumbs[(size_t) i];
                const bool isLast = (i == last);

                if (i == hovered && ! isLast)
                {
                    g.setColour(juce::Colours::white.withAlpha(0.08f));
                    g.fillRoundedRectangle(c.bounds.toFloat().reduced(0.0f, 5.0f), 4.0f);
                }

                g.setColour(isLast || i == hovered ? ExplorerTheme::text : ExplorerTheme::textDim);
                g.setFont(juce::Font(juce::FontOptions(12.5f, isLast ? juce::Font::bold : juce::Font::plain)));
                g.drawText(c.text, c.bounds.reduced(8, 0), juce::Justification::centredLeft, true);

                if (! isLast)
                    FileExplorer::drawChevron(g, (float) c.bounds.getRight() + 8.0f,
                                              (float) getHeight() * 0.5f, false,
                                              ExplorerTheme::textDim.withAlpha(0.7f));
            }
        }

        void mouseMove(const juce::MouseEvent& e) override
        {
            const int idx = crumbAt(e.getPosition());

            if (idx != hovered)
            {
                hovered = idx;
                setMouseCursor(hovered >= 0 && hovered < (int) crumbs.size() - 1
                                   ? juce::MouseCursor::PointingHandCursor
                                   : juce::MouseCursor::NormalCursor);
                repaint();
            }
        }

        void mouseExit(const juce::MouseEvent&) override
        {
            hovered = -1;
            repaint();
        }

        void mouseDown(const juce::MouseEvent& e) override
        {
            const int idx = crumbAt(e.getPosition());

            if (idx < 0 || idx >= (int) crumbs.size() - 1)
                return;

            const auto target = crumbs[(size_t) idx].file;
            owner.navigateTo(target);
        }

    private:
        struct Crumb
        {
            juce::File file;
            juce::String text;
            juce::Rectangle<int> bounds;
            bool isEllipsis;
        };

        int crumbAt(juce::Point<int> p) const
        {
            for (size_t i = 0; i < crumbs.size(); ++i)
                if (crumbs[i].bounds.contains(p))
                    return (int) i;

            return -1;
        }

        FileExplorer& owner;
        std::vector<Crumb> crumbs;
        int hovered = -1;
    };

    //==========================================================================
    // The scrolling grid of big icons
    //==========================================================================
    class Grid : public juce::Component
    {
    public:
        explicit Grid(FileExplorer& o) : owner(o)
        {
            setWantsKeyboardFocus(true);
        }

        int columns      = 1;
        int hoveredIndex = -1;

        int cellW() const { return owner.iconSize + 28; }
        int cellH() const { return owner.iconSize + 58; }

        void relayout()
        {
            const int vw = juce::jmax(1, owner.viewport.getWidth() - owner.viewport.getScrollBarThickness());

            columns = juce::jmax(1, (vw - gridPad * 2) / cellW());

            const int count    = (int) owner.items.size();
            const int rowCount = (count + columns - 1) / columns;
            const int contentH = gridPad * 2 + rowCount * cellH();

            setSize(vw, juce::jmax(contentH, owner.viewport.getHeight()));
        }

        juce::Rectangle<int> tileBounds(int index) const
        {
            const int col = index % columns;
            const int row = index / columns;
            return { gridPad + col * cellW(), gridPad + row * cellH(), cellW(), cellH() };
        }

        int indexAt(juce::Point<int> p) const
        {
            if (p.x < gridPad || p.y < gridPad)
                return -1;

            const int col = (p.x - gridPad) / cellW();
            const int row = (p.y - gridPad) / cellH();

            if (col >= columns)
                return -1;

            const int idx = row * columns + col;
            return juce::isPositiveAndBelow(idx, (int) owner.items.size()) ? idx : -1;
        }

        void setHovered(int idx)
        {
            if (idx != hoveredIndex)
            {
                hoveredIndex = idx;
                repaint();
            }
        }

        //======================================================================
        void paint(juce::Graphics& g) override
        {
            g.fillAll(ExplorerTheme::panel);

            const int count = (int) owner.items.size();

            if (count == 0)
            {
                auto r = getParentComponent() != nullptr ? getParentComponent()->getLocalBounds() : getLocalBounds();
                r = r.withHeight(juce::jmin(r.getHeight(), getHeight()));

                g.setColour(ExplorerTheme::textDim);
                g.setFont(juce::Font(juce::FontOptions(13.0f)));
                g.drawText("This folder is empty", r.withTrimmedBottom(10), juce::Justification::centred);

                g.setFont(juce::Font(juce::FontOptions(11.5f)));
                g.setColour(ExplorerTheme::textDim.withAlpha(0.7f));
                g.drawText("Right-click to create a file or folder",
                           r.withTrimmedTop(36), juce::Justification::centredTop);
                return;
            }

            const auto clip = g.getClipBounds();
            const int firstRow = juce::jmax(0, (clip.getY() - gridPad) / cellH());
            const int lastRow  = (clip.getBottom() - gridPad) / cellH();

            for (int row = firstRow; row <= lastRow; ++row)
            {
                for (int col = 0; col < columns; ++col)
                {
                    const int idx = row * columns + col;

                    if (idx >= count)
                        break;

                    paintTile(g, idx);
                }
            }
        }

        void paintTile(juce::Graphics& g, int idx)
        {
            const auto& it = owner.items[(size_t) idx];
            const auto inner = tileBounds(idx).reduced(3).toFloat();

            const bool selected = (it.file == owner.selectedFile);
            const bool hovered  = (idx == hoveredIndex);

            if (selected)
            {
                g.setColour(ExplorerTheme::accent.withAlpha(0.16f));
                g.fillRoundedRectangle(inner, 6.0f);
                g.setColour(ExplorerTheme::accent.withAlpha(0.7f));
                g.drawRoundedRectangle(inner.reduced(0.5f), 6.0f, 1.2f);
            }
            else if (hovered)
            {
                g.setColour(juce::Colours::white.withAlpha(0.06f));
                g.fillRoundedRectangle(inner, 6.0f);
            }

            const float s = (float) owner.iconSize;
            auto iconRect = juce::Rectangle<float>(s, s).withCentre({ inner.getCentreX(),
                                                                      inner.getY() + 8.0f + s * 0.5f });

            auto ext = it.file.getFileExtension().trimCharactersAtStart(".").toUpperCase().substring(0, 4);
            FileExplorer::drawTileIcon(g, iconRect, it.kind, ext);

            auto labelRect = juce::Rectangle<float>(inner.getX() + 2.0f, inner.getY() + 8.0f + s + 4.0f,
                                                    inner.getWidth() - 4.0f, 32.0f).toNearestInt();

            g.setColour(selected ? juce::Colours::white : ExplorerTheme::text);
            g.setFont(juce::Font(juce::FontOptions(12.0f)));
            g.drawFittedText(it.file.getFileName(), labelRect, juce::Justification::centredTop, 2, 1.0f);
        }

        //======================================================================
        void mouseMove(const juce::MouseEvent& e) override { setHovered(indexAt(e.getPosition())); }
        void mouseExit(const juce::MouseEvent&) override   { setHovered(-1); }

        void mouseDown(const juce::MouseEvent& e) override
        {
            grabKeyboardFocus();

            const int idx = indexAt(e.getPosition());

            if (idx < 0)
            {
                owner.clearSelection();

                if (e.mods.isPopupMenu())
                    owner.showContextMenu();

                return;
            }

            owner.selectIndex(idx, false);

            if (e.mods.isPopupMenu())
                owner.showContextMenu();
        }

        void mouseDoubleClick(const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu())
                return;

            const int idx = indexAt(e.getPosition());

            if (idx >= 0)
                owner.activateIndex(idx);
        }

        void mouseDrag(const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu() || e.getDistanceFromDragStart() < 8)
                return;

            const int idx = indexAt(e.getMouseDownPosition());

            if (idx >= 0)
                owner.beginFileDrag(idx);
        }

        void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
        {
            if (e.mods.isCtrlDown() || e.mods.isCommandDown())
                owner.iconSlider.setValue(owner.iconSlider.getValue() + (double) w.deltaY * 40.0);
            else
                juce::Component::mouseWheelMove(e, w);
        }

        bool keyPressed(const juce::KeyPress& key) override
        {
            const int n = (int) owner.items.size();

            if (key == juce::KeyPress::F2Key)        { owner.renameSelected(); return true; }
            if (key == juce::KeyPress::deleteKey)    { owner.deleteSelected();  return true; }
            if (key == juce::KeyPress::backspaceKey) { owner.goUp();            return true; }

            if (n == 0)
                return false;

            const int sel = owner.indexOf(owner.selectedFile);

            if (key == juce::KeyPress::returnKey)
            {
                if (sel >= 0)
                    owner.activateIndex(sel);

                return true;
            }

            int target = sel;

            if      (key == juce::KeyPress::leftKey)  target = sel < 0 ? 0 : sel - 1;
            else if (key == juce::KeyPress::rightKey) target = sel < 0 ? 0 : sel + 1;
            else if (key == juce::KeyPress::upKey)    target = sel < 0 ? 0 : sel - columns;
            else if (key == juce::KeyPress::downKey)  target = sel < 0 ? 0 : (sel + columns < n ? sel + columns : sel);
            else if (key == juce::KeyPress::homeKey)  target = 0;
            else if (key == juce::KeyPress::endKey)   target = n - 1;
            else                                      return false;

            owner.selectIndex(juce::jlimit(0, n - 1, target), true);
            return true;
        }

    private:
        FileExplorer& owner;
    };

    //==========================================================================
    // Members
    //==========================================================================
    juce::Viewport viewport;
    Grid           grid        { *this };
    BreadcrumbBar  breadcrumbs { *this };

    IconButton openFolderButton { IconButton::Icon::openFolder, "Open Folder" };
    IconButton upButton         { IconButton::Icon::up,         "Up One Level" };
    IconButton newFileButton    { IconButton::Icon::newFile,    "New File" };
    IconButton newFolderButton  { IconButton::Icon::newFolder,  "New Folder" };
    IconButton refreshButton    { IconButton::Icon::refresh,    "Refresh" };
    juce::TextButton emptyOpenButton;

    juce::Slider iconSlider { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };

    std::unique_ptr<juce::FileChooser> chooser;

    std::vector<Item> items;

    juce::File rootFolder;      // project root picked with "Open Folder"
    juce::File currentFolder;   // folder being shown in the grid
    juce::File selectedFile;

    int iconSize      = 84;
    int titleMaxRight = 100;

    //==========================================================================
    // Layout / state
    //==========================================================================
    void updateLayoutState()
    {
        const bool hasFolder = rootFolder.isDirectory();

        viewport.setVisible(hasFolder);
        breadcrumbs.setVisible(hasFolder);
        iconSlider.setVisible(hasFolder);
        emptyOpenButton.setVisible(! hasFolder);
        upButton.setVisible(hasFolder);
        newFileButton.setVisible(hasFolder);
        newFolderButton.setVisible(hasFolder);
        refreshButton.setVisible(hasFolder);

        resized();
        repaint();
    }

    bool isInsideRoot(const juce::File& f) const
    {
        return rootFolder.isDirectory() && (f == rootFolder || f.isAChildOf(rootFolder));
    }

    void chooseFolder()
    {
        chooser = std::make_unique<juce::FileChooser>(
            "Select Project Folder",
            rootFolder.isDirectory()
                ? rootFolder
                : juce::File::getSpecialLocation(juce::File::userHomeDirectory));

        juce::Component::SafePointer<FileExplorer> safe(this);

        chooser->launchAsync(
            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
            [safe](const juce::FileChooser& fc)
            {
                if (safe == nullptr)
                    return;

                auto folder = fc.getResult();

                if (folder.isDirectory())
                {
                    safe->setRootFolder(folder);

                    if (safe->onFolderChanged)
                        safe->onFolderChanged(folder);
                }
            });
    }

    //==========================================================================
    // Items
    //==========================================================================
    void refreshItems()
    {
        // if the browsed folder vanished, climb up to the nearest one that exists
        while (currentFolder != rootFolder && ! currentFolder.isDirectory())
            currentFolder = currentFolder.getParentDirectory();

        items.clear();
        grid.hoveredIndex = -1;

        if (currentFolder.isDirectory())
        {
            auto children = currentFolder.findChildFiles(
                juce::File::findFilesAndDirectories | juce::File::ignoreHiddenFiles, false);

            items.reserve((size_t) children.size());

            for (const auto& f : children)
            {
                const bool dir = f.isDirectory();
                items.push_back({ f, dir, kindFor(f, dir) });
            }

            // folders first, then alphabetical
            std::sort(items.begin(), items.end(), [](const Item& a, const Item& b)
            {
                if (a.isDir != b.isDir)
                    return a.isDir;

                return a.file.getFileName().compareIgnoreCase(b.file.getFileName()) < 0;
            });
        }

        if (indexOf(selectedFile) < 0)
            selectedFile = juce::File();

        upButton.setEnabled(currentFolder != rootFolder);

        grid.relayout();
        grid.repaint();
        breadcrumbs.rebuild();
        repaint();
    }

    int indexOf(const juce::File& f) const
    {
        if (f == juce::File())
            return -1;

        for (size_t i = 0; i < items.size(); ++i)
            if (items[i].file == f)
                return (int) i;

        return -1;
    }

    void scrollToIndex(int index)
    {
        if (! juce::isPositiveAndBelow(index, (int) items.size()))
            return;

        const auto r    = grid.tileBounds(index);
        const auto view = viewport.getViewArea();

        if (r.getY() < view.getY())
            viewport.setViewPosition(view.getX(), juce::jmax(0, r.getY() - gridPad));
        else if (r.getBottom() > view.getBottom())
            viewport.setViewPosition(view.getX(), r.getBottom() + gridPad - viewport.getViewHeight());
    }

    void selectIndex(int index, bool scroll)
    {
        if (! juce::isPositiveAndBelow(index, (int) items.size()))
            return;

        selectedFile = items[(size_t) index].file;

        if (scroll)
            scrollToIndex(index);

        grid.repaint();
        repaint();

        if (onFileSelected)
            onFileSelected(selectedFile);
    }

    void clearSelection()
    {
        selectedFile = juce::File();
        grid.repaint();
        repaint();
    }

    // double-click / Enter: enter folders, open files
    void activateIndex(int index)
    {
        if (! juce::isPositiveAndBelow(index, (int) items.size()))
            return;

        const auto file  = items[(size_t) index].file;
        const bool isDir = items[(size_t) index].isDir;

        if (isDir)
            navigateTo(file);
        else if (onFileOpened)
            onFileOpened(file);
    }

    // drag a file out of the grid (to the Timeline, or to the OS if there is no drag container)
    void beginFileDrag(int index)
    {
        if (! juce::isPositiveAndBelow(index, (int) items.size()))
            return;

        const auto& it = items[(size_t) index];

        if (it.isDir)
            return;

        auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this);

        if (container == nullptr)
        {
            juce::DragAndDropContainer::performExternalDragDropOfFiles(
                juce::StringArray { it.file.getFullPathName() }, false, this);
            return;
        }

        if (container->isDragAndDropActive())
            return;

        auto img = grid.createComponentSnapshot(grid.tileBounds(index).reduced(3), true, 1.0f);
        img.multiplyAllAlphas(0.75f);

        container->startDragging(it.file.getFullPathName(), &grid, juce::ScaledImage(img), false);
    }

    //==========================================================================
    // Context menu
    //==========================================================================
    void showContextMenu()
    {
        const bool hasSelection = selectedFile.exists();

        juce::PopupMenu m;
        m.addItem(7, "Open", hasSelection);
        m.addSeparator();
        m.addItem(1, "New File");
        m.addItem(2, "New Folder");
        m.addSeparator();
        m.addItem(3, "Rename", hasSelection);
        m.addItem(4, "Delete", hasSelection);
        m.addSeparator();
        m.addItem(5, "Reveal in File Explorer");
        m.addItem(6, "Refresh");

        juce::Component::SafePointer<FileExplorer> safe(this);

        m.showMenuAsync(juce::PopupMenu::Options(), [safe](int result)
        {
            if (safe == nullptr)
                return;

            switch (result)
            {
                case 1: safe->createFile();     break;
                case 2: safe->createFolder();   break;
                case 3: safe->renameSelected(); break;
                case 4: safe->deleteSelected(); break;
                case 5: (safe->selectedFile.exists() ? safe->selectedFile : safe->currentFolder).revealToUser(); break;
                case 6: safe->refreshItems();   break;
                case 7: safe->activateIndex(safe->indexOf(safe->selectedFile)); break;
                default: break;
            }
        });
    }

    //==========================================================================
    // File operations
    //==========================================================================
    static bool isValidName(const juce::String& name)
    {
        return name.isNotEmpty()
            && name != "." && name != ".."
            && ! name.containsAnyOf("\\/:*?\"<>|");
    }

    void askForName(const juce::String& title,
                    const juce::String& initialValue,
                    std::function<void(juce::String)> callback)
    {
        auto* alert = new juce::AlertWindow(title, "Enter a name:",
                                            juce::MessageBoxIconType::NoIcon);

        alert->addTextEditor("name", initialValue, "Name:");

        if (auto* ed = alert->getTextEditor("name"))
            ed->selectAll();

        alert->addButton("Confirm", 1, juce::KeyPress(juce::KeyPress::returnKey));
        alert->addButton("Cancel",  0, juce::KeyPress(juce::KeyPress::escapeKey));

        juce::Component::SafePointer<juce::AlertWindow> safeAlert(alert);

        alert->enterModalState(
            true,
            juce::ModalCallbackFunction::create(
                [safeAlert, callback](int result)
                {
                    if (safeAlert == nullptr || result != 1)
                        return;

                    callback(safeAlert->getTextEditorContents("name").trim());
                }),
            true);
    }

    // new items are always created in the folder currently shown
    void selectAndReveal(const juce::File& f)
    {
        selectedFile = f;
        refreshItems();
        scrollToIndex(indexOf(f));
    }

    void createFolder()
    {
        const auto target = currentFolder;

        if (! target.isDirectory())
            return;

        juce::Component::SafePointer<FileExplorer> safe(this);

        askForName("New Folder", "", [safe, target](juce::String name)
        {
            if (safe == nullptr || ! isValidName(name))
                return;

            auto folder = target.getChildFile(name);

            if (folder.exists())
                return;

            if (folder.createDirectory().wasOk() && safe->currentFolder == target)
                safe->selectAndReveal(folder);
        });
    }

    void createFile()
    {
        const auto target = currentFolder;

        if (! target.isDirectory())
            return;

        juce::Component::SafePointer<FileExplorer> safe(this);

        askForName("New File", "", [safe, target](juce::String name)
        {
            if (safe == nullptr || ! isValidName(name))
                return;

            auto file = target.getChildFile(name);

            if (file.exists())
                return;

            if (file.create().wasOk() && safe->currentFolder == target)
                safe->selectAndReveal(file);
        });
    }

    void renameSelected()
    {
        if (! selectedFile.exists())
            return;

        const auto original = selectedFile;

        juce::Component::SafePointer<FileExplorer> safe(this);

        askForName("Rename", original.getFileName(), [safe, original](juce::String name)
        {
            if (safe == nullptr || ! isValidName(name))
                return;

            auto destination = original.getSiblingFile(name);

            if (destination.exists())
                return;

            if (original.moveFileTo(destination))
                safe->selectAndReveal(destination);
        });
    }

    void deleteSelected()
    {
        if (! selectedFile.exists())
            return;

        const auto fileToDelete = selectedFile;

        auto* alert = new juce::AlertWindow(
            "Delete",
            "Move \"" + fileToDelete.getFileName() + "\" to the Recycle Bin?",
            juce::MessageBoxIconType::WarningIcon);

        alert->addButton("Delete", 1, juce::KeyPress(juce::KeyPress::returnKey));
        alert->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

        juce::Component::SafePointer<FileExplorer> safeThis(this);

        alert->enterModalState(
            true,
            juce::ModalCallbackFunction::create(
                [safeThis, fileToDelete](int result)
                {
                    if (safeThis == nullptr || result != 1)
                        return;

                    if (fileToDelete.moveToTrash())
                    {
                        safeThis->selectedFile = juce::File();
                        safeThis->refreshItems();
                    }
                }),
            true);
    }

    //==========================================================================
    // Drawing helpers
    //==========================================================================
    static Kind kindFor(const juce::File& f, bool isDir)
    {
        if (isDir)
            return Kind::folder;

        static const juce::StringArray audio { "wav", "aif", "aiff", "mp3", "flac", "ogg", "m4a", "wma" };
        static const juce::StringArray model { "fbx", "obj", "gltf", "glb", "blend", "dae", "stl", "usd", "usdz", "3ds" };
        static const juce::StringArray code  { "cpp", "h", "hpp", "c", "cc", "cs", "py", "js", "ts", "cmake", "lua", "glsl", "hlsl", "frag", "vert" };
        static const juce::StringArray image { "png", "jpg", "jpeg", "gif", "bmp", "tga", "exr", "hdr", "tif", "tiff", "svg", "webp" };
        static const juce::StringArray data  { "json", "xml", "yaml", "yml", "ini", "txt", "md", "csv" };

        const auto ext = f.getFileExtension().trimCharactersAtStart(".").toLowerCase();

        if (audio.contains(ext)) return Kind::audio;
        if (model.contains(ext)) return Kind::model;
        if (code.contains(ext))  return Kind::code;
        if (image.contains(ext)) return Kind::image;
        if (data.contains(ext))  return Kind::data;

        return Kind::other;
    }

    static juce::Colour colourFor(Kind kind)
    {
        switch (kind)
        {
            case Kind::folder: return ExplorerTheme::folder;
            case Kind::audio:  return ExplorerTheme::audio;
            case Kind::model:  return ExplorerTheme::model;
            case Kind::code:   return ExplorerTheme::code;
            case Kind::image:  return ExplorerTheme::image;
            case Kind::data:   return ExplorerTheme::data;
            case Kind::other:  break;
        }

        return ExplorerTheme::other;
    }

    static void drawChevron(juce::Graphics& g, float cx, float cy, bool open, juce::Colour c)
    {
        juce::Path p;

        if (open)
        {
            p.startNewSubPath(cx - 3.5f, cy - 1.8f);
            p.lineTo(cx, cy + 2.0f);
            p.lineTo(cx + 3.5f, cy - 1.8f);
        }
        else
        {
            p.startNewSubPath(cx - 1.8f, cy - 3.5f);
            p.lineTo(cx + 2.0f, cy);
            p.lineTo(cx - 1.8f, cy + 3.5f);
        }

        g.setColour(c);
        g.strokePath(p, juce::PathStrokeType(1.3f, juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));
    }

    // Large tile icon. Everything scales with the rectangle, so it works at any icon size.
    static void drawTileIcon(juce::Graphics& g, juce::Rectangle<float> r, Kind kind, const juce::String& ext)
    {
        const float s   = juce::jmin(r.getWidth(), r.getHeight());
        const auto  col = colourFor(kind);
        const float cr  = juce::jmax(4.0f, s * 0.07f);

        // ---- folder
        if (kind == Kind::folder)
        {
            auto b = r.reduced(s * 0.04f, s * 0.16f);

            juce::Rectangle<float> tab(b.getX(), b.getY(), b.getWidth() * 0.42f, b.getHeight() * 0.30f);

            g.setColour(col.darker(0.45f));
            g.fillRoundedRectangle(tab, cr);
            g.fillRoundedRectangle(b.withTrimmedTop(b.getHeight() * 0.10f), cr);

            auto front = b.withTrimmedTop(b.getHeight() * 0.24f);
            g.setGradientFill(juce::ColourGradient(col.brighter(0.15f), front.getX(), front.getY(),
                                                   col.darker(0.10f),   front.getX(), front.getBottom(), false));
            g.fillRoundedRectangle(front, cr);
            return;
        }

        // ---- file card
        auto card = juce::Rectangle<float>(s * 0.76f, s * 0.92f).withCentre(r.getCentre());

        g.setColour(ExplorerTheme::panel.brighter(0.12f));
        g.fillRoundedRectangle(card, cr);
        g.setColour(col.withAlpha(0.14f));
        g.fillRoundedRectangle(card, cr);
        g.setColour(col.withAlpha(0.65f));
        g.drawRoundedRectangle(card.reduced(0.5f), cr, 1.2f);

        const float badgeFont = juce::jlimit(8.0f, 15.0f, s * 0.15f);

        if (kind == Kind::other)
        {
            g.setColour(col);
            g.setFont(juce::Font(juce::FontOptions(juce::jlimit(9.0f, 20.0f, s * 0.22f), juce::Font::bold)));
            g.drawText(ext.isEmpty() ? juce::String("FILE") : ext, card.toNearestInt(),
                       juce::Justification::centred, false);
            return;
        }

        auto content   = card;
        auto badgeArea = content.removeFromBottom(content.getHeight() * 0.27f);
        auto glyphArea = content.reduced(card.getWidth() * 0.2f, card.getHeight() * 0.08f);

        const float gs = juce::jmin(glyphArea.getWidth(), glyphArea.getHeight());
        auto sq = juce::Rectangle<float>(gs, gs).withCentre(glyphArea.getCentre());
        const auto c = sq.getCentre();

        const float thick = juce::jmax(1.2f, gs * 0.08f);
        const juce::PathStrokeType stroke(thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

        g.setColour(col);

        switch (kind)
        {
            case Kind::audio:
            {
                static const float prof[] = { 0.30f, 0.60f, 0.95f, 0.50f, 0.78f, 0.40f, 0.25f };
                const int   n   = 7;
                const float bw  = gs / ((float) n * 1.6f);
                const float gap = bw * 0.6f;
                const float tot = (float) n * bw + (float) (n - 1) * gap;
                float x = c.x - tot * 0.5f;

                for (int i = 0; i < n; ++i)
                {
                    const float h = gs * prof[i];
                    g.fillRoundedRectangle(x, c.y - h * 0.5f, bw, h, bw * 0.5f);
                    x += bw + gap;
                }
                break;
            }

            case Kind::model:
            {
                const float rad = gs * 0.5f;
                auto pt = [&](int k)
                {
                    const float a = juce::degreesToRadians(-90.0f + 60.0f * (float) k);
                    return juce::Point<float>(c.x + rad * std::cos(a), c.y + rad * std::sin(a));
                };

                juce::Path hex;
                hex.startNewSubPath(pt(0));
                for (int k = 1; k < 6; ++k)
                    hex.lineTo(pt(k));
                hex.closeSubPath();

                g.setColour(col.withAlpha(0.2f));
                g.fillPath(hex);
                g.setColour(col);
                g.strokePath(hex, stroke);

                for (int k : { 1, 3, 5 })
                    g.drawLine(c.x, c.y, pt(k).x, pt(k).y, thick * 0.8f);
                break;
            }

            case Kind::code:
            {
                juce::Path left, right;
                left.startNewSubPath(c.x - gs * 0.14f, c.y - gs * 0.30f);
                left.lineTo(c.x - gs * 0.44f, c.y);
                left.lineTo(c.x - gs * 0.14f, c.y + gs * 0.30f);

                right.startNewSubPath(c.x + gs * 0.14f, c.y - gs * 0.30f);
                right.lineTo(c.x + gs * 0.44f, c.y);
                right.lineTo(c.x + gs * 0.14f, c.y + gs * 0.30f);

                g.strokePath(left, stroke);
                g.strokePath(right, stroke);
                g.drawLine(c.x + gs * 0.07f, c.y - gs * 0.30f, c.x - gs * 0.07f, c.y + gs * 0.30f, thick * 0.8f);
                break;
            }

            case Kind::image:
            {
                g.fillEllipse(sq.getX() + gs * 0.12f, sq.getY() + gs * 0.06f, gs * 0.24f, gs * 0.24f);

                juce::Path hills;
                hills.startNewSubPath(sq.getX(), sq.getBottom());
                hills.lineTo(sq.getX() + gs * 0.35f, sq.getY() + gs * 0.42f);
                hills.lineTo(sq.getX() + gs * 0.55f, sq.getY() + gs * 0.68f);
                hills.lineTo(sq.getX() + gs * 0.72f, sq.getY() + gs * 0.52f);
                hills.lineTo(sq.getRight(), sq.getBottom());
                hills.closeSubPath();

                g.setColour(col.withAlpha(0.85f));
                g.fillPath(hills);
                break;
            }

            case Kind::data:
            {
                static const float widths[] = { 0.90f, 0.65f, 0.80f, 0.50f };

                for (int i = 0; i < 4; ++i)
                {
                    const float y = sq.getY() + gs * (0.10f + 0.22f * (float) i);
                    g.fillRoundedRectangle(sq.getX(), y, gs * widths[i], gs * 0.10f, gs * 0.05f);
                }
                break;
            }

            default:
                break;
        }

        // ---- extension badge
        if (ext.isNotEmpty())
        {
            g.setColour(col);
            g.setFont(juce::Font(juce::FontOptions(badgeFont, juce::Font::bold)));
            g.drawText(ext, badgeArea.toNearestInt(), juce::Justification::centred, false);
        }
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FileExplorer)
};
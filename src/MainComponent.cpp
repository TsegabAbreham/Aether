#include "MainComponent.h"

MainComponent::MainComponent()
{
    addAndMakeVisible(menuBar);
    addAndMakeVisible(workspace);

    menuBar.onFolderSelected = [this](juce::File folder)
    {
        workspace.setProjectFolder(folder);
    };

    menuBar.onAudioDeviceManagerRequested = [this]()
    {
        return &workspace.getAudioDeviceManager();
    };

    menuBar.onProjectStateRequested = [this]()
    {
        return workspace.captureProjectState();
    };

    menuBar.onProjectDirtyCheck = [this]()
    {
        return ProjectManager::toJson(workspace.captureProjectState()).dump() != savedStateSnapshot;
    };

    menuBar.onProjectSaved = [this](const AetherState& state, const juce::File& file)
    {
        auto& currentState = workspace.getProjectState();
        currentState.projectName = state.projectName;
        currentState.projectFilePath = file.getFullPathName().toStdString();
        savedStateSnapshot = ProjectManager::toJson(workspace.captureProjectState()).dump();
    };

    menuBar.onProjectLoaded = [this](const AetherState& state, juce::String& errorMessage)
    {
        if (! workspace.loadProjectState(state, errorMessage))
            return false;

        savedStateSnapshot = ProjectManager::toJson(workspace.captureProjectState()).dump();
        return true;
    };

    setSize(800, 600);
    savedStateSnapshot = ProjectManager::toJson(workspace.captureProjectState()).dump();
}

MainComponent::~MainComponent()
{
}

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(25, 25, 28));
}

void MainComponent::resized()
{
    auto area = getLocalBounds();

    menuBar.setBounds(area.removeFromTop(30));

    workspace.setBounds(area);
}
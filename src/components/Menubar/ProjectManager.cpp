#include <glm/glm.hpp>
#include <fstream>
#include <iostream>

#include "ProjectManager.h"

#include "../Workspace/Workspace.h"
#include "../Workspace/Panels/Viewport.h"
#include "../Workspace/Panels/FileExplorer.h"


using json = nlohmann::json;

bool ProjectManager::saveProject(const juce::File& file)
{
   json projectData;
   
   // --------------------------
   // ---- Workspace/Panels ----
   // --------------------------
   projectData["workspace"]["viewport"]["camera_position"] = json::array();
   projectData["workspace"]["file_explorer"]["opened_folder"] = "";
   projectData["workspace"]["timeline"]["audio_objects"] = json::array();

   // Viewport Camera Position
   ViewportPanel* viewport;
   glm::vec3 cameraPosition = viewport->getCameraPosition();
   projectData["workspace"]["viewport"]["camera_position"].push_back({
    {"x", cameraPosition.x},
    {"y", cameraPosition.y},
    {"z", cameraPosition.z}
   });

   // File Explorer Current Dir
   FileExplorer* fileExplorer;
   juce::File currentDir = fileExplorer->getBrowsedFolder();
   juce::String currentPath = fileExplorer->getBrowsedFolder().getFullPathName();
   projectData["workspace"]["file_explorer"]["opened_folder"] = currentPath.toStdString();



   return true;


}
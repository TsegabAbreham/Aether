#include <JuceHeader.h>

#include "external/json/json.hpp"

class ProjectManager
{
    public:
        bool saveProject(const juce::File& file);
        bool loadProject(const juce::File& file);
    
    private:
        
};
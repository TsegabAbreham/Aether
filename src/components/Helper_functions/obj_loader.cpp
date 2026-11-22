#include "obj_loader.h"
#include "../Workspace/Panels/Viewport.h"

#include <fstream>
#include <sstream>
#include <vector>
#include <string>

#include <JuceHeader.h>
#include <glm/glm.hpp> 

struct Vertex
{
    float position[3];
    float colour[3];
};

bool ViewportPanel::load_obj(const std::string& filepath, glm::vec3 position)
{
    std::ifstream file(filepath);

    if (!file.is_open())
    {
        DBG("Failed to open OBJ file: " + filepath);
        return false;
    }

    // Temporary storage for OBJ positions
    std::vector<juce::Vector3D<float>> positions;

    // Final vertices sent to OpenGL
    std::vector<Vertex> vertices;

    std::string line;

    while (std::getline(file, line))
    {
        std::stringstream stream(line);

        std::string prefix;
        stream >> prefix;

        // Vertex position
        if (prefix == "v")
        {
            float x, y, z;

            if (stream >> x >> y >> z)
                positions.emplace_back(x, y, z);
        }

        // Face
        else if (prefix == "f")
        {
            std::vector<int> face;
            std::string token;

            while (stream >> token)
            {
                // Supports face formats such as:
                // 1
                // 1/2/3
                // 1//3

                size_t slash = token.find('/');

                std::string positionIndex = token.substr(0, slash);

                try
                {
                    int index = std::stoi(positionIndex);

                    // Convert OBJ indexing to C++ indexing
                    if (index > 0)
                        face.push_back(index - 1);
                    else if (index < 0)
                        face.push_back(static_cast<int>(positions.size()) + index);
                }
                catch (...)
                {
                    DBG("Invalid OBJ face index");
                    return false;
                }
            }

            // Triangulate the face using a triangle fan
            for (size_t i = 1; i + 1 < face.size(); ++i)
            {
                int indices[] = {
                    face[0],
                    face[i],
                    face[i + 1]
                };

                for (int index : indices)
                {
                    if (index < 0 || index >= static_cast<int>(positions.size()))
                        return false;

                    Vertex vertex{};

                    vertex.position[0] = positions[index].x + position[0];
                    vertex.position[1] = positions[index].y + position[1];
                    vertex.position[2] = positions[index].z + position[2];

                    // Default model colour
                    vertex.colour[0] = 0.75f;
                    vertex.colour[1] = 0.72f;
                    vertex.colour[2] = 0.68f;

                    vertices.push_back(vertex);
                }
            }
        }
    }

    file.close();

    if (vertices.empty())
    {
        DBG("OBJ contains no usable triangles");
        return false;
    }

    // Store the loaded geometry
    modelVertices = std::move(vertices);

    DBG("OBJ loaded successfully!");
    DBG("Vertices: " + juce::String(static_cast<int>(modelVertices.size())));

    return true;
}
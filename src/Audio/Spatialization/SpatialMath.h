#pragma once

#include "../Object/AudioListener.h"
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <cmath>

// Conventions (fixed head, never rotates):  +X = right,  +Y = up,  -Z = forward.
namespace SpatialMath
{
    struct SourceAngles
    {
        float azimuth   = 0.0f;   // radians: 0 ahead, +pi/2 right, +-pi behind
        float lateral   = 0.0f;   // radians off the median plane: -pi/2 (left) .. +pi/2 (right)
        float elevation = 0.0f;   // radians: +pi/2 straight up
        float distance  = 0.0f;   // scene units
    };

    inline glm::vec3 getListenerPosition()
    {
        return AudioListener::getListenerPosition();
    }

    // head-relative vector (the head never rotates, so this is only a subtraction)
    inline glm::vec3 toListenerSpace(const glm::vec3& source)
    {
        return source - getListenerPosition();
    }

    inline float getDistance(const glm::vec3& source)
    {
        return glm::length(toListenerSpace(source));
    }

    inline glm::vec3 getDirection(const glm::vec3& source)
    {
        const glm::vec3 p = toListenerSpace(source);
        const float d = glm::length(p);

        return d < 0.0001f ? glm::vec3(0.0f) : p / d;
    }

    inline SourceAngles getSourceAngles(const glm::vec3& source)
    {
        const glm::vec3 p = toListenerSpace(source);

        SourceAngles a;
        a.distance = glm::length(p);

        if (a.distance < 0.0001f)
            return a;

        a.azimuth   = std::atan2(p.x, -p.z);
        a.lateral   = std::asin(glm::clamp(p.x / a.distance, -1.0f, 1.0f));
        a.elevation = std::asin(glm::clamp(p.y / a.distance, -1.0f, 1.0f));
        return a;
    }

    inline float getAttenuation(float distance, float strength = 1.0f)
    {
        return 1.0f / (1.0f + strength * distance);
    }

    // x = left gain, y = right gain (equal power) - used by the simple SpatialGains fallback
    inline glm::vec2 getStereoPan(const glm::vec3& direction)
    {
        const float pan   = glm::clamp(direction.x, -1.0f, 1.0f);
        const float theta = (pan + 1.0f) * glm::pi<float>() / 4.0f;

        return { std::cos(theta), std::sin(theta) };
    }
}
#pragma once

#include <glm/glm.hpp>
#include "SpatialMath.h"

struct SpatialGains
{
    float left  = 1.0f;
    float right = 1.0f;
};

// sourcePosition is in world space; it is made relative to the listener inside SpatialMath.
inline SpatialGains calculateSpatialGains(const glm::vec3& sourcePosition)
{
    const float     distance    = SpatialMath::getDistance(sourcePosition);
    const glm::vec3 direction   = SpatialMath::getDirection(sourcePosition);
    const glm::vec2 pan         = SpatialMath::getStereoPan(direction);
    const float     attenuation = SpatialMath::getAttenuation(distance);

    return { pan.x * attenuation, pan.y * attenuation };
}
#pragma once
#include <glm/glm.hpp>
#include <atomic>

namespace AudioListener
{
    inline std::atomic<float> px { 0.0f }, py { 1.5f }, pz { 0.0f };

    inline glm::vec3 head_position {  0.0f, 1.5f, 0.0f   };

    inline void setListenerPosition(const glm::vec3& p)
    {
        px.store(p.x, std::memory_order_relaxed);
        py.store(p.y, std::memory_order_relaxed);
        pz.store(p.z, std::memory_order_relaxed);
    }

    inline glm::vec3 getListenerPosition()
    {
        return { px.load(std::memory_order_relaxed),
                 py.load(std::memory_order_relaxed),
                 pz.load(std::memory_order_relaxed) };
    }
}
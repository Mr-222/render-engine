#pragma once

#include "core/tool/logger.h"

#include <vulkan/vulkan.h>

#include <string>
#include <vector>

namespace Vk {

// Forward declaration
class Profiler;

/**
 * @class ProfileScope
 * @brief An RAII helper to record/end timestamps in a command buffer.
 *
 * This class is not used directly. It's created by Profiler::profileScope().
 * It's constructor writes a "start" timestamp.
 * It's destructor writes an "end" timestamp.
 */
class ProfileScope {
public:
    ProfileScope(const ProfileScope&) = delete;
    ProfileScope& operator=(const ProfileScope&) = delete;

    ~ProfileScope() {
        if (m_profiler)
            // Write the "end" timestamp
            vkCmdWriteTimestamp(m_commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_queryPool, m_endQueryIndex);
    }

private:
    friend class Profiler;
    ProfileScope(
        Profiler* profiler,
        VkCommandBuffer commandBuffer,
        VkQueryPool queryPool,
        uint32_t startQueryIndex
    ) : m_profiler(profiler),
        m_commandBuffer(commandBuffer),
        m_queryPool(queryPool),
        m_startQueryIndex(startQueryIndex),
        m_endQueryIndex(startQueryIndex + 1) {

        // Write the "start" timestamp
        vkCmdWriteTimestamp(m_commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, m_queryPool, m_startQueryIndex);
    }

    Profiler* m_profiler;
    VkCommandBuffer m_commandBuffer;
    VkQueryPool m_queryPool;
    uint32_t m_startQueryIndex;
    uint32_t m_endQueryIndex;
};

/**
 * @class Profiler
 * @brief Manages Vulkan GPU timestamps queries and results.
 */
class Profiler {
public:
    Profiler() = default;
    void init(VkDevice device, VkPhysicalDevice physicalDevice, uint32_t maxScopes = 64) {
        m_device       = device;
        m_maxScopes    = maxScopes;
        m_currentScope = 0;

        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(physicalDevice, &props);
        m_timestampPeriod = props.limits.timestampPeriod;

        if (m_timestampPeriod == 0.0f || !props.limits.timestampComputeAndGraphics) {
            ERROR_ALL("Device does not support timestamps!");
        }

        uint32_t queryCount = maxScopes * 2; // Each scope has a start and end timestamp

        VkQueryPoolCreateInfo queryPoolInfo = {};
        queryPoolInfo.sType      = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        queryPoolInfo.queryType  = VK_QUERY_TYPE_TIMESTAMP;
        queryPoolInfo.queryCount = queryCount;

        if (vkCreateQueryPool(m_device, &queryPoolInfo, nullptr, &m_queryPool) != VK_SUCCESS) {
            ERROR_ALL("Failed to create query pool for Profiler!");
        }

        m_queryResults.resize(queryCount);
        m_scopeNames.reserve(queryCount);
    }

    ~Profiler() {
        if (m_queryPool != VK_NULL_HANDLE)
            vkDestroyQueryPool(m_device, m_queryPool, nullptr);
    }

    /**
     * @brief Must be called at the start of a command buffer, before any profileScope calls.
     */
    void beginFrame(VkCommandBuffer commandBuffer) {
        if (m_timestampPeriod == 0.0f)
            return;

        vkCmdResetQueryPool(commandBuffer, m_queryPool, 0, m_maxScopes * 2);
        m_currentScope = 0;
        m_scopeNames.clear();
    }

    ProfileScope profileScope(VkCommandBuffer commandBuffer, const std::string& name) {
        if (m_timestampPeriod == 0.0f || m_currentScope >= m_maxScopes)
            // Return a dummy ProfileScope that does nothing
            return { nullptr, VK_NULL_HANDLE, VK_NULL_HANDLE, 0 };

        uint32_t queryIndex = m_currentScope * 2;
        m_scopeNames.push_back(name);
        m_currentScope++;

        // Return RAII object
        return { this, commandBuffer, m_queryPool, queryIndex };
    }

    void printResults() const {
        if (m_timestampPeriod == 0.0f || m_currentScope == 0) {
            INFO_ALL("--- Vulkan Profiler (No data) ---");
            return;
        }

        VkResult result = vkGetQueryPoolResults(
            m_device,
            m_queryPool,
            0,
            m_currentScope * 2,
            m_queryResults.size() * sizeof(uint64_t),
            (void*)m_queryResults.data(),
            sizeof(uint64_t),
            VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT
        );
        if (result != VK_SUCCESS) {
            ERROR_ALL("Failed to get query pool results for Profiler!");
            return;
        }

        INFO_ALL("--- Vulkan Profiler results ---");
        for (uint32_t i = 0; i < m_currentScope; ++i) {
            uint64_t startTick = m_queryResults[i * 2];
            uint64_t endTick   = m_queryResults[i * 2 + 1];
            std::string name   = m_scopeNames[i];

            uint64_t ticks = endTick - startTick;
            double nanoseconds = ticks * m_timestampPeriod;
            double milliseconds = nanoseconds / 1e6;

            INFO_ALL(name + ": " + std::to_string(milliseconds) + " ms");
        }
        INFO_ALL("------------------------------");
    }

private:
    VkDevice m_device;
    VkQueryPool m_queryPool = VK_NULL_HANDLE;
    float m_timestampPeriod = 0.0f; // Nanoseconds per tick
    uint32_t m_maxScopes    = 0;
    uint32_t m_currentScope;

    std::vector<std::string> m_scopeNames;
    std::vector<uint64_t> m_queryResults;
};

#define VK_PROFILE_SCOPE(profiler, cmd, name) auto profile_scope_##__LINE__ = profiler.profileScope(cmd, name);

}
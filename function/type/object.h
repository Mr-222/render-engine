#pragma once

#include "core/config/config.h"
#include "core/vulkan/descriptor_manager.h"
#include "core/vulkan/type/buffer.h"
#include "function/resource_manager/resource.h"
#include "function/type/transform.h"

#ifdef _WIN64
#include <Windows.h>
#endif

struct Object : public Resource {
    struct Param {
        glm::mat4 model;
        glm::mat4 modelInvTrans;
        Vk::DescriptorHandle material;
    };

    std::string name;
    uuid::UUID uuid;

    std::string mesh;

    Transform transform;

    Param param;
    Vk::Buffer paramBuffer;

#ifdef _WIN64
    HANDLE getVkVertexMemHandle();
#else
    int getVkVertexMemHandle();
#endif
    void updatePosition(float delta_time);
    virtual std::string type() const override { return "Object"; }
    virtual void destroy() override;
    static Object fromConfiguration(ObjectConfiguration& config);
};

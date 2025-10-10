#pragma once

#include "core/tool/enum_bit_op.h"
#include "core/vulkan/type/image.h"
#include <string>
#include <unordered_map>

enum class RenderAttachmentType : uint8_t {
    Color    = 1 << 0,
    Depth    = 1 << 1,
    Stencil  = 1 << 2,
    Sampler  = 1 << 3,
    External = 1 << 4, // will be used by CUDA engine
    DontRecreateOnResize = 1 << 5,
};
DEFINE_ENUM_BIT_OPERATORS(RenderAttachmentType)

struct RenderAttachment {
    std::string name;

    Vk::Image image;
    VkImageUsageFlags usage;
    RenderAttachmentType type;
    void destroy();
};

class RenderAttachments {
    static VkImageAspectFlags getAspectFlags(RenderAttachmentType type);

public:
    // you need to specify the complete type and usage.
    // type can't only be sampler.
    void addAttachment(const std::string& name, RenderAttachmentType type, VkImageUsageFlags usage, VkFormat format, VkExtent3D extent, size_t numLayers);
    void removeAttachment(const std::string& name);
    Vk::Image& getAttachment(const std::string& name);
    void onResize();

    void cleanup();

    std::unordered_map<std::string, RenderAttachment> attachments;
};

#pragma once

#include "core/config/config.h"
#include "function/render/render_graph/pipeline.hpp"
#include "function/render/render_graph/render_attachment_description.h"
#include <string>
#include <unordered_map>

class RenderGraphNode {
protected:
    struct AttachmentDescriptionHelper {
        std::string name;
        VkAttachmentLoadOp load_op;
        VkAttachmentStoreOp store_op;
    };
    static VkRenderPass DefaultRenderPass(
        const std::unordered_map<std::string, RenderAttachmentDescription>& attachment_descriptions,
        const std::vector<AttachmentDescriptionHelper>& desc,
        const VkSubpassDependency& dependency);
    /**
     * @brief Creates a VkRenderPass with support for multiple subpasses.
     *
     * @param attachment_info_map A map from an attachment's name to its full description.
     * @param attachment_configs A list of attachments to be included in this render pass.
     * **Crucially, the order of attachments in this vector must exactly match the
     * order of VkImageViews that will be provided when creating the compatible VkFramebuffer.**
     * @param dependencies A vector of subpass dependencies that define execution and memory
     * barriers between the subpasses.
     * @return A handle to the created VkRenderPass.
    */
    static VkRenderPass MultiSubpassRenderPass(
        const std::unordered_map<std::string, RenderAttachmentDescription>& attachment_info_map,
        const std::vector<AttachmentDescriptionHelper>& attachment_configs,
        const std::vector<VkSubpassDependency>& dependencies);
    void bindDescriptorSet(uint32_t index, VkPipelineLayout layout, VkDescriptorSet* set);
    void setDefaultViewportAndScissor();
    Vk::Image* getAttachmentByName(const std::string& name, RenderAttachments* attachments, int swapchain_index);

public:
    // init the descriptrion directly in the derived class
    RenderGraphNode(const std::string& name);
    virtual ~RenderGraphNode() = default;

    virtual void init(Configuration& cfg, RenderAttachments& attachments) = 0;
    virtual void record(uint32_t swapchain_index)                         = 0;
    virtual void onResize()                                               = 0;
    virtual void destroy()                                                = 0;

    std::string name;
    std::unordered_map<std::string, RenderAttachmentDescription> attachment_descriptions;
};

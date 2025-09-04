#pragma once

#include "function/render/render_graph/render_graph_node.h"

class Voxelization : public RenderGraphNode {
    struct Param {
        Vk::DescriptorHandle camera;
    };

    void createRenderPass();
    void createFramebuffer();
    void createPipeline(Configuration& cfg);

    Pipeline<Param> pipeline;
    VkRenderPass render_pass;
    std::vector<VkFramebuffer> framebuffers;
    RenderAttachments* attachments;

public:
    Voxelization(
        const std::string& name,
        const std::string& voxel_buf_name);

    virtual void init(Configuration& cfg, RenderAttachments& attachments) override;
    virtual void record(uint32_t swapchain_index) override;
    virtual void onResize() override;
    virtual void destroy() override;
};
#pragma once

#include "function/render/render_graph/render_graph_node.h"

class Voxelization : public RenderGraphNode {
    struct Param {
        Vk::DescriptorHandle voxelizationViewMat;
        Vk::DescriptorHandle voxelizationProjMats;
    };
    struct Foo {};

    VkPipelineVertexInputStateCreateInfo getVertexInputeState();
    VkPipelineRasterizationStateCreateInfo getRasterizationState(bool rasterize);
    VkPipelineViewportStateCreateInfo getViewportState();

    void createMatsBuffer();
    void createVertPosBuffer();
    void createRenderPass();
    void createFramebuffer();
    void createVoxelizationPipeline(Configuration& cfg);
    void createVertexPosPipeline(Configuration& cfg);
    void setViewportAndScissor();

    static constexpr uint32_t VOXEL_GRID_SIZE = 128;

    Pipeline<Param> voxelPipeline;
    Pipeline<Foo>   vertexPosPipeline;
    VkRenderPass render_pass;
    std::vector<VkFramebuffer> framebuffers;
    RenderAttachments* attachments;

    std::vector<Vk::Buffer> vert_pos_buffers;

    glm::mat4 view_mat;
    Vk::Buffer view_mat_buffer;
    std::array<glm::mat4, VOXEL_GRID_SIZE> proj_mats;
    Vk::Buffer proj_mats_buffer;

public:
    Voxelization(
        const std::string& name,
        const std::string& voxel_buf_name);

    void init(Configuration& cfg, RenderAttachments& attachments) override;
    void record(uint32_t swapchain_index) override;
    void onResize() override;
    void destroy() override;
};
#pragma once

#include "function/render/render_graph/render_graph_node.h"

class Voxelization : public RenderGraphNode {
    struct VoxelParam {
        Vk::DescriptorHandle voxelizationViewMat;
        Vk::DescriptorHandle voxelizationProjMats;
    };

    struct VelocityParam {
        glm::vec2 projSpacePixDim;
        float deltaT;
        Vk::DescriptorHandle voxelizationViewMat;
        Vk::DescriptorHandle voxelizationProjMats;
    };

    struct EmptyParam {};

    VkPipelineVertexInputStateCreateInfo getVertexInputState();
    VkPipelineRasterizationStateCreateInfo getRasterizationState(bool rasterize);
    VkPipelineViewportStateCreateInfo getViewportState();

    void createMatsBuffer();
    void createVertPosBuffer();
    void createRenderPass();
    void createVoxelTex();
    void createFramebuffer();
    void createVoxelizationPipeline(Configuration& cfg);
    void createVelocityRecordPipeline(Configuration& cfg);
    void createVertexPosPipeline(Configuration& cfg);
    void setViewportAndScissor();
    void updateTime();

    Pipeline<VoxelParam> voxel_pipeline;
    Pipeline<VelocityParam> velocity_pipeline;
    Pipeline<EmptyParam> vertex_pos_pipeline;
    VkRenderPass render_pass;
    std::vector<VkFramebuffer> framebuffers;
    RenderAttachments* attachments;

    Vk::Buffer staging_buffer;
    Vk::Image voxel_tex;

    std::vector<Vk::Buffer> vert_pos_buffers;

    VoxelizerConfiguration config;
    glm::mat4 view_mat;
    Vk::Buffer view_mat_buffer;
    std::vector<glm::mat4> proj_mats;
    Vk::Buffer proj_mats_buffer;

public:
    Voxelization(
        const std::string& name,
        const std::string& stencil_tex_name,
        const std::string& velocity_tex_name,
        const Configuration& cfg);

    void init(Configuration& cfg, RenderAttachments& attachments) override;
    void record(uint32_t swapchain_index) override;
    void onResize() override;
    void destroy() override;
};
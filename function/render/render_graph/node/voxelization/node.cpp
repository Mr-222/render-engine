#include "./node.h"
#include "core/vulkan/vulkan_util.h"
#include "function/global_context.h"
#include "function/render/render_graph/pipeline.hpp"
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

using namespace Vk;

Voxelization::Voxelization(const std::string& name, const std::string& voxel_buf_name)
    : RenderGraphNode(name)
{
    assert(voxel_buf_name != RenderAttachmentDescription::SWAPCHAIN_IMAGE_NAME());
    attachment_descriptions = {
        {
            "voxel",
            {
                voxel_buf_name,
                RenderAttachmentType::Stencil | RenderAttachmentType::DontRecreateOnResize,
                RenderAttachmentRW::Write,
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                VK_FORMAT_S8_UINT,
                { VOXEL_GRID_SIZE, VOXEL_GRID_SIZE, 1 },
                VOXEL_GRID_SIZE,
            },
        }
    };
}

void Voxelization::init(Configuration& cfg, RenderAttachments& attachments)
{
    this->attachments = &attachments;
    createMatsBuffer();
    createRenderPass();
    createFramebuffer();
    createPipeline(cfg);
}

void Voxelization::createMatsBuffer()
{
    view_mat = glm::lookAt(
        glm::vec3(0.0f, 5.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, -1.0f)
    );
    view_mat_buffer = Buffer::New(
        g_ctx.vk,
        sizeof(glm::mat4),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        true
    );
    view_mat_buffer.Update(g_ctx.vk, &view_mat, sizeof(glm::mat4));
    g_ctx.dm.registerResource(view_mat_buffer, DescriptorType::Uniform);

    // Near plane matches the depth of the current slice
    for (int i = 1; i <= VOXEL_GRID_SIZE; ++i) {
        constexpr int left = -5.f, right = 5.f, bottom = -5.f, top = 5.f, farPlane = 20.f;
        constexpr int step = 10.f / VOXEL_GRID_SIZE;
        proj_mats[i - 1] = glm::ortho(left, right, bottom, top, i * step, farPlane);
        proj_mats[i - 1][1][1] *= -1;
    }
    proj_mats_buffer = Buffer::New(
        g_ctx.vk,
        sizeof(proj_mats),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        true
    );
    proj_mats_buffer.Update(g_ctx.vk, proj_mats.data(), sizeof(proj_mats));
    g_ctx.dm.registerResource(proj_mats_buffer, DescriptorType::Storage);
}

void Voxelization::createFramebuffer()
{

}

void Voxelization::createRenderPass() {

}

void Voxelization::createPipeline(Configuration& cfg)
{

}

void Voxelization::record(uint32_t swapchain_index)
{

}

void Voxelization::onResize()
{

}

void Voxelization::destroy()
{
    pipeline.destroy();
    vkDestroyRenderPass(g_ctx.vk.device, render_pass, nullptr);
    for (auto& framebuffer : framebuffers) {
        vkDestroyFramebuffer(g_ctx.vk.device, framebuffer, nullptr);
    }
    Buffer::Delete(g_ctx.vk, view_mat_buffer);
    Buffer::Delete(g_ctx.vk, proj_mats_buffer);
}

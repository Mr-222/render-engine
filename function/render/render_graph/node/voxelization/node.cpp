#include "./node.h"
#include "core/vulkan/vulkan_util.h"
#include "function/global_context.h"
#include "function/render/render_graph/pipeline.hpp"

using namespace Vk;

static constexpr uint32_t VOXEL_GRID_SIZE = 128;

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
                VK_FORMAT_D24_UNORM_S8_UINT,
                { VOXEL_GRID_SIZE, VOXEL_GRID_SIZE, 1 },
                VOXEL_GRID_SIZE,
            },
        }
    };
}

void Voxelization::init(Configuration& cfg, RenderAttachments& attachments)
{

}

void Voxelization::createFramebuffer()
{

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
}

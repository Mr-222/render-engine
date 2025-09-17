#define GLM_FORCE_DEPTH_ZERO_TO_ONE

#include "./node.h"
#include "core/filesystem/file.h"
#include "core/vulkan/vulkan_util.h"
#include "function/global_context.h"
#include "function/render/render_graph/pipeline.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "function/resource_manager/resource_manager.h"

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
                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
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
        constexpr float left = -5.f, right = 5.f, bottom = -5.f, top = 5.f, farPlane = 20.f;
        constexpr float step = 10.f / VOXEL_GRID_SIZE;
        proj_mats[i - 1] = glm::ortho(left, right, bottom, top, static_cast<float>(i) * step, farPlane);
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

void Voxelization::createRenderPass()
{
    std::vector<AttachmentDescriptionHelper> helpers = {
        {"voxel", VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE },
    };
    VkSubpassDependency dependency = {};
    dependency.srcSubpass          = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass          = 0;
    dependency.srcStageMask        = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependency.dstStageMask        = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask       = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependency.dstAccessMask       = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    render_pass = DefaultRenderPass(attachment_descriptions, helpers, dependency);
}

void Voxelization::createFramebuffer()
{
    framebuffers.resize(g_ctx.vk.swapChainImages.size());
    for (int i = 0; i < g_ctx.vk.swapChainImages.size(); ++i) {
        std::array<VkImageView, 1> views = {
            attachments->getAttachment(attachment_descriptions["voxel"].name).view,
        };

        VkFramebufferCreateInfo framebufferInfo {};
        framebufferInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass      = render_pass;
        framebufferInfo.attachmentCount = static_cast<uint32_t>(views.size());
        framebufferInfo.pAttachments    = views.data();
        framebufferInfo.width           = VOXEL_GRID_SIZE;
        framebufferInfo.height          = VOXEL_GRID_SIZE;
        framebufferInfo.layers          = VOXEL_GRID_SIZE;

        if (vkCreateFramebuffer(g_ctx.vk.device, &framebufferInfo, nullptr, &framebuffers[i]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create framebuffer!");
        }
    }
}

void Voxelization::createPipeline(Configuration& cfg)
{
    {
        std::vector<VkDescriptorSetLayout> descLayouts = {
            g_ctx.dm.BINDLESS_LAYOUT(),
            g_ctx.dm.PARAMETER_LAYOUT(),
        };
        pipeline.initLayout(descLayouts);
    }

    {
        auto getVertexInputeState = [] {
            static VkVertexInputBindingDescription bindingDescription {};
            bindingDescription.binding   = 0;
            bindingDescription.stride    = sizeof(Vertex);
            bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

            static VkVertexInputAttributeDescription attributeDescription {};
            attributeDescription.binding  = 0;
            attributeDescription.location = 0;
            attributeDescription.format   = VK_FORMAT_R32G32B32_SFLOAT;
            attributeDescription.offset   = offsetof(Vertex, pos);

            VkPipelineVertexInputStateCreateInfo vertexInput {};
            vertexInput.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
            vertexInput.vertexBindingDescriptionCount   = 1;
            vertexInput.pVertexBindingDescriptions      = &bindingDescription;
            vertexInput.vertexAttributeDescriptionCount = 1;
            vertexInput.pVertexAttributeDescriptions    = &attributeDescription;
            return vertexInput;
        };

        auto getRasterizationState = [] {
            VkPipelineRasterizationStateCreateInfo rasterizer {};
            rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
            rasterizer.depthClampEnable        = VK_FALSE;
            rasterizer.rasterizerDiscardEnable = VK_FALSE;
            rasterizer.polygonMode             = VK_POLYGON_MODE_FILL;
            rasterizer.lineWidth               = 1.0f; // if it's not fill mode
            rasterizer.cullMode                = VK_CULL_MODE_NONE; // No culling required by voxelization
            rasterizer.frontFace               = VK_FRONT_FACE_COUNTER_CLOCKWISE;
            rasterizer.depthBiasEnable         = VK_FALSE;
            return rasterizer;
        };

        auto getViewportState = [] {
            static VkViewport viewport {};
            viewport.x        = 0.0f;
            viewport.y        = 0.0f;
            viewport.width    = static_cast<float>(VOXEL_GRID_SIZE);
            viewport.height   = static_cast<float>(VOXEL_GRID_SIZE);
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;

            static VkRect2D scissor {};
            scissor.offset = { 0, 0 };
            scissor.extent = { VOXEL_GRID_SIZE, VOXEL_GRID_SIZE };

            VkPipelineViewportStateCreateInfo viewportState {};
            viewportState.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
            viewportState.viewportCount = 1;
            viewportState.pViewports    = &viewport;
            viewportState.scissorCount  = 1;
            viewportState.pScissors     = &scissor;
            return viewportState;
        };

        auto vertexInput   = getVertexInputeState();
        DynamicStateDefault();
        auto viewportState = getViewportState();
        auto inputAssembly = Pipeline<Param>::inputAssemblyDefault();
        auto rasterization = getRasterizationState();
        auto multisample   = Pipeline<Param>::multisampleDefault();

        JSON_GET(RenderGraphConfiguration, rg_cfg, cfg, "render_graph");
        auto vertShaderCode    = readFile(rg_cfg.shader_directory + "/voxelization/node.vert.spv");
        auto fragShaderCode    = readFile(rg_cfg.shader_directory + "/voxelization/node.frag.spv");
        auto vertShaderModule  = createShaderModule(g_ctx.vk, vertShaderCode);
        auto fragShaderModule  = createShaderModule(g_ctx.vk, fragShaderCode);
        std::vector<VkPipelineShaderStageCreateInfo> shaderStages = {
            Pipeline<Param>::shaderStageDefault(vertShaderModule, VK_SHADER_STAGE_VERTEX_BIT),
            Pipeline<Param>::shaderStageDefault(fragShaderModule, VK_SHADER_STAGE_FRAGMENT_BIT),
        };
        VkPipelineColorBlendStateCreateInfo colorBlending {};
        colorBlending.sType            = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.logicOpEnable    = VK_FALSE;
        colorBlending.attachmentCount  = 0;
        colorBlending.pAttachments     = nullptr;

        VkPipelineDepthStencilStateCreateInfo depthStencil {};
        depthStencil.sType             = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable   = VK_FALSE;
        depthStencil.depthWriteEnable  = VK_FALSE;

        // Stencil operations to increment for back faces and decrement for front faces (with wrapping in both cases).
        depthStencil.stencilTestEnable = VK_TRUE;
        // --- Front Face Stencil Operations ---
        // Configure rules for polygons facing the camera.
        depthStencil.front.failOp      = VK_STENCIL_OP_KEEP;
        depthStencil.front.passOp      = VK_STENCIL_OP_DECREMENT_AND_WRAP;
        depthStencil.front.depthFailOp = VK_STENCIL_OP_KEEP;
        depthStencil.front.compareOp   = VK_COMPARE_OP_ALWAYS;
        depthStencil.front.compareMask = 0xff;
        depthStencil.front.writeMask   = 0xff;
        depthStencil.front.reference   = 1;

        // --- Back Face Stencil Operations ---
        // Configure rules for polygons facing away from the camera.
        depthStencil.back.failOp      = VK_STENCIL_OP_KEEP;
        depthStencil.back.passOp      = VK_STENCIL_OP_INCREMENT_AND_WRAP;
        depthStencil.back.depthFailOp = VK_STENCIL_OP_KEEP;
        depthStencil.back.compareOp   = VK_COMPARE_OP_ALWAYS;
        depthStencil.back.compareMask = 0xff;
        depthStencil.back.writeMask   = 0xff;
        depthStencil.back.reference   = 1;

        VkGraphicsPipelineCreateInfo pipelineInfo {};
        pipelineInfo.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount          = static_cast<uint32_t>(shaderStages.size());
        pipelineInfo.pStages             = shaderStages.data();
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pVertexInputState   = &vertexInput;
        pipelineInfo.pViewportState      = &viewportState;
        pipelineInfo.pRasterizationState = &rasterization;
        pipelineInfo.pDepthStencilState  = &depthStencil;
        pipelineInfo.pMultisampleState   = &multisample;
        pipelineInfo.pColorBlendState    = &colorBlending;
        pipelineInfo.pDynamicState       = &dynamicState;
        pipelineInfo.layout              = pipeline.layout;
        pipelineInfo.renderPass          = render_pass;
        pipelineInfo.subpass             = 0;
        if (vkCreateGraphicsPipelines(g_ctx.vk.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline.pipeline) != VK_SUCCESS) {
            throw std::runtime_error("failed to create pipeline!");
        }
        vkDestroyShaderModule(g_ctx.vk.device, vertShaderModule, nullptr);
        vkDestroyShaderModule(g_ctx.vk.device, fragShaderModule, nullptr);
    }

    {
        pipeline.param.voxelizationViewMat  = g_ctx.dm.getResourceHandle(view_mat_buffer.id);
        pipeline.param.voxelizationProjMats = g_ctx.dm.getResourceHandle(proj_mats_buffer.id);
        pipeline.param_buf                  = Buffer::New(
            g_ctx.vk,
            sizeof(Param),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            true
        );
        pipeline.param_buf.Update(g_ctx.vk, &pipeline.param, sizeof(Param));
        g_ctx.dm.registerParameter(pipeline.param_buf);
    }
}

void Voxelization::setViewportAndScissor()
{
    VkViewport viewport {};
    viewport.x        = 0.0f;
    viewport.y        = 0.0f;
    viewport.width    = static_cast<float>(VOXEL_GRID_SIZE);
    viewport.height   = static_cast<float>(VOXEL_GRID_SIZE);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(g_ctx.vk.commandBuffer, 0, 1, &viewport);

    VkRect2D scissor {};
    scissor.offset = { 0, 0 };
    scissor.extent = { VOXEL_GRID_SIZE, VOXEL_GRID_SIZE };
    vkCmdSetScissor(g_ctx.vk.commandBuffer, 0, 1, &scissor);
}

void Voxelization::record(uint32_t swapchain_index)
{
    setViewportAndScissor();

    VkClearValue clearValue;
    clearValue.depthStencil = { 1.0f, 0 };
    VkRenderPassBeginInfo renderPassInfo {};
    renderPassInfo.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass        = render_pass;
    renderPassInfo.framebuffer       = framebuffers[swapchain_index];
    renderPassInfo.renderArea.offset = { 0, 0 };
    renderPassInfo.renderArea.extent = { VOXEL_GRID_SIZE, VOXEL_GRID_SIZE };
    renderPassInfo.clearValueCount   = 1;
    renderPassInfo.pClearValues      = &clearValue;
    vkCmdBeginRenderPass(g_ctx.vk.commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(g_ctx.vk.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.pipeline);
    bindDescriptorSet(0, pipeline.layout, g_ctx.dm.BINDLESS_SET());
    bindDescriptorSet(1, pipeline.layout, g_ctx.dm.getParameterSet(pipeline.param_buf.id));

    for (const auto& obj : g_ctx.rm->objects) {
        const auto& mesh = g_ctx.rm->meshes[obj.mesh];

        VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(g_ctx.vk.commandBuffer, 0, 1, &mesh.vertexBuffer.buffer, offsets);
        vkCmdBindIndexBuffer(g_ctx.vk.commandBuffer, mesh.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(g_ctx.vk.commandBuffer, mesh.data.indices.size(), VOXEL_GRID_SIZE, 0, 0, 0);
    }

    vkCmdEndRenderPass(g_ctx.vk.commandBuffer);
}

void Voxelization::onResize()
{
    // Do nothing since the voxel texture is fixed size
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

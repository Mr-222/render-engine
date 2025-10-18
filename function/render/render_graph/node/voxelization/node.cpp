#define GLM_FORCE_DEPTH_ZERO_TO_ONE

#include "./node.h"
#include "core/tool/logger.h"
#include "core/filesystem/file.h"
#include "core/vulkan/vulkan_util.h"
#include "function/global_context.h"
#include "function/render/render_graph/pipeline.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "function/resource_manager/resource_manager.h"

using namespace Vk;

Voxelization::Voxelization(const std::string& name, const std::string& stencil_tex_name, const std::string& velocity_tex_name, const Configuration& cfg)
    : RenderGraphNode(name)
{
    if (!cfg.contains("voxelizer"))
        ERROR_ALL("Config file does not contain voxelizer section!");
    config = cfg.at("voxelizer");
    proj_mats.resize(config.dimension[1]);

    assert(stencil_tex_name != RenderAttachmentDescription::SWAPCHAIN_IMAGE_NAME());
    attachment_descriptions = {
        {
            "stencil",
            {
                stencil_tex_name,
                0,
                RenderAttachmentType::Stencil | RenderAttachmentType::DontRecreateOnResize,
                RenderAttachmentRW::Write,
                VK_IMAGE_LAYOUT_STENCIL_ATTACHMENT_OPTIMAL,
                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                VK_FORMAT_S8_UINT,
                { config.dimension[0], config.dimension[2], 1 },
                config.dimension[1],
            },
        },
        {
            "velocity",
            {
                velocity_tex_name,
                1,
                RenderAttachmentType::Color | RenderAttachmentType::External | RenderAttachmentType::DontRecreateOnResize,
                RenderAttachmentRW::Write,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                VK_FORMAT_R16G16B16A16_SFLOAT,
                { config.dimension[0], config.dimension[2], 1 },
                config.dimension[1],
            }
        },
    };
}

void Voxelization::init(Configuration& cfg, RenderAttachments& attachments)
{
    this->attachments = &attachments;
    createMatsBuffer();
    createVertPosBuffer();
    createRenderPass();
    createFramebuffer();
    createVoxelTex();
    createVoxelizationPipeline(cfg);
    createVelocityRecordPipeline(cfg);
    createVertexPosPipeline(cfg);

    Image& velocity_img = attachments.getAttachment("velocity");
    assert(!g_ctx.rm->textures.contains("voxel"));
    assert(!g_ctx.rm->textures.contains("velocity"));
    g_ctx.rm->textures["voxel"] = Texture { "voxel", voxel_tex };
    g_ctx.rm->textures["velocity"] = Texture { "velocity", velocity_img };
}

void Voxelization::createMatsBuffer()
{
    auto eye_pos = glm::vec3(config.start_pos[0] + config.size[0] / 2.0f, config.start_pos[1], config.start_pos[2] + config.size[2] / 2.0f);
    auto center  = glm::vec3(config.start_pos[0] + config.size[0] / 2.0f, config.start_pos[1] + config.size[1] / 2.0f, config.start_pos[2] + config.size[2] / 2.0f);

    view_mat = glm::lookAt(
        eye_pos,
        center,
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

    for (int i = 0; i < config.dimension[1]; ++i) {
        const float left = -config.size[0] / 2.f, right = config.size[0] / 2.f, bottom = -config.size[2] / 2.f, top = config.size[2] / 2.f;
        const float farPlane = config.size[1];
        const float step = farPlane / static_cast<float>(config.dimension[1]);
        const float half_step = step * 0.5f;
        // Near plane matches the depth of the current slice
        proj_mats[i] = glm::ortho(left, right, bottom, top, static_cast<float>(i) * step + half_step, farPlane);
        proj_mats[i][1][1] *= -1;
    }
    proj_mats_buffer = Buffer::New(
        g_ctx.vk,
        sizeof(glm::mat4) * proj_mats.size(),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        true
    );
    proj_mats_buffer.Update(g_ctx.vk, proj_mats.data(), sizeof(glm::mat4) * proj_mats.size());
    g_ctx.dm.registerResource(proj_mats_buffer, DescriptorType::Storage);
}

void Voxelization::createVertPosBuffer()
{
    vert_pos_buffers.resize(g_ctx.rm->objects.size());
    for (int i = 0; i < g_ctx.rm->objects.size(); ++i) {
        Object& obj = g_ctx.rm->objects[i];
        const Mesh& mesh = g_ctx.rm->meshes[obj.mesh];
        if (!mesh.isWaterTight)
            continue;

        Buffer buffer = Buffer::New(
                    g_ctx.vk,
                sizeof(glm::vec4) * mesh.data.vertices.size(),
                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_BUFFER_BIT_EXT,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                    false
                );
        buffer.ClearSingleTime(g_ctx.vk);
        obj.param.vertBuf = g_ctx.dm.registerResource(buffer, DescriptorType::Storage);
        vert_pos_buffers[i] = buffer;
    }
}

void Voxelization::createRenderPass()
{
    std::vector<AttachmentDescriptionHelper> helpers = {
        {"stencil", VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE },
        { "velocity", VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE }
    };

    std::vector<VkSubpassDependency> dependencies = {
        {
            VK_SUBPASS_EXTERNAL,
            0,
            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
        },
    {
        VK_SUBPASS_EXTERNAL,
        1,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_TRANSFORM_FEEDBACK_BIT_EXT,
        VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFORM_FEEDBACK_WRITE_BIT_EXT,
        VK_ACCESS_SHADER_READ_BIT
        },
        {
            1,
            2,
            VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
            VK_PIPELINE_STAGE_TRANSFORM_FEEDBACK_BIT_EXT,
            VK_ACCESS_SHADER_READ_BIT,
            VK_ACCESS_TRANSFORM_FEEDBACK_WRITE_BIT_EXT
        }
    };

    render_pass = MultiSubpassRenderPass(attachment_descriptions, helpers, dependencies);
}

void Voxelization::createVoxelTex()
{
    staging_buffer = Buffer::New(
        g_ctx.vk,
        config.dimension[0] * config.dimension[1] * config.dimension[2] * sizeof(uint8_t),
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    staging_buffer.ClearSingleTime(g_ctx.vk);

    voxel_tex = Image::New(
        g_ctx.vk,
        VK_FORMAT_R8_UINT,
        { config.dimension[0], config.dimension[2], config.dimension[1] },
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        1,
        1,
        true,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_TYPE_3D,
        VK_IMAGE_VIEW_TYPE_3D);
    voxel_tex.TransitionLayoutSingleTime(g_ctx.vk, VK_IMAGE_LAYOUT_GENERAL);
}

void Voxelization::createFramebuffer()
{
    framebuffers.resize(g_ctx.vk.swapChainImages.size());
    for (int i = 0; i < g_ctx.vk.swapChainImages.size(); ++i) {
        std::array<VkImageView, 2> views = {
            attachments->getAttachment(attachment_descriptions["stencil"].name).view,
            attachments->getAttachment(attachment_descriptions["velocity"].name).view,
        };

        VkFramebufferCreateInfo framebufferInfo {};
        framebufferInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass      = render_pass;
        framebufferInfo.attachmentCount = static_cast<uint32_t>(views.size());
        framebufferInfo.pAttachments    = views.data();
        framebufferInfo.width           = config.dimension[0];
        framebufferInfo.height          = config.dimension[2];
        framebufferInfo.layers          = config.dimension[1];

        if (vkCreateFramebuffer(g_ctx.vk.device, &framebufferInfo, nullptr, &framebuffers[i]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create framebuffer!");
        }
    }
}

VkPipelineVertexInputStateCreateInfo Voxelization::getVertexInputState()
{
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
}

VkPipelineRasterizationStateCreateInfo Voxelization::getRasterizationState(bool rasterize)
{
    VkPipelineRasterizationStateCreateInfo rasterizer {};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable        = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = rasterize ? VK_FALSE : VK_TRUE;
    rasterizer.polygonMode             = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth               = 1.0f; // if it's not fill mode
    rasterizer.cullMode                = VK_CULL_MODE_NONE; // No culling required by voxelization
    rasterizer.frontFace               = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable         = VK_FALSE;
    return rasterizer;
}

VkPipelineViewportStateCreateInfo Voxelization::getViewportState()
{
    static VkViewport viewport {};
    viewport.x        = 0.0f;
    viewport.y        = 0.0f;
    viewport.width    = static_cast<float>(config.dimension[0]);
    viewport.height   = static_cast<float>(config.dimension[2]);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    static VkRect2D scissor {};
    scissor.offset = { 0, 0 };
    scissor.extent = { config.dimension[0], config.dimension[2] };

    VkPipelineViewportStateCreateInfo viewportState {};
    viewportState.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports    = &viewport;
    viewportState.scissorCount  = 1;
    viewportState.pScissors     = &scissor;
    return viewportState;
}

void Voxelization::createVoxelizationPipeline(Configuration& cfg)
{
    {
        std::vector<VkDescriptorSetLayout> descLayouts = {
            g_ctx.dm.BINDLESS_LAYOUT(),
            g_ctx.dm.PARAMETER_LAYOUT(),
            g_ctx.dm.PARAMETER_LAYOUT(),
        };
        voxel_pipeline.initLayout(descLayouts);
    }

    {
        auto vertexInput   = getVertexInputState();
        DynamicStateDefault();
        auto viewportState = getViewportState();
        auto inputAssembly = Pipeline<VoxelParam>::inputAssemblyDefault();
        auto rasterization = getRasterizationState(true);
        auto multisample   = Pipeline<VoxelParam>::multisampleDefault();

        JSON_GET(RenderGraphConfiguration, rg_cfg, cfg, "render_graph");
        auto vertShaderCode    = readFile(rg_cfg.shader_directory + "/voxelization/voxelization.vert.spv");
        auto fragShaderCode    = readFile(rg_cfg.shader_directory + "/voxelization/voxelization.frag.spv");
        auto vertShaderModule  = createShaderModule(g_ctx.vk, vertShaderCode);
        auto fragShaderModule  = createShaderModule(g_ctx.vk, fragShaderCode);
        std::vector shaderStages = {
            Pipeline<VoxelParam>::shaderStageDefault(vertShaderModule, VK_SHADER_STAGE_VERTEX_BIT),
            Pipeline<VoxelParam>::shaderStageDefault(fragShaderModule, VK_SHADER_STAGE_FRAGMENT_BIT),
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
        pipelineInfo.layout              = voxel_pipeline.layout;
        pipelineInfo.renderPass          = render_pass;
        pipelineInfo.subpass             = 0;
        if (vkCreateGraphicsPipelines(g_ctx.vk.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &voxel_pipeline.pipeline) != VK_SUCCESS) {
            throw std::runtime_error("failed to create voxelization pipeline!");
        }
        vkDestroyShaderModule(g_ctx.vk.device, vertShaderModule, nullptr);
        vkDestroyShaderModule(g_ctx.vk.device, fragShaderModule, nullptr);
    }

    {
        voxel_pipeline.param.voxelizationViewMat  = g_ctx.dm.getResourceHandle(view_mat_buffer.id);
        voxel_pipeline.param.voxelizationProjMats = g_ctx.dm.getResourceHandle(proj_mats_buffer.id);
        voxel_pipeline.param_buf                  = Buffer::New(
            g_ctx.vk,
            sizeof(VoxelParam),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            true
        );
        voxel_pipeline.param_buf.Update(g_ctx.vk, &voxel_pipeline.param, sizeof(VoxelParam));
        g_ctx.dm.registerParameter(voxel_pipeline.param_buf);
    }
}

void Voxelization::createVelocityRecordPipeline(Configuration &cfg)
{
    {
        std::vector<VkDescriptorSetLayout> descLayouts = {
            g_ctx.dm.BINDLESS_LAYOUT(),
            g_ctx.dm.PARAMETER_LAYOUT(),
            g_ctx.dm.PARAMETER_LAYOUT()
        };
        velocity_pipeline.initLayout(descLayouts);
    }

    {
        auto vertexInput   = getVertexInputState();
        DynamicStateDefault();
        auto viewportState = getViewportState();
        auto inputAssembly = Pipeline<VelocityParam>::inputAssemblyDefault();
        auto rasterization = getRasterizationState(true);
        auto multisample   = Pipeline<VelocityParam>::multisampleDefault();

        JSON_GET(RenderGraphConfiguration, rg_cfg, cfg, "render_graph");
        auto vertShaderCode    = readFile(rg_cfg.shader_directory + "/voxelization/velocity.vert.spv");
        auto geomShaderCode    = readFile(rg_cfg.shader_directory + "/voxelization/velocity.geom.spv");
        auto fragShaderCode    = readFile(rg_cfg.shader_directory + "/voxelization/velocity.frag.spv");
        auto vertShaderModule  = createShaderModule(g_ctx.vk, vertShaderCode);
        auto geomShaderModule  = createShaderModule(g_ctx.vk, geomShaderCode);
        auto fragShaderModule  = createShaderModule(g_ctx.vk, fragShaderCode);
        std::vector shaderStages = {
            Pipeline<VelocityParam>::shaderStageDefault(vertShaderModule, VK_SHADER_STAGE_VERTEX_BIT),
            Pipeline<VelocityParam>::shaderStageDefault(geomShaderModule, VK_SHADER_STAGE_GEOMETRY_BIT),
            Pipeline<VelocityParam>::shaderStageDefault(fragShaderModule, VK_SHADER_STAGE_FRAGMENT_BIT)
        };

        VkPipelineColorBlendAttachmentState colorBlendAttachment {};
        colorBlendAttachment.colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachment.blendEnable         = VK_FALSE;
        VkPipelineColorBlendStateCreateInfo colorBlending {};
        colorBlending.sType            = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.logicOpEnable    = VK_FALSE;
        colorBlending.attachmentCount  = 1;
        colorBlending.pAttachments     = &colorBlendAttachment;

        VkPipelineDepthStencilStateCreateInfo depthStencil {};
        depthStencil.sType             = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable   = VK_FALSE;
        depthStencil.depthWriteEnable  = VK_FALSE;
        depthStencil.stencilTestEnable = VK_FALSE;

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
        pipelineInfo.layout              = velocity_pipeline.layout;
        pipelineInfo.renderPass          = render_pass;
        pipelineInfo.subpass             = 1;
        if (vkCreateGraphicsPipelines(g_ctx.vk.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &velocity_pipeline.pipeline) != VK_SUCCESS) {
            throw std::runtime_error("failed to create vertices position feedback pipeline!");
        }
        vkDestroyShaderModule(g_ctx.vk.device, vertShaderModule, nullptr);
        vkDestroyShaderModule(g_ctx.vk.device, geomShaderModule, nullptr);
        vkDestroyShaderModule(g_ctx.vk.device, fragShaderModule, nullptr);
    }

    {
        velocity_pipeline.param.projSpacePixDim      = glm::vec2(1.f / static_cast<float>(config.dimension[0]), 1.f / static_cast<float>(config.dimension[2]));
        velocity_pipeline.param.deltaT               = 0.00555f; // default value, will be updated every frame
        velocity_pipeline.param.voxelizationViewMat  = g_ctx.dm.getResourceHandle(view_mat_buffer.id);
        velocity_pipeline.param.voxelizationProjMats = g_ctx.dm.getResourceHandle(proj_mats_buffer.id);

        velocity_pipeline.param_buf = Buffer::New(
            g_ctx.vk,
            sizeof(VelocityParam),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            true
        );
        velocity_pipeline.param_buf.Update(g_ctx.vk, &velocity_pipeline.param, sizeof(VelocityParam));
        g_ctx.dm.registerParameter(velocity_pipeline.param_buf);
    }
}

void Voxelization::createVertexPosPipeline(Configuration &cfg)
{
    {
        std::vector<VkDescriptorSetLayout> descLayouts = {
            g_ctx.dm.PARAMETER_LAYOUT(),
        };
        vertex_pos_pipeline.initLayout(descLayouts);
    }

    {
        auto vertexInput   = getVertexInputState();
        DynamicStateDefault();
        auto viewportState = getViewportState();
        auto inputAssembly = Pipeline<EmptyParam>::inputAssemblyDefault();
        auto rasterization = getRasterizationState(false);
        auto multisample   = Pipeline<EmptyParam>::multisampleDefault();

        JSON_GET(RenderGraphConfiguration, rg_cfg, cfg, "render_graph");
        auto vertShaderCode    = readFile(rg_cfg.shader_directory + "/voxelization/vertexPos.vert.spv");
        auto vertShaderModule  = createShaderModule(g_ctx.vk, vertShaderCode);
        std::vector shaderStages = {
            Pipeline<EmptyParam>::shaderStageDefault(vertShaderModule, VK_SHADER_STAGE_VERTEX_BIT),
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
        depthStencil.stencilTestEnable = VK_FALSE;

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
        pipelineInfo.layout              = vertex_pos_pipeline.layout;
        pipelineInfo.renderPass          = render_pass;
        pipelineInfo.subpass             = 2;
        if (vkCreateGraphicsPipelines(g_ctx.vk.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &vertex_pos_pipeline.pipeline) != VK_SUCCESS) {
            throw std::runtime_error("failed to create vertices position feedback pipeline!");
        }
        vkDestroyShaderModule(g_ctx.vk.device, vertShaderModule, nullptr);
    }
}

void Voxelization::setViewportAndScissor()
{
    VkViewport viewport {};
    viewport.x        = 0.0f;
    viewport.y        = 0.0f;
    viewport.width    = static_cast<float>(config.dimension[0]);
    viewport.height   = static_cast<float>(config.dimension[2]);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(g_ctx.vk.commandBuffer, 0, 1, &viewport);

    VkRect2D scissor {};
    scissor.offset = { 0, 0 };
    scissor.extent = { config.dimension[0], config.dimension[2] };
    vkCmdSetScissor(g_ctx.vk.commandBuffer, 0, 1, &scissor);
}

void Voxelization::updateTime() {
    velocity_pipeline.param.deltaT = g_ctx.frame_time;
    //INFO_ALL("Delt time: " + std::to_string(velocity_pipeline.param.deltaT) + "s");
    velocity_pipeline.param_buf.Update(g_ctx.vk, &velocity_pipeline.param, sizeof(VelocityParam));
}

void Voxelization::record(uint32_t swapchain_index)
{
    updateTime();
    setViewportAndScissor();

    std::array<VkClearValue, 2> clearValues {};
    clearValues[0].depthStencil = { 1.0f, 0 };
    clearValues[1].color = { 0.0f, 0.0f, 0.0f, 1.0f };
    VkRenderPassBeginInfo renderPassInfo {};
    renderPassInfo.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass        = render_pass;
    renderPassInfo.framebuffer       = framebuffers[swapchain_index];
    renderPassInfo.renderArea.offset = { 0, 0 };
    renderPassInfo.renderArea.extent = { config.dimension[0], config.dimension[2] };
    renderPassInfo.clearValueCount   = clearValues.size();
    renderPassInfo.pClearValues      = clearValues.data();
    vkCmdBeginRenderPass(g_ctx.vk.commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    // Subpass 0, mesh voxelization
    vkCmdBindPipeline(g_ctx.vk.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, voxel_pipeline.pipeline);
    bindDescriptorSet(0, voxel_pipeline.layout, g_ctx.dm.BINDLESS_SET());
    bindDescriptorSet(1, voxel_pipeline.layout, g_ctx.dm.getParameterSet(voxel_pipeline.param_buf.id));

    for (int i = 0; i < g_ctx.rm->objects.size(); ++i) {
        const Object& obj = g_ctx.rm->objects[i];
        const Mesh& mesh = g_ctx.rm->meshes[obj.mesh];
        if (!mesh.isWaterTight) // This voxelization method only apply to watertight mesh, exclude scene boundary meshes
            continue;

        bindDescriptorSet(2, voxel_pipeline.layout, g_ctx.dm.getParameterSet(obj.paramBuffer.id));
        constexpr VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(g_ctx.vk.commandBuffer, 0, 1, &mesh.vertexBuffer.buffer, offsets);
        vkCmdBindIndexBuffer(g_ctx.vk.commandBuffer, mesh.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(g_ctx.vk.commandBuffer, mesh.data.indices.size(), config.dimension[1], 0, 0, 0);
    }

    // Subpass 1, each objects writes its velocity to the velocity texture
    // This is not a good pattern for using subpass since these two subpasses don't share any
    // on-chip memory resource. However, the bottleneck is in the simulation side so we can keep the current
    // design to achieve minimum modifications of the render engine for implementing voxelization.
    vkCmdNextSubpass(g_ctx.vk.commandBuffer, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(g_ctx.vk.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, velocity_pipeline.pipeline);
    bindDescriptorSet(1, velocity_pipeline.layout, g_ctx.dm.getParameterSet(velocity_pipeline.param_buf.id));

    for (int i = 0; i < g_ctx.rm->objects.size(); ++i) {
        const Object& obj = g_ctx.rm->objects[i];
        const Mesh& mesh = g_ctx.rm->meshes[obj.mesh];
        if (!mesh.isWaterTight) // This voxelization method only apply to watertight mesh, exclude scene boundary meshes
            continue;

        bindDescriptorSet(2, velocity_pipeline.layout, g_ctx.dm.getParameterSet(obj.paramBuffer.id));
        constexpr VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(g_ctx.vk.commandBuffer, 0, 1, &mesh.vertexBuffer.buffer, offsets);
        vkCmdBindIndexBuffer(g_ctx.vk.commandBuffer, mesh.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(g_ctx.vk.commandBuffer, mesh.data.indices.size(), config.dimension[1], 0, 0, 0);
    }

    // Subpass 2, each object writes its vertices positions to object's own position buffer.
    // This is not a good pattern for using subpass since these two subpasses don't share any
    // on-chip memory resource. And the way it uses the transform feedback is also inoptimal
    // since it does not use any indirect draw commands but issuing of a bunch of commands in
    // CPU side instead. However, the bottleneck is in the simulation side so we can keep the current
    // design to achieve minimum modifications of the render engine for implementing voxelization.
    vkCmdNextSubpass(g_ctx.vk.commandBuffer, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(g_ctx.vk.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vertex_pos_pipeline.pipeline);

    for (int i = 0; i < g_ctx.rm->objects.size(); ++i) {
        const Object& obj = g_ctx.rm->objects[i];
        const Mesh& mesh = g_ctx.rm->meshes[obj.mesh];
        if (!mesh.isWaterTight) // This voxelization method only apply to watertight mesh, exclude scene boundary meshes
            continue;

        bindDescriptorSet(0, vertex_pos_pipeline.layout, g_ctx.dm.getParameterSet(obj.paramBuffer.id));
        constexpr VkDeviceSize offsets[] = { 0 };
        fpCmdBindTransformFeedbackBuffersEXTHandle(g_ctx.vk.commandBuffer, 0, 1, &vert_pos_buffers[i].buffer, offsets, nullptr);
        fpCmdBeginTransformFeedbackEXTHandle(g_ctx.vk.commandBuffer, 0, 0, nullptr, nullptr);

        vkCmdBindVertexBuffers(g_ctx.vk.commandBuffer, 0, 1, &mesh.vertexBuffer.buffer, offsets);
        vkCmdBindIndexBuffer(g_ctx.vk.commandBuffer, mesh.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(g_ctx.vk.commandBuffer, mesh.data.indices.size(), 1, 0, 0, 0);

        fpCmdEndTransformFeedbackEXTHandle(g_ctx.vk.commandBuffer, 0, 0, nullptr, nullptr);
    }

    vkCmdEndRenderPass(g_ctx.vk.commandBuffer);

    // Copy the stencil image to voxel image for CUDA readback
    attachments->getAttachment("stencil").CopyTo(g_ctx.vk, staging_buffer, VK_IMAGE_ASPECT_STENCIL_BIT);
    staging_buffer.Barrier(g_ctx.vk, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);
    staging_buffer.CopyTo(g_ctx.vk, voxel_tex, { config.dimension[0], config.dimension[2], config.dimension[1] });
}

void Voxelization::onResize()
{
    // Do nothing since the voxel texture is fixed size
}

void Voxelization::destroy()
{
    voxel_pipeline.destroy();
    vkDestroyRenderPass(g_ctx.vk.device, render_pass, nullptr);
    for (auto& framebuffer : framebuffers) {
        vkDestroyFramebuffer(g_ctx.vk.device, framebuffer, nullptr);
    }
    Buffer::Delete(g_ctx.vk, view_mat_buffer);
    Buffer::Delete(g_ctx.vk, proj_mats_buffer);
    for (Buffer& buffer : vert_pos_buffers)
        Buffer::Delete(g_ctx.vk, buffer);
}

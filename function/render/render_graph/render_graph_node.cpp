#include "render_graph_node.h"
#include "function/global_context.h"

RenderGraphNode::RenderGraphNode(const std::string& name)
    : name(name)
{
}

VkRenderPass RenderGraphNode::DefaultRenderPass(
    const std::unordered_map<std::string, RenderAttachmentDescription>& attachment_descriptions,
    const std::vector<AttachmentDescriptionHelper>& desc,
    const VkSubpassDependency& dependency)
{
    std::vector<VkAttachmentDescription> attachments;
    for (const auto& d : desc) {
        bool isStencilAttachment = static_cast<uint8_t>(attachment_descriptions.at(d.name).type & RenderAttachmentType::Stencil) != 0;
        attachments.push_back(
            {
                .format         = attachment_descriptions.at(d.name).format,
                .samples        = VK_SAMPLE_COUNT_1_BIT,
                .loadOp         = isStencilAttachment ? VK_ATTACHMENT_LOAD_OP_DONT_CARE : d.load_op,
                .storeOp        = isStencilAttachment ? VK_ATTACHMENT_STORE_OP_DONT_CARE : d.store_op,
                .stencilLoadOp  = isStencilAttachment ? d.load_op : VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                .stencilStoreOp = isStencilAttachment ? d.store_op : VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .initialLayout  = attachment_descriptions.at(d.name).layout,
                .finalLayout    = attachment_descriptions.at(d.name).layout,
            });
    }

    bool has_depth_stencil = false;
    std::vector<VkAttachmentReference> colorAttachmentRefs {};
    VkAttachmentReference depthAttachmentRef {};
    for (const auto& d : desc) {
        if (static_cast<uint8_t>(attachment_descriptions.at(d.name).type & RenderAttachmentType::Depth) != 0 ||
            static_cast<uint8_t>(attachment_descriptions.at(d.name).type & RenderAttachmentType::Stencil) != 0)
        {
            assert(!has_depth_stencil);
            has_depth_stencil  = true;
            depthAttachmentRef = {
                .attachment = static_cast<uint32_t>(colorAttachmentRefs.size()),
                .layout     = attachment_descriptions.at(d.name).layout,
            };
        } else {
            colorAttachmentRefs.push_back(
                {
                    .attachment = static_cast<uint32_t>(colorAttachmentRefs.size()),
                    .layout     = attachment_descriptions.at(d.name).layout,
                });
        }
    }

    VkSubpassDescription subpass    = {};
    subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount    = static_cast<uint32_t>(colorAttachmentRefs.size());
    subpass.pColorAttachments       = colorAttachmentRefs.data();
    subpass.pDepthStencilAttachment = has_depth_stencil ? &depthAttachmentRef : nullptr;

    VkRenderPass render_pass;
    VkRenderPassCreateInfo renderPassInfo {};
    renderPassInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = attachments.size();
    renderPassInfo.pAttachments    = attachments.data();
    renderPassInfo.subpassCount    = 1;
    renderPassInfo.pSubpasses      = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies   = &dependency;
    if (vkCreateRenderPass(g_ctx.vk.device, &renderPassInfo, nullptr, &render_pass) != VK_SUCCESS) {
        throw std::runtime_error("failed to create render pass!");
    }
    return render_pass;
}

VkRenderPass RenderGraphNode::MultiSubpassRenderPass(
    const std::unordered_map<std::string, RenderAttachmentDescription> &attachment_info_map,
    const std::vector<AttachmentDescriptionHelper> &attachment_configs,
    const std::vector<VkSubpassDependency> &dependencies)
{
    std::vector<VkAttachmentDescription> attachments;
    std::unordered_map<std::string, uint32_t> attachment_name_to_index;

    for (const auto& config : attachment_configs) {
        attachment_name_to_index[config.name] = attachment_name_to_index.size();
        const auto& info = attachment_info_map.at(config.name);
        bool is_stencil = static_cast<uint8_t>(info.type & RenderAttachmentType::Stencil) != 0;

        attachments.push_back({
            .format         = info.format,
            .samples        = VK_SAMPLE_COUNT_1_BIT,
            .loadOp         = is_stencil ? VK_ATTACHMENT_LOAD_OP_DONT_CARE : config.load_op,
            .storeOp        = is_stencil ? VK_ATTACHMENT_STORE_OP_DONT_CARE : config.store_op,
            .stencilLoadOp  = is_stencil ? config.load_op : VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = is_stencil ? config.store_op : VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout  = info.layout,
            .finalLayout    = info.layout,
        });
    }

    uint32_t max_subpass_idx = 0;
    uint32_t subpass_count = 0;
    // subpass count is the max(dependency's dstSubpass, attachment's subpass) + 1
    if (!attachment_configs.empty()) {
        for (const auto& config : attachment_configs) {
            max_subpass_idx = std::max(max_subpass_idx, attachment_info_map.at(config.name).subpass);
        }
    }
    for (const auto& dependency : dependencies) {
        max_subpass_idx = std::max(max_subpass_idx, dependency.dstSubpass);
    }
    subpass_count = max_subpass_idx + 1;

    // Group attachment references by subpass
    std::vector<std::vector<VkAttachmentReference>> subpass_color_refs(subpass_count);
    //std::vector<std::vector<VkAttachmentReference>> subpass_input_refs(subpass_count);
    std::vector<std::optional<VkAttachmentReference>> subpass_depth_stencil_ref(subpass_count);

    for (const auto& config : attachment_configs) {
        const auto& info = attachment_info_map.at(config.name);

        assert(attachment_name_to_index.contains(config.name));
        VkAttachmentReference ref = {
            .attachment = attachment_name_to_index.at(config.name),
            .layout     = info.layout
        };

        bool is_depth_stencil =
            (static_cast<uint8_t>(info.type & RenderAttachmentType::Depth) != 0) ||
            (static_cast<uint8_t>(info.type & RenderAttachmentType::Stencil) != 0);

        if (info.rw == RenderAttachmentRW::Read) {
            // TODO: Add input attachment support if needed
        } else {
            if (is_depth_stencil) {
                assert(!subpass_depth_stencil_ref[info.subpass].has_value());
                subpass_depth_stencil_ref[info.subpass] = ref;
            } else {
                subpass_color_refs[info.subpass].push_back(ref);
            }
        }
    }

    // Create a VkSubpassDescription for each subpass
    std::vector<VkSubpassDescription> subpasses;
    subpasses.reserve(subpass_count);

    for (uint32_t i = 0; i < subpass_count; i++) {
        VkSubpassDescription subpass_desc {};
        subpass_desc.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass_desc.pColorAttachments       = subpass_color_refs[i].empty() ? nullptr : subpass_color_refs[i].data();
        subpass_desc.colorAttachmentCount    = subpass_color_refs[i].size();
        subpass_desc.pDepthStencilAttachment = subpass_depth_stencil_ref[i].has_value() ? &subpass_depth_stencil_ref[i].value() : nullptr;

        subpasses.push_back(subpass_desc);
    }

    VkRenderPass render_pass;
    VkRenderPassCreateInfo render_pass_info {};
    render_pass_info.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    render_pass_info.attachmentCount = attachments.size();
    render_pass_info.pAttachments    = attachments.data();
    render_pass_info.subpassCount    = subpasses.size();
    render_pass_info.pSubpasses      = subpasses.data();
    render_pass_info.dependencyCount = dependencies.size();
    render_pass_info.pDependencies   = dependencies.data();

    if (vkCreateRenderPass(g_ctx.vk.device, &render_pass_info, nullptr, &render_pass) != VK_SUCCESS) {
        throw std::runtime_error("failed to create render pass!");
    }
    return render_pass;
}

void RenderGraphNode::bindDescriptorSet(uint32_t index, VkPipelineLayout layout, VkDescriptorSet* set)
{
    vkCmdBindDescriptorSets(
        g_ctx.vk.commandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        layout,
        index,
        1,
        set,
        0,
        nullptr);
}

void RenderGraphNode::setDefaultViewportAndScissor()
{
    VkViewport viewport {};
    viewport.x        = 0.0f;
    viewport.y        = 0.0f;
    viewport.width    = static_cast<float>(g_ctx.vk.swapChainImages[0]->extent.width);
    viewport.height   = static_cast<float>(g_ctx.vk.swapChainImages[0]->extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(g_ctx.vk.commandBuffer, 0, 1, &viewport);
    VkRect2D scissor {};
    scissor.offset = { 0, 0 };
    scissor.extent = Vk::toVkExtent2D(g_ctx.vk.swapChainImages[0]->extent);
    vkCmdSetScissor(g_ctx.vk.commandBuffer, 0, 1, &scissor);
}

Vk::Image* RenderGraphNode::getAttachmentByName(const std::string& name, RenderAttachments* attachments, int swapchain_index)
{
    if (name == RenderAttachmentDescription::SWAPCHAIN_IMAGE_NAME()) {
        return g_ctx.vk.swapChainImages[swapchain_index].get();
    }
    return &(attachments->getAttachment(name));
}

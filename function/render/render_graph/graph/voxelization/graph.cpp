#include "graph.h"

void VoxelizationGraph::init(Configuration& cfg)
{
    nodes["DefaultObject"]
        = std::move(std::make_unique<DefaultObject>("DefaultObject", "object_color", "depth"));
    nodes["Voxelization"]
        = std::move(std::make_unique<Voxelization>("Voxelization", "voxel", "velocity", cfg));
    nodes["HDRToSDR"]
        = std::move(std::make_unique<HDRToSDR>("HDRToSDR", "object_color", "sdr_buf"));
    nodes["CalculateLuminance"]
        = std::move(std::make_unique<CalculateLuminance>("CalculateLuminance", "sdr_buf", "sdr_buf_alpha_illuminance"));
    nodes["FXAA"]
        = std::move(std::make_unique<FXAANode>("FXAA", "sdr_buf_alpha_illuminance", RenderAttachmentDescription::SWAPCHAIN_IMAGE_NAME()));
    nodes["UI"]
        = std::move(std::make_unique<UI>("UI", RenderAttachmentDescription::SWAPCHAIN_IMAGE_NAME(), fn));
    initAttachments();

    for (auto& node : nodes) {
        node.second->init(cfg, attachments);
    }

    graph = {
        { "HDRToSDR", { "DefaultObject" } },
        { "CalculateLuminance", { "HDRToSDR" } },
        { "FXAA", { "CalculateLuminance" } },
        { "UI", { "FXAA" } },
    };
    initGraph();
}

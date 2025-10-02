#include "object.h"
#include "core/tool/logger.h"
#include "core/vulkan/vulkan_util.h"
#include "function/global_context.h"
#include "function/resource_manager/resource_manager.h"
#include <glm/gtc/type_ptr.hpp>

using namespace Vk;

void Object::destroy()
{
    Buffer::Delete(g_ctx.vk, paramBuffer);
}

Object Object::fromConfiguration(ObjectConfiguration& config)
{
    Object obj;
    obj.name = config.name;
    obj.mesh = config.mesh;

    obj.transform = Transform(
        glm::make_vec3(config.initial_position.data()),
        glm::radians(glm::make_vec3(config.initial_rotation.data())),
        glm::make_vec3(config.initial_scale.data()),
        glm::make_vec3(config.angular_velocity.data())
    );

    obj.param.material = g_ctx.dm.getResourceHandle(g_ctx.rm->materials[config.material].buffer.id);
    obj.param.model    = obj.transform.get_matrix();
    obj.paramBuffer    = Buffer::New(
        g_ctx.vk,
        sizeof(Param),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        true);
    obj.paramBuffer.Update(g_ctx.vk, &obj.param, sizeof(Param));
    g_ctx.dm.registerParameter(obj.paramBuffer);

    return obj;
}

#ifdef _WIN64
HANDLE Object::getVkVertexMemHandle()
{
    HANDLE handle;
    VkMemoryGetWin32HandleInfoKHR vkMemoryGetWin32HandleInfoKHR = {};
    vkMemoryGetWin32HandleInfoKHR.sType                         = VK_STRUCTURE_TYPE_MEMORY_GET_WIN32_HANDLE_INFO_KHR;
    vkMemoryGetWin32HandleInfoKHR.memory                        = g_ctx.rm->meshes[mesh].vertexBuffer.memory;
    vkMemoryGetWin32HandleInfoKHR.handleType                    = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;

    fpGetMemoryWin32Handle(g_ctx.vk.device, &vkMemoryGetWin32HandleInfoKHR, &handle);
    return handle;
}
#else
int Object::getVkVertexMemHandle()
{
    int fd;
    VkMemoryGetFdInfoKHR vkMemoryGetFdInfoKHR = {};
    vkMemoryGetFdInfoKHR.sType                = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR;
    vkMemoryGetFdInfoKHR.memory               = g_ctx.rm->meshes[mesh].vertexBuffer.memory;
    vkMemoryGetFdInfoKHR.handleType           = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;

    fpGetMemoryFdKHR(g_ctx.vk.device, &vkMemoryGetFdInfoKHR, &fd);
    return fd;
}
#endif

void Object::updatePosition(float delta_time)
{
    transform.update(delta_time);

    param.model = transform.get_matrix();
    param.modelInvTrans = glm::transpose(glm::inverse(param.model));
    paramBuffer.Update(g_ctx.vk, &param, sizeof(Param));
}
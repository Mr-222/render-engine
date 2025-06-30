#include "resource_manager.h"
#include "function/global_context.h"

using namespace Vk;

void ResourceManager::load(Configuration& config)
{
    this->config = config;

    JSON_GET(CameraConfiguration, camera_cfg, config, "camera");
    camera = Camera::fromConfiguration(camera_cfg);
    JSON_GET(std::vector<LightConfiguration>, lights_cfg, config, "lights");
    lights = Lights::fromConfiguration(lights_cfg);

    JSON_GET(std::vector<MeshConfiguration>, mesh_cfg, config, "meshes");
    for (auto& cfg : mesh_cfg) {
        auto mesh         = Mesh::fromConfiguration(cfg);
        meshes[mesh.name] = mesh;
    }

    loadDefaultTextures();
    JSON_GET(std::vector<TextureConfiguration>, texture_cfg, config, "textures");
    for (auto& cfg : texture_cfg) {
        auto texture           = Texture::fromConfiguration(cfg);
        textures[texture.name] = texture;
    }

    JSON_GET(std::vector<MaterialConfiguration>, material_cfg, config, "materials");
    for (auto& cfg : material_cfg) {
        auto material            = Material::fromConfiguration(cfg);
        materials[material.name] = material;
    }

    json fields_json = config["fields"];
    if (!fields_json.is_null()) {
        FieldsConfiguration fields_cfg = std::move(fields_json.get<FieldsConfiguration>());
        fields                         = Fields::fromConfiguration(fields_cfg);
    }

    JSON_GET(std::vector<ObjectConfiguration>, objects_cfg, config, "objects");
    for (auto& cfg : objects_cfg) {
        objects.emplace_back(Object::fromConfiguration(cfg));
    }

    inlet_angle = config["lfm"]["inlet_angle"];
    recorder.init(config);
}

void ResourceManager::addResource(std::unique_ptr<Resource> resource)
{
    if (resources.find(resource->name) != resources.end()) {
        throw std::runtime_error("resource already exists: " + resource->name);
    }
    resources[resource->name] = std::move(resource);
}

void ResourceManager::removeResource(const std::string& name)
{
    auto it = resources.find(name);
    if (it == resources.end()) {
        throw std::runtime_error("resource not found: " + name);
    }
    it->second->destroy();
    resources.erase(name);
}

void ResourceManager::cleanup()
{
    for (auto& resource : resources) {
        resource.second->destroy();
    }

    recorder.destroy();

    camera.destroy();
    lights.destroy();
    for (auto& mesh : meshes) {
        mesh.second.destroy();
    }
    for (auto& mat : materials) {
        mat.second.destroy();
    }
    for (auto& texture : textures) {
        texture.second.destroy();
    }
    for (auto& object : objects) {
        object.destroy();
    }

    json fields_cfg = config["fields"];
    if (!fields_cfg.is_null()) {
        fields.destroy();
    }
}

void ResourceManager::loadDefaultTextures()
{
    textures["default_color"]     = Texture::loadDefaultColorTexture();
    textures["default_metallic"]  = Texture::loadDefaultMetallicTexture();
    textures["default_roughness"] = Texture::loadDefaultRoughnessTexture();
    textures["default_normal"]    = Texture::loadDefaultNormalTexture();
    textures["default_ao"]        = Texture::loadDefaultAoTexture();
}

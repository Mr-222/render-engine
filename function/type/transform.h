#pragma once

#define GLM_ENABLE_EXPERIMENTAL

#include <glm/glm.hpp>
#include <glm/gtx/quaternion.hpp>

struct Transform {
    glm::vec3 position;
    glm::quat rotation;
    glm::vec3 scale;
    glm::vec3 angular_velocity;

    Transform();
    Transform(const glm::vec3& position, const glm::vec3& euler_angles, const glm::vec3& scale, const glm::vec3& angular_velocity = glm::vec3(0.0f));

    void rotate(float delta_time);
    void update(float delta_time);
    glm::mat4 get_matrix() const;
};

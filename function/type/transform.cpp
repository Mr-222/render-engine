#include "transform.h"

Transform::Transform()
    : position(0.0f, 0.0f, 0.0f),
      rotation(glm::quat(1.0f, 0.0f, 0.0f, 0.0f)),
      scale(1.0f, 1.0f, 1.0f),
      angular_velocity(0.0f, 0.0f, 0.0f)
{}

Transform::Transform(const glm::vec3 &position, const glm::vec3 &euler_angles, const glm::vec3 &scale, const glm::vec3 &angular_velocity)
    : position(position),
      rotation(glm::quat(euler_angles)),
      scale(scale),
      angular_velocity(glm::radians(angular_velocity))
{}

void Transform::rotate(float delta_time) {
    if (glm::length(angular_velocity) > 0.0f) {
        const float angle = glm::length(angular_velocity) * delta_time;
        glm::vec3 axis = glm::normalize(angular_velocity);
        const auto delta_rot = glm::angleAxis(angle, axis);
        rotation = delta_rot * rotation;
        rotation = glm::normalize(rotation);
    }
}

void Transform::update(float delta_time) {
    rotate(delta_time);
}

glm::mat4 Transform::get_matrix() const {
    const glm::mat4 trans = glm::translate(glm::mat4(1.0f), position);
    const glm::mat4 rot   = glm::toMat4(rotation);
    const glm::mat4 scl   = glm::scale(glm::mat4(1.0f), scale);
    return trans * rot * scl;
}
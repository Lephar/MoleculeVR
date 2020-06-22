#version 460 core
#extension GL_ARB_separate_shader_objects : enable

layout(binding = 0) uniform Transform {
    mat4 model;
    mat4 left;
    mat4 right;
    mat4 proj;
} transform;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;

layout(location = 0) out vec3 fragColor;

layout (constant_id = 0) const float eyeConstant = 0.0f;

void main() {
    if (eyeConstant < 0.0f) {
        gl_Position = transform.proj * transform.left * transform.model * vec4(inPosition, 1.0);
    }

    if (eyeConstant > 0.0f) {
        gl_Position = transform.proj * transform.right * transform.model * vec4(inPosition, 1.0);
    }

    fragColor = inColor;
}

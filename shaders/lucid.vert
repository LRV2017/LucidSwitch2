#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in uint inPackedColor;
layout(location = 2) in vec2 inUv;

layout(push_constant) uniform FrameConstants {
    vec2 targetSize;
} frame;

layout(location = 0) out vec4 vertexColor;
layout(location = 1) out vec2 uv;

void main() {
    vec2 ndc = vec2(
        (inPosition.x / frame.targetSize.x) * 2.0 - 1.0,
        1.0 - (inPosition.y / frame.targetSize.y) * 2.0
    );
    gl_Position = vec4(ndc, inPosition.z, 1.0);

    vertexColor = vec4(
        float((inPackedColor >> 24u) & 0xFFu),
        float((inPackedColor >> 16u) & 0xFFu),
        float((inPackedColor >> 8u) & 0xFFu),
        float(inPackedColor & 0xFFu)
    ) / 255.0;
    uv = inUv;
}

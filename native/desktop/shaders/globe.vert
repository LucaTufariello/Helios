#version 440
layout(location = 0) in vec3 position;
layout(location = 1) in vec2 uv;
layout(location = 2) in vec2 surfaceUV;
layout(location = 0) out vec2 textureUV;
layout(location = 1) out vec2 tileUV;
layout(std140, binding = 0) uniform Scene {
    mat4 viewProjection;
    vec4 originOpacity;
    vec4 tint;
    vec4 cutoutInfo;
    vec4 cutouts[96];
};
void main() {
    textureUV = uv;
    tileUV = surfaceUV;
    gl_Position = viewProjection * vec4(position + originOpacity.xyz, 1.0);
}

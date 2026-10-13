#version 440
layout(location = 0) in vec2 textureUV;
layout(location = 1) in vec2 tileUV;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform Scene {
    mat4 viewProjection;
    vec4 originOpacity;
    vec4 tint;
    vec4 cutoutInfo;
    vec4 cutouts[96];
};
layout(binding = 1) uniform sampler2D image;
void main() {
    for (int i = 0; i < int(cutoutInfo.x); ++i) {
        vec4 rectangle = cutouts[i];
        if (all(greaterThanEqual(tileUV, rectangle.xy)) && all(lessThan(tileUV, rectangle.zw)))
            discard;
    }
    if (cutoutInfo.y > 0.5 && (any(lessThan(textureUV,vec2(0))) || any(greaterThan(textureUV,vec2(1)))))
        discard;
    vec4 pixel = texture(image, textureUV);
    if (pixel.a < 0.005) discard;
    fragColor = vec4(pixel.rgb * tint.rgb, pixel.a * originOpacity.w * tint.a);
}

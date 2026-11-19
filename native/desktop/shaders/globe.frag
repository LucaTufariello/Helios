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
    if (cutoutInfo.z > 0.5) {
        pixel *= tint;
        if (cutoutInfo.w >= 0.0 && pixel.a < cutoutInfo.w) discard;
        if (cutoutInfo.w < 0.0 && cutoutInfo.z < 1.5) pixel.a = 1.0;
        pixel.rgb = mix(12.92 * pixel.rgb, 1.055 * pow(max(pixel.rgb,vec3(0)),vec3(1.0/2.4)) - 0.055,
                        greaterThan(pixel.rgb,vec3(0.0031308)));
        fragColor = pixel;
        return;
    }
    if (pixel.a < 0.005) discard;
    fragColor = vec4(pixel.rgb * tint.rgb, pixel.a * originOpacity.w * tint.a);
}

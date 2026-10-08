#include <metal_stdlib>
using namespace metal;
struct Vertex { packed_float3 position; packed_float2 uv; };
struct Uniforms { float4x4 viewProjection; float4 color; float4 settings; float4 eye; float4 offset; float4 texTransform; };
struct Raster { float4 position [[position]]; float2 uv; float4 color; float size [[point_size]]; };
vertex Raster tileVertex(const device Vertex* vertices [[buffer(0)]], constant Uniforms& uniforms [[buffer(1)]], uint id [[vertex_id]]) {
    Raster out; out.position = uniforms.viewProjection * float4(float3(vertices[id].position) + uniforms.offset.xyz, 1);
    if (uniforms.settings.y > 0.5) {
        // Segment/ellipsoid occlusion also admits elevated satellites beyond the surface horizon.
        float3 scale = float3(1, 1, 1.00336408982);
        float3 origin = uniforms.eye.xyz * scale;
        float3 direction = float3(vertices[id].position) * scale - origin;
        float t = -dot(origin, direction) / max(dot(direction, direction), 1e-20f);
        float3 closest = origin + t * direction;
        if (t > 0 && t < 1 && dot(closest, closest) < 1) out.position = float4(2, 2, 2, 1);
    }
    out.uv = vertices[id].uv; out.color = uniforms.color; out.size = uniforms.settings.x; return out;
}
fragment float4 tileFragment(Raster in [[stage_in]], texture2d<float> image [[texture(0)]], constant Uniforms& uniforms [[buffer(1)]], const device float4* coverage [[buffer(2)]]) {
    // Georeferenced SAR uses UVs in its own raster grid; outside is transparent.
    if (uniforms.settings.w > 1.5 && (any(in.uv < 0) || any(in.uv > 1))) discard_fragment();
    // A parent surface must not occlude the more detailed children replacing it.
    for (uint i = 0; i < uint(uniforms.settings.z); ++i) {
        float4 rect = coverage[i];
        if (all(in.uv >= rect.xy) && all(in.uv < rect.zw)) discard_fragment();
    }
    constexpr sampler sample(filter::linear, address::clamp_to_edge);
    constexpr sampler grid(filter::nearest, address::clamp_to_edge);
    float2 uv = in.uv * uniforms.texTransform.xy + uniforms.texTransform.zw;
    // Optical pixel-grid display avoids smoothing enlarged source pixels.
    float4 pixel = uniforms.settings.w > 2.5 ? image.sample(grid, uv) : image.sample(sample, uv);
    float luminance = dot(pixel.rgb, float3(0.2126,0.7152,0.0722));
    if (uniforms.eye.w > 2.5) pixel.rgb = float3(clamp(luminance*2.0,0.0,1.0), clamp(2.0-abs(luminance*4.0-2.0),0.0,1.0), clamp(2.0-luminance*4.0,0.0,1.0));
    else if (uniforms.eye.w > 1.5) pixel.rgb = luminance * float3(0.25,1,0.32);
    else if (uniforms.eye.w > 0.5) pixel.rgb = float3(luminance);
    return float4(pixel.rgb, pixel.a * in.color.a);
}
fragment float4 solidFragment(Raster in [[stage_in]]) { return in.color; }
fragment float4 markerFragment(Raster in [[stage_in]], float2 point [[point_coord]]) {
    if (distance(point, float2(0.5)) > 0.5) discard_fragment();
    return in.color;
}

// Constant screen-width ribbons remain readable at every zoom and Retina scale.
struct LineVertex { packed_float3 position; packed_float3 other; float side; };
vertex Raster lineVertex(const device LineVertex* vertices [[buffer(0)]], constant Uniforms& uniforms [[buffer(1)]], uint id [[vertex_id]]) {
    Raster out;
    float4 point = uniforms.viewProjection * float4(float3(vertices[id].position) + uniforms.offset.xyz, 1);
    float4 other = uniforms.viewProjection * float4(float3(vertices[id].other) + uniforms.offset.xyz, 1);
    float2 viewport = max(uniforms.settings.zw, float2(1));
    float2 direction = (other.xy / max(other.w, 1e-8f) - point.xy / max(point.w, 1e-8f)) * viewport;
    float distance = length(direction);
    float2 normal = distance > 1e-6f ? float2(-direction.y, direction.x)/distance : float2(0);
    point.xy += normal * vertices[id].side * uniforms.settings.x / viewport * point.w;
    out.position = point;
    out.uv = float2(0); out.color = uniforms.color; out.size = 1;
    return out;
}

fragment float4 meshFragment(Raster in [[stage_in]], texture2d<float> image [[texture(0)]], sampler sample [[sampler(0)]], constant Uniforms& uniforms [[buffer(1)]]) {
    float4 pixel = image.sample(sample, in.uv) * in.color;
    if (uniforms.settings.x >= 0 && pixel.a < uniforms.settings.x) discard_fragment();
    if (uniforms.settings.x < 0 && uniforms.settings.w < 0.5) pixel.a = 1;
    // glTF base-color textures are sRGB; factors apply in linear light.
    // Raster display uses an unorm target, so encode the final mesh colour explicitly.
    pixel.rgb = select(12.92f * pixel.rgb, 1.055f * pow(max(pixel.rgb, float3(0)), float3(1.0f / 2.4f)) - 0.055f, pixel.rgb > 0.0031308f);
    return pixel;
}

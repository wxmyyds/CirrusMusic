#version 440
// A cover with rounded corners, sampled straight from its image's texture (no offscreen
// layer and mask): PreserveAspectCrop through `crop`, the painted size over the item's.

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec2 boxSize; // item size in logical px
    float radius; // corner radius in logical px
    vec2 crop;    // painted size / item size (1 when the image fits exactly)
};

layout(binding = 1) uniform sampler2D source;

float roundedBox(vec2 p, vec2 halfSize, float r)
{
    vec2 q = abs(p) - halfSize + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}

void main()
{
    vec2 p = (qt_TexCoord0 - 0.5) * boxSize;
    float r = min(radius, min(boxSize.x, boxSize.y) * 0.5);
    float d = roundedBox(p, boxSize * 0.5, r);
    float aa = max(fwidth(d), 1e-4);
    float inside = clamp(0.5 - d / aa, 0.0, 1.0);
    vec2 uv = (qt_TexCoord0 - 0.5) / crop + 0.5;
    fragColor = texture(source, uv) * (inside * qt_Opacity);
}

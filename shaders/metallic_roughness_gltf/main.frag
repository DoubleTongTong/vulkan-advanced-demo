#version 450

layout(set = 0, binding = 0) uniform sampler kMaterialSampler;
layout(set = 0, binding = 1) uniform sampler kIblSampler;
layout(set = 0, binding = 2) uniform texture2D kBaseColorTexture;
layout(set = 0, binding = 3) uniform texture2D kMetallicRoughnessTexture;
layout(set = 0, binding = 4) uniform texture2D kNormalTexture;
layout(set = 0, binding = 5) uniform texture2D kOcclusionTexture;
layout(set = 0, binding = 6) uniform texture2D kEmissiveTexture;
layout(set = 0, binding = 7) uniform texture2D kBrdfLut;
layout(set = 0, binding = 8) uniform textureCube kIrradianceMap;
layout(set = 0, binding = 9) uniform textureCube kGgxEnvironment;

struct PbrFrameData {
    mat4 model;
    mat4 mvp;
    vec4 cameraPosition;
    vec4 baseColorFactor;
    vec4 emissiveFactor;
    vec4 materialFactors;
};

layout(std430, set = 0, binding = 10) readonly buffer FrameData {
    PbrFrameData frames[];
} kFrameData;

layout(push_constant) uniform PushConstants {
    uint frameIndex;
} pc;

layout(location = 0) in vec3 inWorldPosition;
layout(location = 1) in vec3 inWorldNormal;
layout(location = 2) in vec2 inUv;

layout(location = 0) out vec4 outColor;

const float Pi = 3.14159265358979323846;

vec3 fresnelSchlick(float cosTheta, vec3 f0) {
    return f0 + (vec3(1.0) - f0) * pow(1.0 - cosTheta, 5.0);
}

vec3 fresnelSchlickRoughness(float cosTheta, vec3 f0, float roughness) {
    return f0 + (max(vec3(1.0 - roughness), f0) - f0) *
        pow(1.0 - cosTheta, 5.0);
}

float distributionGgx(vec3 normal, vec3 halfVector, float roughness) {
    const float alpha = roughness * roughness;
    const float alphaSquared = alpha * alpha;
    const float nDotHalf = max(dot(normal, halfVector), 0.0);
    const float denominator =
        nDotHalf * nDotHalf * (alphaSquared - 1.0) + 1.0;
    return alphaSquared / max(Pi * denominator * denominator, 1e-6);
}

float geometrySchlickGgx(float nDotDirection, float roughness) {
    const float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
    return nDotDirection /
        max(nDotDirection * (1.0 - k) + k, 1e-6);
}

float geometrySmith(vec3 normal, vec3 view, vec3 light, float roughness) {
    return geometrySchlickGgx(max(dot(normal, view), 0.0), roughness) *
        geometrySchlickGgx(max(dot(normal, light), 0.0), roughness);
}

// DamagedHelmet 没有切线属性，使用屏幕空间导数从位置和 UV 重建 TBN。
vec3 mappedNormal(vec3 geometricNormal, vec3 normalSample, float normalScale) {
    vec3 tangentNormal = normalSample * 2.0 - 1.0;
    tangentNormal.xy *= normalScale;
    tangentNormal = normalize(tangentNormal);

    const vec3 positionDx = dFdx(inWorldPosition);
    const vec3 positionDy = dFdy(inWorldPosition);
    const vec2 uvDx = dFdx(inUv);
    const vec2 uvDy = dFdy(inUv);
    const vec3 perpendicularX = cross(positionDy, geometricNormal);
    const vec3 perpendicularY = cross(geometricNormal, positionDx);
    vec3 tangent = perpendicularX * uvDx.x + perpendicularY * uvDy.x;
    vec3 bitangent = perpendicularX * uvDx.y + perpendicularY * uvDy.y;
    const float scale = inversesqrt(max(max(dot(tangent, tangent), dot(bitangent, bitangent)), 1e-8));
    tangent *= scale;
    bitangent *= scale;
    tangent *= dot(cross(geometricNormal, tangent), bitangent) < 0.0 ? -1.0 : 1.0;
    return normalize(mat3(tangent, bitangent, geometricNormal) * tangentNormal);
}

void main() {
    const PbrFrameData frame = kFrameData.frames[pc.frameIndex];
    const vec4 baseColorSample = texture(
        sampler2D(kBaseColorTexture, kMaterialSampler), inUv);
    const vec3 baseColor = baseColorSample.rgb * frame.baseColorFactor.rgb;
    const vec4 metallicRoughnessSample = texture(
        sampler2D(kMetallicRoughnessTexture, kMaterialSampler), inUv);
    const float perceptualRoughness = clamp(
        metallicRoughnessSample.g * frame.materialFactors.y, 0.04, 1.0);
    const float metallic = clamp(
        metallicRoughnessSample.b * frame.materialFactors.x, 0.0, 1.0);

    vec3 geometricNormal = normalize(inWorldNormal);
    if (!gl_FrontFacing) {
        geometricNormal = -geometricNormal;
    }
    const vec3 normal = mappedNormal(
        geometricNormal,
        texture(sampler2D(kNormalTexture, kMaterialSampler), inUv).xyz,
        frame.materialFactors.z);
    const vec3 view = normalize(frame.cameraPosition.xyz - inWorldPosition);
    const float nDotView = max(dot(normal, view), 0.001);
    const vec3 f0 = mix(vec3(0.04), baseColor, metallic);

    // 一盏固定方向光用于观察直接光照下的 GGX 高光。
    const vec3 light = normalize(vec3(0.5, 1.0, 0.35));
    const vec3 halfVector = normalize(view + light);
    const float nDotLight = max(dot(normal, light), 0.0);
    const vec3 fresnel = fresnelSchlick(max(dot(halfVector, view), 0.0), f0);
    const float distribution = distributionGgx(normal, halfVector, perceptualRoughness);
    const float geometry = geometrySmith(normal, view, light, perceptualRoughness);
    const vec3 specular =
        fresnel * distribution * geometry /
        max(4.0 * nDotView * nDotLight, 1e-5);
    const vec3 directDiffuse =
        (vec3(1.0) - fresnel) * (1.0 - metallic) * baseColor / Pi;
    const vec3 direct = (directDiffuse + specular) * nDotLight * 2.5;

    // IBL：Irradiance 提供漫反射，GGX mip 链和 BRDF LUT 提供镜面反射。
    const vec3 environmentFresnel =
        fresnelSchlickRoughness(nDotView, f0, perceptualRoughness);
    const vec3 diffuseWeight = (vec3(1.0) - environmentFresnel) * (1.0 - metallic);
    const vec3 irradiance = texture(
        samplerCube(kIrradianceMap, kIblSampler), normal).rgb;
    const vec3 diffuseIbl = irradiance * baseColor * diffuseWeight;

    const vec3 reflection = reflect(-view, normal);
    const float maxEnvironmentLod = float(
        textureQueryLevels(samplerCube(kGgxEnvironment, kIblSampler)) - 1);
    const vec3 prefilteredEnvironment = textureLod(
        samplerCube(kGgxEnvironment, kIblSampler),
        reflection,
        perceptualRoughness * maxEnvironmentLod).rgb;
    // BRDF LUT 生成时粗糙度 0 位于图像底部，因此 Y 坐标要反转。
    const vec2 integratedBrdf = texture(
        sampler2D(kBrdfLut, kIblSampler),
        vec2(nDotView, 1.0 - perceptualRoughness)).rg;
    const vec3 specularIbl = prefilteredEnvironment *
        (f0 * integratedBrdf.x + integratedBrdf.y);

    const float sampledAo = texture(
        sampler2D(kOcclusionTexture, kMaterialSampler), inUv).r;
    const float ao = mix(1.0, sampledAo, frame.materialFactors.w);
    const vec3 emissive = texture(
        sampler2D(kEmissiveTexture, kMaterialSampler), inUv).rgb *
        frame.emissiveFactor.rgb;
    vec3 color = direct + (diffuseIbl + specularIbl) * ao + emissive;

    // Swapchain 使用 UNORM，因此在写出前进行简单 tone mapping 和 gamma 编码。
    color = color / (color + vec3(1.0));
    color = pow(max(color, vec3(0.0)), vec3(1.0 / 2.2));
    outColor = vec4(color, 1.0);
}

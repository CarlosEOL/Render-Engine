RaytracingAccelerationStructure scene : register(t0);
StructuredBuffer<float3> positions : register(t1);

struct Material {
    float3 color;
    float reflectivity;
    uint firstVertex;
    uint channels;
    float unitScale;
    uint kind;
};
StructuredBuffer<Material> materials : register(t2);
Texture2D<float4> rasterColor : register(t3);
Texture2D<float> rasterDepth : register(t4);
TextureCube<float4> skyRadiance : register(t5);
Texture2D<float4> waterNormal : register(t6);
SamplerState skySampler : register(s0);
RWTexture2D<float4> result : register(u0);
RWTexture2D<float4> reflectionSamples : register(u1);
RWTexture2D<float4> reflectionGuide : register(u2);

cbuffer View : register(b0) {
    column_major float4x4 projection;
    column_major float4x4 inverseProjection;
    float4 eyeScale;      // Camera position in projection units and metres per unit
    float4 lightRange;
    uint4 region;        // Left, top, width, height
    uint4 options;       // Depth/sky flags, underwater, reflection, refraction
    float4 sky;          // Local zenith and atmospheric contribution
};
cbuffer Pass : register(b1) { uint passIndex; };

static const uint Primary = 1;
static const uint Reflective = 2;
static const uint Transmitted = 4;
static const uint Shadow = 8;
static const uint Indirect = 16;
static const uint Surface = 1;
static const uint Glass = 5;

struct Hit {
    float distance;
    uint geometry;
    uint primitive;
};

Hit Trace(float3 origin, float3 direction, float limit, uint channel) {
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = .015;
    ray.TMax = limit;
    RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> query;
    query.TraceRayInline(scene, channel == Shadow ? RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH : 0, 255, ray);
    while (query.Proceed()) {
        if (query.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE &&
            (materials[query.CandidateGeometryIndex()].channels & channel) != 0)
            query.CommitNonOpaqueTriangleHit();
    }
    Hit hit;
    hit.distance = -1;
    hit.geometry = hit.primitive = 0;
    if (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
        hit.distance = query.CommittedRayT();
        hit.geometry = query.CommittedGeometryIndex();
        hit.primitive = query.CommittedPrimitiveIndex();
    }
    return hit;
}

float3 Normal(Hit hit, float3 incident) {
    const uint offset = materials[hit.geometry].firstVertex + hit.primitive * 3;
    const float scale = materials[hit.geometry].unitScale;
    float3 n = normalize(cross((positions[offset + 1] - positions[offset]) * scale,
                               (positions[offset + 2] - positions[offset]) * scale));
    return dot(n, incident) > 0 ? -n : n;
}

float4 Project(float3 location) {
    return mul(projection, float4(eyeScale.xyz + location / eyeScale.w, 1));
}

float Depth(float3 location, float4 clip) {
    if ((options.x & 1) != 0) {
        const float au = length(location) / 149597870700.0;
        return log2(1 + au * 1e12) / log2(1 + 20000.0 * 1e12);
    }
    return clip.z / clip.w * .5 + .5;
}

bool Lookup(float4 clip, out uint2 pixel) {
    // The shared scene flips clip-space Y before rasterization so the native
    // render target is addressed in the engine's bottom-up texture order.
    float2 uv = clip.xy / max(clip.w, 1e-22) * .5 + .5;
    pixel = region.xy + min(uint2(saturate(uv) * region.zw), region.zw - 1);
    return clip.w > 0 && all(uv >= 0) && all(uv < 1);
}

float3 Environment(float3 direction) {
    if ((options.x & 2) != 0) return skyRadiance.SampleLevel(skySampler,direction,0).rgb;
    const float3 ambient = float3(.005,.009,.015);
    if (sky.w <= 0) return ambient;
    const float elevation = dot(direction,sky.xyz);
    const float sunHeight = dot(lightRange.xyz,sky.xyz);
    const float daytime = smoothstep(-.14,.24,sunHeight);
    const float towardsLight = pow(saturate(dot(direction,lightRange.xyz)),9);
    const float twilight = smoothstep(-.19,-.03,sunHeight)*(1-daytime);
    const float3 horizon = float3(.12,.19,.28)*daytime + float3(.22,.082,.027)*twilight*towardsLight;
    const float3 zenith = float3(.024,.082,.20)*daytime;
    const float3 air = lerp(horizon,zenith,sqrt(saturate(elevation)))
        + float3(.18,.148,.10)*daytime*towardsLight;
    return ambient+air*sky.w*smoothstep(-.028,.024,elevation);
}

float3 Radiance(Hit hit, float3 origin, float3 direction) {
    const Material material = materials[hit.geometry];
    const float3 location = origin + direction * hit.distance;
    const float4 clip = Project(location);
    const float3 n = Normal(hit, direction);
    const float3 approximate = material.color * (.14 + .80 * saturate(dot(n, lightRange.xyz)));
    uint2 pixel;
    if (Lookup(clip, pixel) && abs(rasterDepth[pixel] - Depth(location, clip)) < .00015) {
        const float4 sampleColor = rasterColor[pixel];
        if (sampleColor.a < .099 || sampleColor.a > .405) {
            const float2 uv = clip.xy/clip.w*.5+.5;
            const float2 edge = min(uv,1-uv);
            return lerp(approximate,sampleColor.rgb,smoothstep(0,.055,min(edge.x,edge.y)));
        }
    }
    return approximate;
}

float3 ViewDirection(uint2 pixelInRegion) {
    const float2 ndc = (float2(pixelInRegion) + .5) / float2(region.zw) * 2 - 1;
    const float4 h = mul(inverseProjection, float4(ndc, .5, 1));
    return normalize(h.xyz-eyeScale.xyz*h.w);
}

float3 ReconstructReflection(uint2 pixel, float3 normal) {
    const float4 center = reflectionSamples[pixel];
    if (center.a >= -1) return center.rgb;
    const float depth = rasterDepth[pixel];
    float4 combined = 0;
    float referenceHit = -2;
    [unroll] for (int y = -1; y <= 1; ++y) {
        [unroll] for (int x = -1; x <= 1; ++x) {
            if (abs(x)+abs(y) != 1) continue;
            const int2 neighbor = int2(pixel) + int2(x,y);
            if (any(neighbor < int2(region.xy)) || any(neighbor >= int2(region.xy + region.zw))) continue;
            const float4 candidate = reflectionSamples[neighbor];
            const float4 guide = reflectionGuide[neighbor];
            if (candidate.a < -1 || guide.a <= 0 ||
                abs(rasterDepth[neighbor]-depth) > .0016 || dot(guide.xyz,normal) < .88) continue;
            if (referenceHit < -1) referenceHit = candidate.a;
            if ((candidate.a < 0) != (referenceHit < 0)) continue;
            const float weight = exp(-abs(rasterDepth[neighbor]-depth)*2400)
                * pow(saturate(dot(guide.xyz,normal)),16)
                * (candidate.a < 0 ? 1 : exp(-abs(candidate.a-referenceHit)/max(2.0,referenceHit*.15)));
            combined += float4(candidate.rgb,1) * weight;
        }
    }
    if (combined.a > .0001) return combined.rgb/combined.a;
    return Environment(reflect(ViewDirection(pixel-region.xy),normal));
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (any(id.xy >= region.zw)) return;
    const uint2 pixel = region.xy + id.xy;
    if (passIndex != 0) {
        const float4 guide = reflectionGuide[pixel];
        if (guide.a > 0) {
            const float4 base = result[pixel];
            result[pixel] = float4(lerp(base.rgb, ReconstructReflection(pixel,guide.xyz),guide.a),base.a);
        }
        return;
    }
    reflectionSamples[pixel] = float4(0,0,0,-2);
    reflectionGuide[pixel] = 0;
    const float4 original = rasterColor[pixel];
    float3 color = original.rgb;
    const float rasterZ = rasterDepth[pixel];
    if (rasterZ < .99999) {
        const float3 direction = ViewDirection(id.xy);
        const Hit surface = Trace(0, direction, lightRange.w, Primary);
        if (surface.distance >= 0) {
            const float3 location = direction * surface.distance;
            if (abs(rasterZ - Depth(location, Project(location))) < .0003) {
                const Material material = materials[surface.geometry];
                const bool water = material.kind == Surface && original.a >= .099 && original.a <= .405;
                const bool glass = material.kind == Glass;
                const float3 geometryNormal = Normal(surface, direction);
                const float4 wave = water ? waterNormal[pixel] : 0;
                const float3 normal = wave.a > 0 && dot(wave.xyz, -direction) > .002 ? normalize(wave.xyz) : geometryNormal;
                const float offset = max(.025, surface.distance * .000015);
                const float3 exterior = location + geometryNormal * offset;
                const float3 interior = location - geometryNormal * offset;
                if (!glass && dot(normal, lightRange.xyz) > .035) {
                    const Hit occluder = Trace(exterior, lightRange.xyz, lightRange.w, Shadow);
                    if (occluder.distance >= 0) color *= .38;
                }
                if (!water && !glass) {
                    const float3 tangent = normalize(cross(abs(normal.y) < .96 ? float3(0,1,0) : float3(1,0,0), normal));
                    const float3 bitangent = cross(normal, tangent);
                    const float3 first = normalize(normal + tangent * .58 + bitangent * .67);
                    const float3 second = normalize(normal - tangent * .77 - bitangent * .35);
                    const float bounceRange = (options.x & 1) != 0 ? min(lightRange.w, 160.0) : lightRange.w;
                    const Hit a = Trace(exterior, first, bounceRange, Indirect);
                    const Hit b = Trace(exterior, second, bounceRange, Indirect);
                    float3 bounced = 0;
                    if (a.distance >= 0) bounced += Radiance(a, exterior, first);
                    if (b.distance >= 0) bounced += Radiance(b, exterior, second);
                    color += bounced * .11 * material.color;
                }
                const float f0 = water ? .0204 : glass ? .04 : material.reflectivity;
                const float fresnel = f0 + (1 - f0) * pow(1 - saturate(dot(normal, -direction)), 5);
                if (options.w != 0 && (water || glass)) {
                    const float3 bent = refract(direction, normal, water ? 1.0 / 1.333 : 1.0 / 1.5);
                    const Hit through = Trace(interior, bent, water ? 110.0 : lightRange.w, Transmitted);
                    if (through.distance >= 0) {
                        const float3 absorption = water ? exp(-through.distance * float3(.18, .067, .036)) : float3(.92, .96, .99);
                        const float3 light = Radiance(through, interior, bent);
                        color = lerp(color, light * absorption + color * (1 - absorption), (1 - fresnel) * (water ? .86 : .96));
                    }
                }
                if (options.z != 0 && (water || glass || f0 > .001)) {
                    const float weight = fresnel * (water || glass ? .94 : .50);
                    reflectionGuide[pixel] = float4(normal,weight);
                    if (((id.x + id.y) & 1) == 0) {
                        const float3 reflected = reflect(direction, normal);
                        const Hit mirror = Trace(exterior, reflected, lightRange.w, Reflective);
                        const float3 reflectedLight = mirror.distance >= 0 ? Radiance(mirror, exterior, reflected)
                            : (water || glass || (options.x & 2) != 0 ? Environment(reflected) : color);
                        reflectionSamples[pixel] = float4(reflectedLight,min(mirror.distance,65000.0));
                    }
                }
            }
        }
    }
    result[pixel] = float4(clamp(color, 0, 65000), original.a);
}

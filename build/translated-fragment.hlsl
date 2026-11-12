uniform float3 tint;
Texture2D<float4> scene : register(t0, space0);
SamplerState _scene_sampler : register(s0, space0);

static float4 color;
static float3 direction;

struct SPIRV_Cross_Input
{
    float3 direction : TEXCOORD0;
};

struct SPIRV_Cross_Output
{
    float4 color : SV_Target0;
};

void frag_main()
{
    color = float4(tint, 1.0f) * scene.Sample(_scene_sampler, direction.xy);
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    direction = stage_input.direction;
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output.color = color;
    return stage_output;
}

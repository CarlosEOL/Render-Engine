uniform float4x4 _matrix;

static float4 gl_Position;
static float3 direction;
static float3 position;

struct SPIRV_Cross_Input
{
    float3 position : TEXCOORD0;
};

struct SPIRV_Cross_Output
{
    float3 direction : TEXCOORD0;
    float4 gl_Position : SV_Position;
};

void vert_main()
{
    direction = position;
    gl_Position = mul(float4(position, 1.0f), _matrix);
    gl_Position.y = -gl_Position.y;
    gl_Position.z = (gl_Position.z + gl_Position.w) * 0.5;
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    position = stage_input.position;
    vert_main();
    SPIRV_Cross_Output stage_output;
    stage_output.gl_Position = gl_Position;
    stage_output.direction = direction;
    return stage_output;
}

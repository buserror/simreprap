
#if (GLSL_VERSION == 120)
#extension GL_EXT_gpu_shader4 : enable
#endif

// References:
// http://www.geeks3d.com/20110405/fxaa-fast-approximate-anti-aliasing-demo-glsl-opengl-test-radeon-geforce/3/
// http://jmonkeyengine.googlecode.com/svn-history/r9095/trunk/engine/src/core-data/Common/MatDefs/Post/

uniform sampler2D m_Texture;
uniform vec2 g_Resolution = vec2(800,600);

//uniform float m_VxOffset;
uniform float m_SpanMax = 8.0;
uniform float m_ReduceMul = (1.0/8.0);

varying vec2 texCoord;
varying vec4 posPos;

#define FxaaTex(t, p) texture2D(t, p)
#define OffsetVec(a, b) ivec2(a, b)
#if (GLSL_VERSION == 120)
    #define FxaaTexOff(t, p, o, r) texture2DLodOffset(t, p, 0.0, o)
#endif
#if (GLSL_VERSION == 130)
    #define FxaaTexOff(t, p, o, r) textureLodOffset(t, p, 0.0, o)
#endif

vec3 FxaaPixelShader(
  vec4 posPos,   // Output of FxaaVertexShader interpolated across screen.
  sampler2D tex, // Input texture.
  vec2 rcpFrame) // Constant {1.0/frameWidth, 1.0/frameHeight}.
{

    #define FXAA_REDUCE_MIN   (1.0/128.0)
    //#define FXAA_REDUCE_MUL   (1.0/8.0)
    //#define FXAA_SPAN_MAX     8.0

    vec3 rgbNW = FxaaTex(tex, posPos.zw).xyz;
    vec3 rgbNE = FxaaTexOff(tex, posPos.zw, OffsetVec(1,0), rcpFrame.xy).xyz;
    vec3 rgbSW = FxaaTexOff(tex, posPos.zw, OffsetVec(0,1), rcpFrame.xy).xyz;
    vec3 rgbSE = FxaaTexOff(tex, posPos.zw, OffsetVec(1,1), rcpFrame.xy).xyz;

    vec3 rgbM  = FxaaTex(tex, posPos.xy).xyz;

    vec3 luma = vec3(0.299, 0.587, 0.114);
    float lumaNW = dot(rgbNW, luma);
    float lumaNE = dot(rgbNE, luma);
    float lumaSW = dot(rgbSW, luma);
    float lumaSE = dot(rgbSE, luma);
    float lumaM  = dot(rgbM,  luma);

    float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
    float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));

    vec2 dir;
    dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
    dir.y =  ((lumaNW + lumaSW) - (lumaNE + lumaSE));

    float dirReduce = max(
        (lumaNW + lumaNE + lumaSW + lumaSE) * (0.25 * m_ReduceMul),
        FXAA_REDUCE_MIN);
    float rcpDirMin = 1.0/(min(abs(dir.x), abs(dir.y)) + dirReduce);
    dir = min(vec2( m_SpanMax,  m_SpanMax),
          max(vec2(-m_SpanMax, -m_SpanMax),
          dir * rcpDirMin)) * rcpFrame.xy;

    vec3 rgbA = (1.0/2.0) * (
        FxaaTex(tex, posPos.xy + dir * vec2(1.0/3.0 - 0.5)).xyz +
        FxaaTex(tex, posPos.xy + dir * vec2(2.0/3.0 - 0.5)).xyz);
    vec3 rgbB = rgbA * (1.0/2.0) + (1.0/4.0) * (
        FxaaTex(tex, posPos.xy + dir * vec2(0.0/3.0 - 0.5)).xyz +
        FxaaTex(tex, posPos.xy + dir * vec2(3.0/3.0 - 0.5)).xyz);

    float lumaB = dot(rgbB, luma);

    if ((lumaB < lumaMin) || (lumaB > lumaMax))
    {
        return rgbA;
    }
    else
    {
        return rgbB; 
    }
}

void main()
{
    vec2 rcpFrame = vec2(1.0) / g_Resolution;
    gl_FragColor = vec4(FxaaPixelShader(posPos, m_Texture, rcpFrame), 1.0);
//	gl_FragColor.g *= 2;
}

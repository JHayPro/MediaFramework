// Globals.cpp (MediaFramework)
#include "Globals.h"

std::unordered_map<uint64_t, std::unique_ptr<Decoder>> g_decoders;
std::atomic<uint64_t> g_nextDecoderId{1ULL};  // Start from 1ULL

std::unordered_map<uint32_t, MediaInstance> g_activeVideos;
std::atomic<uint32_t> g_nextVideoId{1};  // Start from 1; 0 reserved for default

std::unordered_map<uint64_t, MediaInstance> g_mediaInstances;
std::atomic<uint64_t> g_nextMediaInstanceId;

VideoResources g_resources;  // Global shared resources (shaders, etc.)

const char* const kEmbeddedHLSL = R"(
cbuffer VideoParams : register(b0)
{
    float mediaAspect;   // mediaWidth / mediaHeight
    float targetAspect;  // real pixel aspect of destination rect
    uint  scaleMode;     // 0 = Fit, 1 = Fill, 2 = Stretch
    uint  _pad;
};

struct VSIn  { float4 pos : POSITION; float2 uv : TEXCOORD; };
struct PSIn  { float4 pos : SV_POSITION; float2 uv : TEXCOORD; };

PSIn VSMain(VSIn IN)
{
    PSIn O;
    O.pos = IN.pos;

    float2 uv = IN.uv;

    if (scaleMode == 0) // Fit → letterbox / pillarbox (black bars)
    {
        if (mediaAspect > targetAspect)
        {
            // wider → bars top/bottom
            float s = targetAspect / mediaAspect;
            uv.y = 0.5 + (uv.y - 0.5) / s;
        }
        else if (mediaAspect < targetAspect)
        {
            // taller → bars left/right
            float s = mediaAspect / targetAspect;
            uv.x = 0.5 + (uv.x - 0.5) / s;
        }
    }
    else if (scaleMode == 1) // Fill → crop to fill (no black bars, aspect preserved)
    {
        if (mediaAspect > targetAspect)
        {
            // video wider than target → crop left/right
            float s = targetAspect / mediaAspect;  // < 1
            uv.x = 0.5 + (uv.x - 0.5) * s;
        }
        else if (mediaAspect < targetAspect)
        {
            // video narrower than target → crop top/bottom
            float s = mediaAspect / targetAspect;  // < 1
            uv.y = 0.5 + (uv.y - 0.5) * s;
        }
    }
    // scaleMode == 2 (Stretch) → leave UV as-is

    O.uv = uv;
    return O;
}

Texture2D    tex0  : register(t0);
SamplerState samp0 : register(s0);

float4 PSMain(PSIn IN) : SV_Target
{
    // Only Fit mode produces UVs outside [0,1] → turn them into black bars
    if (any(IN.uv < 0.0 || IN.uv > 1.0))
        return float4(0, 0, 0, 1);
    return tex0.Sample(samp0, IN.uv);
}
)";

std::mutex g_videoMutex;

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
struct VSIn { float4 pos : POSITION; float2 uv : TEXCOORD; };
struct PSIn { float4 pos : SV_POSITION; float2 uv : TEXCOORD; };
PSIn VSMain(VSIn IN) { PSIn O; O.pos = IN.pos; O.uv = IN.uv; return O; }
Texture2D tex0 : register(t0);
SamplerState samp0 : register(s0);
float4 PSMain(PSIn IN) : SV_Target { return tex0.Sample(samp0, IN.uv); }
)";

std::mutex g_videoMutex;

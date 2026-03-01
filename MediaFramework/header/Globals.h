// Globals.h (MediaFramework)
// Global structures and resources.
#pragma once
#include "MediaFrameworkAPI.h"
#include "ScaleformTextureHelper.h"
#include "PCH.h"

/** @brief Shared memory layout for audio decoder. */
struct SharedAudioControl
{
	// Status
	volatile uint32_t isPlaying{ 0 };
	volatile uint32_t shouldExit{ 0 };
	volatile uint32_t lastHeartbeat{ 0 };
	volatile uint32_t decoderState{ 0 };

	// Synchronization with video decoder
	volatile double currentAudioPTS{ 0.0 };
	volatile double targetVideoPTS{ 0.0 };
	volatile int32_t syncDriftMs{ 0 };

	// 3D Position (updated by DLL every frame)
	float listenerX{ 0.0f };
	float listenerY{ 0.0f };
	float listenerZ{ 0.0f };
	float listenerFrontX{ 0.0f };
	float listenerFrontY{ 1.0f };
	float listenerFrontZ{ 0.0f };
	float listenerUpX{ 0.0f };
	float listenerUpY{ 0.0f };
	float listenerUpZ{ 1.0f };

	float emitterX{ 0.0f };
	float emitterY{ 0.0f };
	float emitterZ{ 0.0f };

	// Audio properties
	float volume{ 1.0f };
	float maxDistance{ 30.0f };
	float minDistance{ 1.0f };

	// Audio info
	volatile uint32_t sampleRate{ 0 };
	volatile uint32_t channels{ 0 };

	// Error reporting
	char errorMessage[256]{ 0 };
	volatile uint32_t errorCode{ 0 };

	// Command system (for warm decoder)
	volatile uint32_t command{ 0 };      // 0=none, 1=play, 2=pause, 3=stop, 4=seek
	volatile uint32_t commandAck{ 0 };   // Command acknowledged (set by decoder)
	char audioPath[512]{ 0 };            // Path for play command
	volatile uint32_t loopEnabled{ 1 };  // Loop flag
	volatile float seekTime{ 0.0f };     // Seek time in seconds

	uint32_t reserved[4]{ 0 };  // Adjusted for command fields
};

/** @brief Enhanced shared memory layout for Phase 1 video decoder. */
struct SharedVideoHeader
{
	// Video dimensions and format
	volatile uint32_t width{ 0 };
	volatile uint32_t height{ 0 };
	volatile uint32_t pixelFormat{ 0 };
	volatile uint32_t dataSize{ 0 };

	// Frame control
	volatile uint32_t frameIndex{ 0 };    // Frames decoded THIS session
	volatile int64_t absoluteFrame{ 0 };  // Total frames from video start
	volatile uint32_t isReady{ 0 };
	volatile uint32_t writeIndex{ 0 };
	volatile uint32_t readIndex{ 0 };

	// Health monitoring
	volatile uint32_t lastHeartbeat{ 0 };
	volatile uint32_t decoderState{ 0 };
	volatile uint32_t shouldExit{ 0 };

	// Error reporting
	char errorMessage[256]{ 0 };
	volatile uint32_t errorCode{ 0 };

	// Audio sync support
	volatile uint32_t hasAudio{ 0 };
	volatile double videoPTS{ 0.0 };
	volatile double videoFPS{ 0.0 };

	// Command system (for warm decoder)
	volatile uint32_t command{ 0 };      // 0=none, 1=play, 2=pause, 3=stop, 4=seek
	volatile uint32_t commandAck{ 0 };   // Command acknowledged (set by decoder)
	char videoPath[512]{ 0 };            // Path for play command
	volatile int64_t seekFrame{ -1 };    // Start/seek frame
	volatile uint32_t loopEnabled{ 1 };  // Loop flag
	volatile float targetFPS{ 0.0f };    // Target FPS (0 = native)

	uint32_t reserved[2]{ 0 };  // Adjusted for new command fields
};

/** @brief Decoder owns the decoder process and shared memory, can switch between multiple MediaInstances */
struct Decoder
{
	uint64_t id{ 0 };

	HANDLE videoProcess{ nullptr };
	HANDLE audioProcess{ nullptr };

	HANDLE videoShmHandle{ nullptr };
	uint8_t* videoShmView{ nullptr };
	SharedVideoHeader* videoHeader{ nullptr };
	uint8_t* frameData0{ nullptr };
	uint8_t* frameData1{ nullptr };

	HANDLE audioShmHandle{ nullptr };
	uint8_t* audioShmView{ nullptr };
	SharedAudioControl* audioHeader{ nullptr };

	// Shared memory names (consistent for this decoder)
	std::string videoShmName;
	std::string audioShmName;

	// State
	std::atomic<bool> isInitialized{ false };
	std::atomic<bool> isPlaying{ false };
	int64_t lastFrameIndex{ -1 };

	// Currently playing instance (only one at a time)
	MediaInstanceHandle currentInstanceHandle{ 0 };

	// Health monitoring
	uint32_t lastVideoHeartbeat{ 0 };
	uint32_t lastAudioHeartbeat{ 0 };
    
    void Reset()
    {
        isPlaying.store(false);
        isInitialized.store(false);
        lastFrameIndex = -1;
        currentInstanceHandle = 0;
        
        if (videoProcess) {
			CloseHandle(videoProcess);
			videoProcess = nullptr;
        }
        if (audioProcess) {
            CloseHandle(audioProcess);
            audioProcess = nullptr;
        }
        if (videoShmView) {
            UnmapViewOfFile(videoShmView);
            videoShmView = nullptr;
        }
        if (videoShmHandle) {
            CloseHandle(videoShmHandle);
            videoShmHandle = nullptr;
        }
        if (audioShmView) {
            UnmapViewOfFile(audioShmView);
            audioShmView = nullptr;
        }
        if (audioShmHandle) {
            CloseHandle(audioShmHandle);
            audioShmHandle = nullptr;
        }
        
        videoHeader = nullptr;
        audioHeader = nullptr;
        frameData0 = nullptr;
        frameData1 = nullptr;
        lastVideoHeartbeat = 0;
        lastAudioHeartbeat = 0;
    }
};

extern std::unordered_map<uint64_t, std::unique_ptr<Decoder>> g_decoders;
extern std::atomic<uint64_t> g_nextDecoderId;

/** @brief Engine-owned texture wrapper for ShaderFX rendering */
struct EngineTextureCache
{
    // Texture resources
    RE::NiPointer<RE::NiTexture> niTexture;
    RE::BSGraphics::Texture* bsTexture{ nullptr };
    uint32_t width{ 0 };
    uint32_t height{ 0 };
    bool needsRecreate{ true };
    
    // SWF/Scaleform integration
    const char* menuName{ nullptr };
    const char* menuObjPath{ nullptr };
    RE::BSFixedString bsMenuName;
    
    // BSScaleformExternalTexture for img:// binding
    RE::BSScaleformExternalTexture externalTexture;

    ScaleformTextureHelper helper;
    
    void Reset() {
        //niTexture.reset();
        bsTexture = nullptr;  // Engine owns, don't delete
        width = 0;
        height = 0;
        needsRecreate = true;
        menuName = nullptr;
        menuObjPath = nullptr;
    }
};

/** @brief Lightweight media instance - just metadata, decoder does the work */
struct MediaInstance
{
    uint64_t id{ 0 };
    DecoderHandle decoderHandle{ 0 };
    std::string mediaPath;

	MediaComposition mediaComposition { VisualType::None, AudioType::None};
    
    // Playback parameters
    int64_t startFrame{ -1 };
    bool loop{ true };
    float volume{ 1.0f };

    // Rendering: MODE determines HOW, STAGE determines WHEN, Menu determines WHERE
    RenderMode renderMode{ RenderMode::Fullscreen };
    RenderPipelineStage renderStage{ RenderPipelineStage::Post };
	HookedMenuName hookedMenu;

    float renderX{ 0.0f };
    float renderY{ 0.0f };
    float renderW{ 1.0f };
    float renderH{ 1.0f };

	// GPU resources (instance-owned)
	ComPtr<ID3D11Texture2D> mediaTexture;
	ComPtr<ID3D11ShaderResourceView> srv;
	uint32_t mediaWidth{ 0 };
	uint32_t mediaHeight{ 0 };

	ComPtr<ID3D11Buffer> quadVB;
	bool vbDirty{ true };
    
    // Engine texture cache (for PreUI ShaderFX rendering)
    EngineTextureCache engineTexture;
    
    // State
    std::atomic<bool> isActive{ false };
    
    // Callback
    CallbackFunc finishedCallback{ nullptr };
    void* callbackUserData{ nullptr };
    
    void Reset()
    {
		srv.Reset();
		mediaTexture.Reset();
        isActive.store(false);
		finishedCallback = nullptr;
        callbackUserData = nullptr;
        startFrame = -1;
        loop = true;
        volume = 1.0f;
		mediaWidth = 0;
		mediaHeight = 0;
        renderStage = RenderPipelineStage::Post;
        engineTexture.Reset();
    }
};

extern std::atomic<uint64_t> g_preUIMenuRefCount;
extern std::unordered_map<uint64_t, MediaInstance> g_mediaInstances;
extern std::atomic<uint64_t> g_nextMediaInstanceId;

/** @brief Global shared resources (shaders, etc.). */
struct VideoResources
{
	ComPtr<ID3D11VertexShader> vs;
	ComPtr<ID3D11PixelShader> ps;
	ComPtr<ID3D11InputLayout> layout;
	ComPtr<ID3D11BlendState> blend;
	ComPtr<ID3D11RasterizerState> raster;
	ComPtr<ID3D11DepthStencilState> depth;
	ComPtr<ID3D11SamplerState> sampler;

	using PresentFn = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT);
	PresentFn originalPresent{ nullptr };
	void* presentPtr{ nullptr };

	void Reset()
	{
		vs.Reset();
		ps.Reset();
		layout.Reset();
		blend.Reset();
		raster.Reset();
		depth.Reset();
		sampler.Reset();
		originalPresent = nullptr;
		presentPtr = nullptr;
#ifdef _DEBUG
		logger::debug("Global resources reset");
#endif
	}
};

extern VideoResources g_resources;

extern const char* const kEmbeddedHLSL;

struct Vertex
{
	float pos[4];
	float uv[2];
};

extern const Vertex kQuadVerts[6];

static constexpr size_t SHM_SIZE = 48ULL * 1024ULL * 1024ULL;
static constexpr size_t AUDIO_SHM_SIZE = 4096ULL;  // Small, just control data
static constexpr uint32_t HEARTBEAT_TIMEOUT_MS = 500;

// Decoder states
static constexpr uint32_t STATE_INITIALIZING = 0;
static constexpr uint32_t STATE_IDLE = 1;
static constexpr uint32_t STATE_PLAYING = 2;
static constexpr uint32_t STATE_PAUSED = 3;
static constexpr uint32_t STATE_ERROR = 4;

// Add command constants (to avoid hardcoding)
constexpr uint32_t CMD_NONE = 0;
constexpr uint32_t CMD_PLAY = 1;
constexpr uint32_t CMD_PAUSE = 2;
constexpr uint32_t CMD_STOP = 3;
constexpr uint32_t CMD_SEEK = 4;

extern std::mutex g_videoMutex;

extern HANDLE g_jobHandle;

// Helper to get decoder from handle
static Decoder* GetDecoder(DecoderHandle handle)
{
	if (auto it = g_decoders.find(handle); it != g_decoders.end())
		return it->second.get();

	return nullptr;
}

// Helper to get media instance from handle
static MediaInstance* GetMediaInstance(MediaInstanceHandle handle)
{
	auto it = g_mediaInstances.find(handle);
	return (it != g_mediaInstances.end()) ? &it->second : nullptr;
}

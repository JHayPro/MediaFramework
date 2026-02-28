// MediaFrameworkAPI.h (MediaFramework) - Updated with INI support
#pragma once
#include "PCH.h"

#ifdef MEDIAFRAMEWORK_EXPORTS
#	define MF_API __declspec(dllexport)
#else
#	define MF_API __declspec(dllimport)
#endif

extern "C"
{
    /* =========================================================
   Handles (opaque)
   ========================================================= */

    typedef uint64_t DecoderHandle;
    typedef uint64_t MediaInstanceHandle;

	/* =========================================================
   Result codes (never throw across DLL boundary)
   ========================================================= */

    enum class MF_Result : uint32_t
    {
        Ok = 0,
        InvalidHandle,
        InvalidArgument,
        Unsupported,
        InternalError,
		AccessError,
        ParseError, // New: INI parsing failed
    };

    /* =========================================================
   Decoder creation
   ========================================================= */
	enum class VisualType : uint32_t
	{
		None,
		Image,
		Video,
	};

	enum class DecoderVisualType : uint32_t
	{
		None,
		Video,
	};

	enum class AudioType : uint32_t
	{
		None,
		Enabled,
	};

	struct MediaComposition
	{
		VisualType visualType = VisualType::None;
		AudioType audioType = AudioType::None;
	};

	struct DecoderComposition
	{
		DecoderVisualType visualType = DecoderVisualType::None;
		AudioType audioType = AudioType::None;
	};

    struct DecoderCreateParams
    {
        uint32_t size; // Set to sizeof(DecoderCreateParams)
		DecoderComposition decoderComposition;
    };

    MF_API MF_Result MF_CreateDecoder(
        const DecoderCreateParams* params,
        DecoderHandle* outDecoder);
    MF_API MF_Result MF_DestroyDecoder(
        DecoderHandle decoder);

    /* =========================================================
   Media creation
   ========================================================= */
    struct MediaCreateParams
    {
        uint32_t size; // Set to sizeof(MediaCreateParams)
        const char* mediaPath; // UTF-8 path
		MediaComposition mediaComposition;
    };

    /* =========================================================
   Command system
   ========================================================= */

    enum class MediaCommandType : uint32_t
    {
        None = 0,
        Play,
        Pause,
        Stop,
        Seek,
        SetVolume,
        SetFade,
        SetRenderMode,
        RegisterCallback
    };

    /* ---- Command parameter structs ---- */

    enum class repeatSetting : uint32_t
    {
        None = 0,
        Loop,
        PingPong
    };
    struct audioPositionParams
    {
        uint32_t size; // Set to sizeof(audioPositionParams)
        float listenerX;
        float listenerY;
        float listenerZ;
        float listenerFrontX;
        float listenerFrontY;
        float listenerFrontZ;
        float emitterX;
        float emitterY;
        float emitterZ;
    };

    struct SeekParams
    {
        uint32_t size; // Set to sizeof(SeekParams)
        float timeSeconds;
    };

    struct VolumeParams
    {
        uint32_t size; // Set to sizeof(VolumeParams)
        float volume; // 0.0 - 1.25
    };

    struct FadeParams
    {
        uint32_t size; // Set to sizeof(FadeParams)
        float fadeInSeconds;
        float fadeOutSeconds;
        float color[4]; // RGBA
    };

    enum class RenderMode : uint32_t
    {
        Fullscreen,
        Window,
        //Texture // I dont think this makes sense
    };

	enum class ScaleMode : uint32_t
	{
		Fit,
		Fill,
		Stretch,
	};

    enum class RenderPipelineStage : uint32_t
    {
        Post,
        PreUI,
        MenuHook,
		TextureSwap,
    };

	enum class HookedMenuName : uint32_t
	{
		LoadingMenu,
	};

	enum class ScaleModes : uint32_t
	{
		Fit,
		Fill,
		Stretch,
	};

	struct RenderModeParams
	{
		uint32_t size; // Set to sizeof(RenderModeParams)
		RenderMode mode;
		float x, y, w, h; // normalized or pixels (framework decides)
		RenderPipelineStage stage; // When to render (TextureSwap/Post/PreUI/MenuHook)
		RE::UI_DEPTH_PRIORITY depth;
		HookedMenuName menuName;
		const char* customMenuName;
		ScaleModes scaleMode;
		bool maintainAspect;
		bool blackBars;
	};

	struct InputParams
	{
		bool continueKey;
		bool allowSkip;
	};

    enum class CallbackType : uint32_t
    {
        VideoFinished,
    };

    typedef void (*CallbackFunc)(MediaInstanceHandle instance, void* userData);
    struct CallbackParams
    {
        uint32_t size; // Set to sizeof(CallbackParams)
        CallbackType type;
        CallbackFunc func;
        void* userData; // Optional context
    };

    /* ---- Command packet ---- */

    struct MediaCommandPacket
    {
        MediaCommandType type;
        union
        {
            SeekParams seek;
            VolumeParams volume;
            FadeParams fade;
            RenderModeParams renderMode;
            CallbackParams callback;
            audioPositionParams audioPosition; // If needed, though not in original commands
            uint8_t reserved[128]; // For future param types, max size
        } params;
    };

    /* =========================================================
   INI PARSING API (NEW)
   ========================================================= */

    /**
     * @brief Parse media definition from cascading INI files
     * 
     * Cascading order (later overrides earlier):
     * 1. Child INI (e.g., "MediaLoadscreen.ini") - base defaults
     * 2. Folder INI (parent folder name + ".ini") - folder-wide settings
     * 3. File INI (e.g., "Media/MyMedia.ini") - specific file settings
     * 
     * @param childIniPath Path to child plugin's default INI (can be NULL)
     * @param fileIniPath Path to the media file's INI
     * @param outCreateParams Output: MediaCreateParams (caller allocates)
     * @param outCommands Output: Array of command packets (caller allocates)
     * @param maxCommands Maximum commands array can hold
     * @param outCommandCount Output: Actual number of commands written
     * @return MF_Result::Ok on success, ParseError on failure
     * 
     * @note Thread-safe. Always falls back to safe defaults.
     * @note All pointers must be valid. Use empty string for NULL paths.
     */
    MF_API MF_Result MF_ParseMediaINI(
        const char* childIniPath,
        const char* fileIniPath,
        MediaCommandPacket* outCommands,
        uint32_t maxCommands,
        uint32_t* outCommandCount);

    /**
     * @brief Get error message from last parse failure
     * 
     * @return UTF-8 error string (valid until next MF_ParseMediaINI call)
     */
    MF_API const char* MF_GetLastParseError();

	struct MediaDescriptor
	{
		uint32_t size;          // Must be sizeof(MediaDescriptor)
		char primaryPath[512];  // UTF-8, always filled for valid entries
		char audioPath[512];    // UTF-8, empty string if none
		char iniPath[512];      // UTF-8, empty string if none
		MediaComposition mediaComposition{ VisualType::None, AudioType::None };
		uint32_t reserved[4] = { 0 };
	};

	MF_API MF_Result MF_DiscoverMedia(
		const char* path,
		MediaDescriptor* outDescriptors,
		uint32_t maxDescriptors,
		uint32_t* outCount);

    /* =========================================================
   Media instance API
   ========================================================= */

    MF_API MF_Result MF_CreateMediaInstance(
        DecoderHandle decoder,
        const MediaCreateParams* createParams,
        const MediaCommandPacket* initialCommands,
        uint32_t initialCommandCount,
        MediaInstanceHandle* outInstance);

    MF_API MF_Result MF_MediaCommand(
        MediaInstanceHandle instance,
        const MediaCommandPacket* commands,
        uint32_t commandCount);

    MF_API MF_Result MF_DestroyMediaInstance(
        MediaInstanceHandle instance);

	/**
	* @brief Open the PreUI render menu (refcounted)
	* Increments internal refcount. On 0->1 transition, shows MediaFrameworkMenu.
	* PreUI-stage media will only render when refcount > 0.
	* @return Ok on success, InternalError if UI system unavailable
	*/
	MF_API MF_Result MF_OpenRenderMenu();

	/**
	* @brief Close the PreUI render menu (refcounted)
	* Decrements internal refcount. On 1->0 transition, hides MediaFrameworkMenu.
	* @return Ok on success, InternalError if refcount already zero
	*/
	MF_API MF_Result MF_CloseRenderMenu();
    /* =========================================================
   Queries
   ========================================================= */

    enum class VideoQueryType : uint32_t
    {
        IsPlaying,
        CurrentTime,
        Duration,
		InstanceValid
    };

    struct VideoQueryResult
    {
        union
        {
            uint32_t boolValue;
            float floatValue;
			const char* stringValue;
        };
    };

    struct FrameworkQueryResult
    {
        union
        {
            uint32_t boolValue;
            float floatValue;
			const char* stringValue;
        };
    };

    MF_API MF_Result MF_QueryVideo(
        MediaInstanceHandle instance,
        VideoQueryType query,
        VideoQueryResult* outResult);

    // New: Framework queries
    enum class FrameworkQueryType : uint32_t
    {
        Framework_Version
    };

    MF_API MF_Result MF_QueryFramework(
        FrameworkQueryType query,
        FrameworkQueryResult* outResult);
}

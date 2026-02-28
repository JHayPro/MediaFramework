#include "PCH.h"
#include <format>
#include <libavutil/error.h>

// ============================================================================
// CONSTANTS & CONFIGURATION
// ============================================================================
constexpr uint32_t AUDIO_BUFFER_COUNT = 6;
constexpr uint32_t AUDIO_BUFFER_SIZE_MS = 300;
constexpr uint32_t HEARTBEAT_INTERVAL_MS = 100;
constexpr uint32_t STATE_INITIALIZING = 0;
constexpr uint32_t STATE_IDLE = 1;
constexpr uint32_t STATE_PLAYING = 2;
constexpr uint32_t STATE_PAUSED = 3;
constexpr uint32_t STATE_ERROR = 4;
constexpr float MAX_AUDIO_DISTANCE = 30.0f;
constexpr float MIN_AUDIO_DISTANCE = 1.0f;

// Command types (matching VideoDecoder)
constexpr uint32_t CMD_NONE = 0;
constexpr uint32_t CMD_PLAY = 1;
constexpr uint32_t CMD_PAUSE = 2;
constexpr uint32_t CMD_STOP = 3;
constexpr uint32_t CMD_SEEK = 4;

// ============================================================================
// SHARED MEMORY STRUCTURES
// ============================================================================
struct SharedAudioControl {
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
    float maxDistance{ MAX_AUDIO_DISTANCE };
    float minDistance{ MIN_AUDIO_DISTANCE };

    // Audio info
    volatile uint32_t sampleRate{ 0 };
    volatile uint32_t channels{ 0 };

    // Error reporting
    char errorMessage[256]{ 0 };
    volatile uint32_t errorCode{ 0 };

    // Command system (for warm decoder)
    volatile uint32_t command{ CMD_NONE };
    volatile uint32_t commandAck{ 0 };
    char videoPath[512]{ 0 };
    volatile uint32_t loopEnabled{ 1 };
    volatile float seekTime{ 0.0f };  // Seek time in seconds

    uint32_t reserved[4]{ 0 };  // Adjusted for command fields
};

// ============================================================================
// UTILITIES
// ============================================================================
inline uint32_t GetTickCountMilliseconds() {
    return static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count()
    );
}

// ============================================================================
// LOGGING
// ============================================================================
class Logger {
#ifdef _DEBUG
    static inline std::ofstream debugLog;
    static inline bool initialized{ false };
#endif

public:
    static bool Init() {
#ifdef _DEBUG
        if (initialized) return true;

        PWSTR documentsRawPath = nullptr;
        if (FAILED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &documentsRawPath))) {
            return false;
        }

        std::filesystem::path documentsPath = documentsRawPath;
        CoTaskMemFree(documentsRawPath);

        std::filesystem::path logDir = documentsPath / "My Games" / "Fallout4" / "F4SE";

        if (!std::filesystem::exists(logDir)) {
            std::filesystem::create_directories(logDir);
        }

        std::filesystem::path logPath = logDir / "MediaFramework_AudioDecoder.log";
        debugLog.open(logPath, std::ios::app);

        if (debugLog.is_open()) {
            auto now = std::chrono::system_clock::now();
            debugLog << std::format("\n========== New Session: {:%F %T} ==========\n", now);
            initialized = true;
        }
        return initialized;
#else
        return true;
#endif
    }

    static void Error(const std::string& msg, int errorCode = 0) {
        std::cerr << "ERROR: " << msg;
        if (errorCode != 0) {
            char errBuf[256];
            av_strerror(errorCode, errBuf, sizeof(errBuf));
            std::cerr << std::format(" (FFmpeg: {})", errBuf);
        }
        std::cerr << std::endl;

#ifdef _DEBUG
        if (initialized && debugLog.is_open()) {
            debugLog << "ERROR: " << msg;
            if (errorCode != 0) {
                char errBuf[256];
                av_strerror(errorCode, errBuf, sizeof(errBuf));
                debugLog << std::format(" (FFmpeg: {})", errBuf);
            }
            debugLog << std::endl;
            debugLog.flush();
        }
#endif
    }

    static void Debug(const std::string& msg) {
#ifdef _DEBUG
        if (initialized && debugLog.is_open()) {
            debugLog << "DEBUG: " << msg << std::endl;
            debugLog.flush();
        }
#endif
    }

    static void Info(const std::string& msg) {
#ifdef _DEBUG
        if (initialized && debugLog.is_open()) {
            debugLog << "INFO: " << msg << std::endl;
            debugLog.flush();
        }
#endif
    }
};

// ============================================================================
// RAII WRAPPERS
// ============================================================================
class SharedMemoryMap {
    HANDLE mapHandle{ nullptr };
    void* buffer{ nullptr };

public:
    explicit SharedMemoryMap(const std::string& name) {
        mapHandle = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, name.c_str());
        if (mapHandle) {
            buffer = MapViewOfFile(mapHandle, FILE_MAP_ALL_ACCESS, 0, 0, 0);
            if (!buffer) {
                Logger::Error("Failed to map view of file", GetLastError());
            }
        }
        else {
            Logger::Error("Failed to open shared memory mapping", GetLastError());
        }
    }

    ~SharedMemoryMap() {
        if (buffer) UnmapViewOfFile(buffer);
        if (mapHandle) CloseHandle(mapHandle);
    }

    bool IsValid() const { return buffer != nullptr; }
    void* GetBuffer() const { return buffer; }
    SharedAudioControl* GetHeader() const { return static_cast<SharedAudioControl*>(buffer); }

    SharedMemoryMap(const SharedMemoryMap&) = delete;
    SharedMemoryMap& operator=(const SharedMemoryMap&) = delete;
};

struct AVDeleter {
    void operator()(AVFormatContext* ctx) const { if (ctx) avformat_close_input(&ctx); }
    void operator()(AVCodecContext* ctx) const { if (ctx) avcodec_free_context(&ctx); }
    void operator()(AVFrame* frame) const { if (frame) av_frame_free(&frame); }
    void operator()(AVPacket* packet) const { if (packet) av_packet_free(&packet); }
    void operator()(SwrContext* ctx) const { if (ctx) swr_free(&ctx); }
};

template<typename T>
using AVPtr = std::unique_ptr<T, AVDeleter>;

// ============================================================================
// HEARTBEAT MONITOR
// ============================================================================
class HeartbeatMonitor {
    SharedAudioControl* header;
    std::chrono::steady_clock::time_point lastBeat;
    std::chrono::milliseconds interval;

public:
    HeartbeatMonitor(SharedAudioControl* h, uint32_t intervalMs = HEARTBEAT_INTERVAL_MS)
        : header(h), interval(intervalMs) {
        lastBeat = std::chrono::steady_clock::now();
        UpdateHeartbeat();
    }

    void UpdateHeartbeat() {
        header->lastHeartbeat = GetTickCountMilliseconds();
    }

    void MaybeUpdate() {
        auto now = std::chrono::steady_clock::now();
        if (now - lastBeat >= interval) {
            UpdateHeartbeat();
            lastBeat = now;
        }
    }
};

// ============================================================================
// XAUDIO2 VOICE CALLBACK
// ============================================================================
class AudioCallback : public IXAudio2VoiceCallback {
public:
    std::queue<size_t>* freeQueue{ nullptr };
    std::mutex* freeQueueMutex{ nullptr };
    std::atomic<int> pendingBuffers{ 0 };

    void STDMETHODCALLTYPE OnVoiceProcessingPassStart(UINT32) override {}
    void STDMETHODCALLTYPE OnVoiceProcessingPassEnd() override {}
    void STDMETHODCALLTYPE OnStreamEnd() override {}
    void STDMETHODCALLTYPE OnBufferStart(void*) override {}

    void STDMETHODCALLTYPE OnBufferEnd(void* pContext) override {
        if (pContext && freeQueue && freeQueueMutex) {
            size_t idx = static_cast<size_t>(reinterpret_cast<uintptr_t>(pContext));
            {
                std::lock_guard<std::mutex> lock(*freeQueueMutex);
                freeQueue->push(idx);
            }
        }
        pendingBuffers.fetch_sub(1, std::memory_order_relaxed);
    }

    void STDMETHODCALLTYPE OnLoopEnd(void*) override {}
    void STDMETHODCALLTYPE OnVoiceError(void*, HRESULT) override {}
};

// ============================================================================
// AUDIO DECODER CLASS
// ============================================================================
class AudioDecoder {
    AVPtr<AVFormatContext> formatCtx;
    AVPtr<AVCodecContext> codecCtx;
    AVPtr<SwrContext> swrCtx;
    AVPtr<AVFrame> frame;
    AVPtr<AVPacket> packet;

    AVStream* audioStream{ nullptr };
    int audioStreamIdx{ -1 };

    uint32_t sampleRate{ 0 };
    uint32_t channels{ 0 };
    AVSampleFormat targetFormat{ AV_SAMPLE_FMT_FLT };
    std::string currentVideoPath;

    SharedAudioControl* header{ nullptr };
    double lastPTS{ 0.0 };

public:
    ~AudioDecoder() {
        Cleanup();
    }

    void Cleanup() {
        packet.reset();
        frame.reset();
        swrCtx.reset();
        codecCtx.reset();
        formatCtx.reset();
        
        audioStream = nullptr;
        audioStreamIdx = -1;
        sampleRate = 0;
        channels = 0;
        currentVideoPath.clear();
        lastPTS = 0.0;
    }

    bool IsInitialized() const { return formatCtx != nullptr; }
    std::string GetCurrentVideoPath() const { return currentVideoPath; }

    bool Initialize(const std::string& videoPath, SharedAudioControl* h) {
        header = h;
        header->decoderState = STATE_INITIALIZING;

        AVFormatContext* rawFormatCtx = nullptr;
        AVDictionary* opts = nullptr;
        av_dict_set(&opts, "probesize", "50M", 0);
        av_dict_set(&opts, "analyzeduration", "50M", 0);
        av_dict_set(&opts, "buffer_size", "8192k", 0);
        av_dict_set(&opts, "err_detect", "ignore_err", 0);
        av_dict_set(&opts, "flags", "+ignidx+genpts+discardcorrupt", 0);

        int ret = avformat_open_input(&rawFormatCtx, videoPath.c_str(), nullptr, &opts);
        av_dict_free(&opts);
        if (ret < 0) {
            SetError(std::format("Failed to open video file: {}", videoPath), ret);
            return false;
        }
        formatCtx.reset(rawFormatCtx);

        ret = avformat_find_stream_info(formatCtx.get(), nullptr);
        if (ret < 0) {
            SetError("Failed to find stream info", ret);
            return false;
        }

        audioStreamIdx = av_find_best_stream(formatCtx.get(), AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
        if (audioStreamIdx < 0) {
            SetError("No audio stream found in video");
            return false;
        }
        audioStream = formatCtx->streams[audioStreamIdx];

        // Disable non-audio streams
        for (unsigned int i = 0; i < formatCtx->nb_streams; ++i) {
            if (i != static_cast<unsigned int>(audioStreamIdx)) {
                formatCtx->streams[i]->discard = AVDISCARD_ALL;
            }
        }

        const AVCodec* decoder = avcodec_find_decoder(audioStream->codecpar->codec_id);
        if (!decoder) {
            SetError("Audio decoder not found");
            return false;
        }

        AVCodecContext* rawCodecCtx = avcodec_alloc_context3(decoder);
        if (!rawCodecCtx) {
            SetError("Failed to allocate audio codec context");
            return false;
        }
        codecCtx.reset(rawCodecCtx);

        ret = avcodec_parameters_to_context(codecCtx.get(), audioStream->codecpar);
        if (ret < 0) {
            SetError("Failed to copy audio codec parameters", ret);
            return false;
        }

        codecCtx->thread_count = 2;

        ret = avcodec_open2(codecCtx.get(), decoder, nullptr);
        if (ret < 0) {
            SetError("Failed to open audio codec", ret);
            return false;
        }

        sampleRate = codecCtx->sample_rate;
        channels = codecCtx->ch_layout.nb_channels;

        header->sampleRate = sampleRate;
        header->channels = channels;

        SwrContext* rawSwrCtx = swr_alloc();
        if (!rawSwrCtx) {
            SetError("Failed to allocate resampler");
            return false;
        }
        swrCtx.reset(rawSwrCtx);

        av_opt_set_chlayout(swrCtx.get(), "in_chlayout", &codecCtx->ch_layout, 0);
        av_opt_set_chlayout(swrCtx.get(), "out_chlayout", &codecCtx->ch_layout, 0);
        av_opt_set_int(swrCtx.get(), "in_sample_rate", sampleRate, 0);
        av_opt_set_int(swrCtx.get(), "out_sample_rate", sampleRate, 0);
        av_opt_set_sample_fmt(swrCtx.get(), "in_sample_fmt", codecCtx->sample_fmt, 0);
        av_opt_set_sample_fmt(swrCtx.get(), "out_sample_fmt", targetFormat, 0);

        ret = swr_init(swrCtx.get());
        if (ret < 0) {
            SetError("Failed to initialize resampler", ret);
            return false;
        }

        AVFrame* rawFrame = av_frame_alloc();
        AVPacket* rawPacket = av_packet_alloc();
        if (!rawFrame || !rawPacket) {
            SetError("Failed to allocate frame/packet");
            if (rawFrame) av_frame_free(&rawFrame);
            if (rawPacket) av_packet_free(&rawPacket);
            return false;
        }
        frame.reset(rawFrame);
        packet.reset(rawPacket);

        currentVideoPath = videoPath;
        lastPTS = 0.0;

        Logger::Info(std::format("Audio decoder initialized: {}Hz, {} channels", sampleRate, channels));
        return true;
    }

    enum class DecodeResult { Success, NeedMoreData, EndOfFile, Error };

    DecodeResult DecodeNextFrame(std::vector<float>& outSamples) {
        int ret = avcodec_receive_frame(codecCtx.get(), frame.get());

        if (ret == 0) {
            if (frame->nb_samples == 0) {
                av_frame_unref(frame.get());
                return DecodeResult::Success;
            }

            int max_out_samples = swr_get_out_samples(swrCtx.get(), frame->nb_samples);
            if (max_out_samples < 0) {
                SetError("swr_get_out_samples failed", max_out_samples);
                return DecodeResult::Error;
            }

            outSamples.resize(max_out_samples * channels);

            uint8_t* outputBuffer = reinterpret_cast<uint8_t*>(outSamples.data());
            int converted = swr_convert(swrCtx.get(), &outputBuffer, max_out_samples,
                const_cast<const uint8_t**>(frame->data), frame->nb_samples);

            if (converted < 0) {
                SetError("Failed to convert audio samples", converted);
                return DecodeResult::Error;
            }

            outSamples.resize(converted * channels);

            if (frame->pts != AV_NOPTS_VALUE) {
                lastPTS = frame->pts * av_q2d(audioStream->time_base);
                header->currentAudioPTS = lastPTS;
            }

            av_frame_unref(frame.get());
            return DecodeResult::Success;
        }
        else if (ret == AVERROR(EAGAIN)) {
            // Need more input
        }
        else if (ret == AVERROR_EOF) {
            int flush_samples = swr_get_out_samples(swrCtx.get(), 0);
            if (flush_samples > 0) {
                outSamples.resize(flush_samples * channels);
                uint8_t* outputBuffer = reinterpret_cast<uint8_t*>(outSamples.data());
                int converted = swr_convert(swrCtx.get(), &outputBuffer, flush_samples, nullptr, 0);
                if (converted > 0) {
                    outSamples.resize(converted * channels);
                    return DecodeResult::Success;
                }
            }
            return DecodeResult::EndOfFile;
        }
        else if (ret == AVERROR_INVALIDDATA) {
            return DecodeResult::NeedMoreData;
        }
        else if (ret < 0) {
            return DecodeResult::NeedMoreData;
        }

        // Read packets
        while (true) {
            ret = av_read_frame(formatCtx.get(), packet.get());

            if (ret == AVERROR(EAGAIN)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                continue;
            }
            else if (ret == AVERROR_EOF) {
                avcodec_send_packet(codecCtx.get(), nullptr);
                while (true) {
                    ret = avcodec_receive_frame(codecCtx.get(), frame.get());
                    if (ret == AVERROR_EOF) break;
                    if (ret == AVERROR(EAGAIN)) continue;
                    if (ret < 0) break;
                    
                    int max_out_samples = swr_get_out_samples(swrCtx.get(), frame->nb_samples);
                    if (max_out_samples < 0) break;
                    outSamples.resize(max_out_samples * channels);
                    uint8_t* outputBuffer = reinterpret_cast<uint8_t*>(outSamples.data());
                    int converted = swr_convert(swrCtx.get(), &outputBuffer, max_out_samples,
                        const_cast<const uint8_t**>(frame->data), frame->nb_samples);
                    if (converted > 0) {
                        outSamples.resize(converted * channels);
                        av_frame_unref(frame.get());
                        return DecodeResult::Success;
                    }
                    av_frame_unref(frame.get());
                }
                return DecodeResult::EndOfFile;
            }
            else if (ret == AVERROR_INVALIDDATA) {
                av_packet_unref(packet.get());
                continue;
            }
            else if (ret < 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                return DecodeResult::NeedMoreData;
            }

            if (packet->stream_index == audioStreamIdx) {
                ret = avcodec_send_packet(codecCtx.get(), packet.get());
                if (ret < 0 && ret != AVERROR_INVALIDDATA) {
                    SetError("avcodec_send_packet failed", ret);
                    av_packet_unref(packet.get());
                    return DecodeResult::Error;
                }
                av_packet_unref(packet.get());
                return DecodeResult::NeedMoreData;
            }
            av_packet_unref(packet.get());
        }
    }

    void SeekToTime(double seconds) {
        int64_t timestamp = static_cast<int64_t>(seconds / av_q2d(audioStream->time_base));
        av_seek_frame(formatCtx.get(), audioStreamIdx, timestamp, AVSEEK_FLAG_BACKWARD);
        avcodec_flush_buffers(codecCtx.get());
        lastPTS = seconds;
    }

    void Reset() {
        av_seek_frame(formatCtx.get(), audioStreamIdx, 0, AVSEEK_FLAG_BACKWARD);
        avcodec_flush_buffers(codecCtx.get());
        lastPTS = 0.0;
    }

    uint32_t GetSampleRate() const { return sampleRate; }
    uint32_t GetChannels() const { return channels; }
    double GetCurrentPTS() const { return lastPTS; }

private:
    void SetError(const std::string& message, int code = 0) {
        Logger::Error(message, code);
        header->decoderState = STATE_ERROR;
        header->errorCode = (code != 0) ? static_cast<uint32_t>(code) : 1;

        std::string fullMsg = message;
        if (code != 0) {
            char errBuf[128];
            av_strerror(code, errBuf, sizeof(errBuf));
            fullMsg = std::format("{} ({})", message, errBuf);
        }

        strncpy_s(header->errorMessage, sizeof(header->errorMessage),
            fullMsg.c_str(), _TRUNCATE);
    }
};

// ============================================================================
// 3D AUDIO ENGINE
// ============================================================================
class Audio3DEngine {
    IXAudio2* xaudio{ nullptr };
    IXAudio2MasteringVoice* masterVoice{ nullptr };
    IXAudio2SourceVoice* sourceVoice{ nullptr };
    AudioCallback callback;

    X3DAUDIO_HANDLE x3dInstance{};
    X3DAUDIO_LISTENER listener{};
    X3DAUDIO_EMITTER emitter{};
    X3DAUDIO_DSP_SETTINGS dspSettings{};

    std::vector<float> matrixCoefficients;
    std::vector<float> channelAzimuths;
    uint32_t inputChannels{ 0 };
    uint32_t outputChannels{ 0 };

    SharedAudioControl* header{ nullptr };

public:
    ~Audio3DEngine() {
        if (sourceVoice) {
            sourceVoice->DestroyVoice();
            sourceVoice = nullptr;
        }
        if (masterVoice) {
            masterVoice->DestroyVoice();
            masterVoice = nullptr;
        }
        if (xaudio) {
            xaudio->Release();
            xaudio = nullptr;
        }
    }

    void Cleanup() {
        if (sourceVoice) {
            sourceVoice->DestroyVoice();
            sourceVoice = nullptr;
        }
        inputChannels = 0;
    }

    bool Initialize(uint32_t sampleRate, uint32_t channels, SharedAudioControl* h) {
        header = h;
        inputChannels = channels;

        // Initialize XAudio2 if not already done
        if (!xaudio) {
            HRESULT hr = XAudio2Create(&xaudio, 0, XAUDIO2_DEFAULT_PROCESSOR);
            if (FAILED(hr)) {
                Logger::Error("Failed to create XAudio2 instance", hr);
                return false;
            }

            hr = xaudio->CreateMasteringVoice(&masterVoice);
            if (FAILED(hr)) {
                Logger::Error("Failed to create mastering voice", hr);
                return false;
            }

            XAUDIO2_VOICE_DETAILS details;
            masterVoice->GetVoiceDetails(&details);
            outputChannels = details.InputChannels;

            // Initialize X3DAudio
            DWORD channelMask;
            masterVoice->GetChannelMask(&channelMask);
            X3DAudioInitialize(channelMask, X3DAUDIO_SPEED_OF_SOUND, x3dInstance);

            // Setup listener
            listener.Position = { 0.0f, 0.0f, 0.0f };
            listener.OrientFront = { 0.0f, 0.0f, 1.0f };
            listener.OrientTop = { 0.0f, 1.0f, 0.0f };
            listener.Velocity = { 0.0f, 0.0f, 0.0f };
        }

        // Create source voice for this audio stream
        WAVEFORMATEXTENSIBLE wfx{};
        wfx.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
        wfx.Format.nChannels = static_cast<WORD>(channels);
        wfx.Format.nSamplesPerSec = sampleRate;
        wfx.Format.wBitsPerSample = 32;
        wfx.Format.nBlockAlign = (wfx.Format.nChannels * wfx.Format.wBitsPerSample) / 8;
        wfx.Format.nAvgBytesPerSec = wfx.Format.nSamplesPerSec * wfx.Format.nBlockAlign;
        wfx.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
        wfx.Samples.wValidBitsPerSample = 32;
        wfx.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
        wfx.dwChannelMask = (channels == 1) ? SPEAKER_FRONT_CENTER : (SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT);

        HRESULT hr = xaudio->CreateSourceVoice(&sourceVoice, reinterpret_cast<WAVEFORMATEX*>(&wfx), 0, 2.0f, &callback);
        if (FAILED(hr)) {
            Logger::Error("Failed to create source voice", hr);
            return false;
        }

        // Setup emitter
        emitter.Position = { 0.0f, 0.0f, 0.0f };
        emitter.OrientFront = { 0.0f, 0.0f, 1.0f };
        emitter.OrientTop = { 0.0f, 1.0f, 0.0f };
        emitter.Velocity = { 0.0f, 0.0f, 0.0f };
        emitter.ChannelCount = channels;
        emitter.CurveDistanceScaler = 1.0f;
        emitter.DopplerScaler = 1.0f;
        emitter.ChannelRadius = 1.0f;

        if (channels > 1) {
            channelAzimuths.resize(channels);
            if (channels == 2) {
                channelAzimuths[0] = -X3DAUDIO_PI / 6.0f;
                channelAzimuths[1] = X3DAUDIO_PI / 6.0f;
            } else {
                for (uint32_t i = 0; i < channels; ++i) {
                    channelAzimuths[i] = (2.0f * X3DAUDIO_PI * i) / channels - X3DAUDIO_PI;
                }
            }
            emitter.pChannelAzimuths = channelAzimuths.data();
        } else {
            emitter.pChannelAzimuths = nullptr;
        }

        // Setup DSP settings
        matrixCoefficients.resize(inputChannels * outputChannels);
        dspSettings.SrcChannelCount = inputChannels;
        dspSettings.DstChannelCount = outputChannels;
        dspSettings.pMatrixCoefficients = matrixCoefficients.data();
        dspSettings.pDelayTimes = nullptr;

        Logger::Info(std::format("3D Audio engine initialized: {} input -> {} output channels", inputChannels, outputChannels));
        return true;
    }

    void UpdatePositions() {
        if (!sourceVoice) return;

        listener.Position.x = header->listenerX;
        listener.Position.y = header->listenerY;
        listener.Position.z = header->listenerZ;
        listener.OrientFront.x = header->listenerFrontX;
        listener.OrientFront.y = header->listenerFrontY;
        listener.OrientFront.z = header->listenerFrontZ;
        listener.OrientTop.x = header->listenerUpX;
        listener.OrientTop.y = header->listenerUpY;
        listener.OrientTop.z = header->listenerUpZ;

        // Normalize listener orientations
        float frontLen = sqrtf(
            listener.OrientFront.x * listener.OrientFront.x +
            listener.OrientFront.y * listener.OrientFront.y +
            listener.OrientFront.z * listener.OrientFront.z
        );
        if (frontLen > 0.0001f) {
            listener.OrientFront.x /= frontLen;
            listener.OrientFront.y /= frontLen;
            listener.OrientFront.z /= frontLen;
        } else {
            listener.OrientFront = { 0.0f, 0.0f, 1.0f };
        }

        float topLen = sqrtf(
            listener.OrientTop.x * listener.OrientTop.x +
            listener.OrientTop.y * listener.OrientTop.y +
            listener.OrientTop.z * listener.OrientTop.z
        );
        if (topLen > 0.0001f) {
            listener.OrientTop.x /= topLen;
            listener.OrientTop.y /= topLen;
            listener.OrientTop.z /= topLen;
        } else {
            listener.OrientTop = { 0.0f, 1.0f, 0.0f };
        }

        emitter.Position.x = header->emitterX;
        emitter.Position.y = header->emitterY;
        emitter.Position.z = header->emitterZ;

        if (emitter.Position.x == 0.0f && emitter.Position.y == 0.0f && emitter.Position.z == 0.0f &&
            listener.Position.x == 0.0f && listener.Position.y == 0.0f && listener.Position.z == 0.0f) {
            emitter.Position = listener.Position;
        }

        DWORD flags = X3DAUDIO_CALCULATE_MATRIX | X3DAUDIO_CALCULATE_DOPPLER;
        X3DAudioCalculate(x3dInstance, &listener, &emitter, flags, &dspSettings);

        float distance = sqrtf(
            powf(emitter.Position.x - listener.Position.x, 2.0f) +
            powf(emitter.Position.y - listener.Position.y, 2.0f) +
            powf(emitter.Position.z - listener.Position.z, 2.0f)
        );

        float attenuation = 1.0f;
        if (distance > header->minDistance) {
            if (distance >= header->maxDistance) {
                attenuation = 0.0f;
            } else {
                attenuation = 1.0f - ((distance - header->minDistance) /
                    (header->maxDistance - header->minDistance));
            }
        }

        float finalVolume = header->volume * attenuation;

        sourceVoice->SetOutputMatrix(nullptr, inputChannels, outputChannels,
            dspSettings.pMatrixCoefficients);
        sourceVoice->SetVolume(finalVolume);
        sourceVoice->SetFrequencyRatio(dspSettings.DopplerFactor);
    }

    bool SubmitBuffer(const std::vector<float>& samples, void* context) {
        if (samples.empty() || !sourceVoice) {
            Logger::Error("Attempted to submit empty audio buffer or no source voice");
            return false;
        }

        XAUDIO2_BUFFER buffer{};
        buffer.AudioBytes = static_cast<UINT32>(samples.size() * sizeof(float));
        buffer.pAudioData = reinterpret_cast<const BYTE*>(samples.data());
        buffer.Flags = 0;
        buffer.pContext = context;

        HRESULT hr = sourceVoice->SubmitSourceBuffer(&buffer);
        if (FAILED(hr)) {
            Logger::Error("Failed to submit audio buffer: HRESULT=0x" +
                std::to_string(hr));
            return false;
        }
        callback.pendingBuffers.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    void Start() {
        if (sourceVoice) {
            sourceVoice->Start(0);
        }
    }

    void Stop() {
        if (sourceVoice) {
            sourceVoice->Stop(0);
            sourceVoice->FlushSourceBuffers();
        }
    }

    uint32_t GetQueuedBufferCount() const {
        return callback.pendingBuffers.load(std::memory_order_relaxed);
    }

    void SetFreeQueue(std::queue<size_t>* q, std::mutex* mtx) {
        callback.freeQueue = q;
        callback.freeQueueMutex = mtx;
    }
};

// ============================================================================
// AUDIO PLAYBACK LOOP
// ============================================================================
class AudioPlaybackLoop {
    SharedAudioControl* header;
    AudioDecoder& decoder;
    Audio3DEngine& audioEngine;
    HeartbeatMonitor& heartbeat;

    uint32_t samplesPerBuffer;
    bool loop;
    bool shouldStop{ false };

    std::vector<std::vector<float>> activeBuffers;
    std::queue<size_t> freeIndices;
    uint32_t noProgressCount{ 0 };

    std::mutex freeQueueMutex;

public:
    AudioPlaybackLoop(SharedAudioControl* h, AudioDecoder& d, Audio3DEngine& ae, HeartbeatMonitor& hb, bool loopAudio)
        : header(h), decoder(d), audioEngine(ae), heartbeat(hb), loop(loopAudio) {
        samplesPerBuffer = (decoder.GetSampleRate() * AUDIO_BUFFER_SIZE_MS) / 1000;

        activeBuffers.resize(AUDIO_BUFFER_COUNT * 2);
        for (auto& buf : activeBuffers) {
            buf.reserve(samplesPerBuffer * decoder.GetChannels() * 2);
        }

        for (size_t i = 0; i < activeBuffers.size(); ++i) {
            std::lock_guard<std::mutex> lock(freeQueueMutex);
            freeIndices.push(i);
        }

        audioEngine.SetFreeQueue(&freeIndices, &freeQueueMutex);

        Logger::Info("Audio playback configured: " + std::to_string(samplesPerBuffer) +
            " samples per buffer, Loop: " + (loop ? "YES" : "NO"));
    }

    void Stop() { shouldStop = true; }

    bool Run() {
        header->decoderState = STATE_PLAYING;
        header->isPlaying = 1;
        Logger::Info("Starting audio playback loop");

        std::vector<float> accumulatedSamples;
        accumulatedSamples.reserve(samplesPerBuffer * decoder.GetChannels() * 2);

        auto lastPositionUpdate = std::chrono::steady_clock::now();
        uint32_t buffersSubmitted = 0;
        shouldStop = false;

        // Prime the queue
        int primedBuffers = 0;
        noProgressCount = 0;
        while (primedBuffers < AUDIO_BUFFER_COUNT && !header->shouldExit && !shouldStop && !freeIndices.empty()) {
            heartbeat.MaybeUpdate();

            // Check for stop command
            if (header->command == CMD_STOP) {
                header->command = CMD_NONE;
                header->commandAck = 1;
                Logger::Info("Stop command received during priming");
                header->decoderState = STATE_IDLE;
                header->isPlaying = 0;
                return true;
            }

            std::vector<float> samples;
            auto result = decoder.DecodeNextFrame(samples);

            switch (result) {
            case AudioDecoder::DecodeResult::Success:
                accumulatedSamples.insert(accumulatedSamples.end(), samples.begin(), samples.end());
                noProgressCount = 0;

                while (accumulatedSamples.size() >= samplesPerBuffer * decoder.GetChannels() && !freeIndices.empty()) {
                    size_t bufIndex;
                    {
                        std::lock_guard<std::mutex> lock(freeQueueMutex);
                        if (freeIndices.empty()) break;
                        bufIndex = freeIndices.front();
                        freeIndices.pop();
                    }

                    auto& persistentBuffer = activeBuffers[bufIndex];
                    size_t submitSize = samplesPerBuffer * decoder.GetChannels();
                    persistentBuffer.assign(
                        accumulatedSamples.begin(),
                        accumulatedSamples.begin() + submitSize
                    );

                    accumulatedSamples.erase(
                        accumulatedSamples.begin(),
                        accumulatedSamples.begin() + submitSize
                    );

                    if (!audioEngine.SubmitBuffer(
                        persistentBuffer,
                        reinterpret_cast<void*>(static_cast<uintptr_t>(bufIndex))
                    )) {
                        std::lock_guard<std::mutex> lock(freeQueueMutex);
                        freeIndices.push(bufIndex);
                        Logger::Error("Failed to submit audio buffer");
                        header->decoderState = STATE_ERROR;
                        return false;
                    }

                    buffersSubmitted++;
                    primedBuffers++;
                }
                break;

            case AudioDecoder::DecodeResult::NeedMoreData:
                noProgressCount++;
                if (noProgressCount > 10 && !accumulatedSamples.empty() && !freeIndices.empty()) {
                    size_t bufIndex;
                    {
                        std::lock_guard<std::mutex> lock(freeQueueMutex);
                        if (freeIndices.empty()) break;
                        bufIndex = freeIndices.front();
                        freeIndices.pop();
                    }

                    auto& persistentBuffer = activeBuffers[bufIndex];
                    persistentBuffer.assign(accumulatedSamples.begin(), accumulatedSamples.end());
                    accumulatedSamples.clear();

                    if (!persistentBuffer.empty()) {
                        if (audioEngine.SubmitBuffer(persistentBuffer, reinterpret_cast<void*>(static_cast<uintptr_t>(bufIndex)))) {
                            buffersSubmitted++;
                            primedBuffers++;
                            noProgressCount = 0;
                        } else {
                            std::lock_guard<std::mutex> lock(freeQueueMutex);
                            freeIndices.push(bufIndex);
                        }
                    }
                }
                continue;

            case AudioDecoder::DecodeResult::EndOfFile:
                if (!accumulatedSamples.empty() && !freeIndices.empty()) {
                    size_t bufIndex;
                    {
                        std::lock_guard<std::mutex> lock(freeQueueMutex);
                        if (freeIndices.empty()) break;
                        bufIndex = freeIndices.front();
                        freeIndices.pop();
                    }

                    auto& persistentBuffer = activeBuffers[bufIndex];
                    persistentBuffer.assign(accumulatedSamples.begin(), accumulatedSamples.end());
                    accumulatedSamples.clear();

                    if (audioEngine.SubmitBuffer(persistentBuffer, reinterpret_cast<void*>(static_cast<uintptr_t>(bufIndex)))) {
                        buffersSubmitted++;
                        primedBuffers++;
                    } else {
                        std::lock_guard<std::mutex> lock(freeQueueMutex);
                        freeIndices.push(bufIndex);
                    }
                }

                if (loop) {
                    decoder.Reset();
                    Logger::Debug("Looping audio during priming");
                } else {
                    Logger::Info("End of audio during priming");
                    // Let buffers drain
                    audioEngine.Start();
                    while (audioEngine.GetQueuedBufferCount() > 0 && !header->shouldExit) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                        heartbeat.MaybeUpdate();
                    }
                    audioEngine.Stop();
                    header->decoderState = STATE_IDLE;
                    header->isPlaying = 0;
                    return true;
                }
                noProgressCount = 0;
                break;

            case AudioDecoder::DecodeResult::Error:
                Logger::Error("Audio decode error during priming");
                header->decoderState = STATE_ERROR;
                return false;
            }
        }

        audioEngine.Start();

        // Main playback loop
        noProgressCount = 0;
        while (!header->shouldExit && !shouldStop) {
            heartbeat.MaybeUpdate();

            auto now = std::chrono::steady_clock::now();
            if (now - lastPositionUpdate >= std::chrono::milliseconds(16)) {
                audioEngine.UpdatePositions();
                lastPositionUpdate = now;
            }

            // Check for stop command
            if (header->command == CMD_STOP) {
                header->command = CMD_NONE;
                header->commandAck = 1;
                Logger::Info("Stop command received");
                break;
            }

            while (audioEngine.GetQueuedBufferCount() < AUDIO_BUFFER_COUNT && !freeIndices.empty()) {
                std::vector<float> samples;
                auto result = decoder.DecodeNextFrame(samples);

                switch (result) {
                case AudioDecoder::DecodeResult::Success:
                    accumulatedSamples.insert(accumulatedSamples.end(), samples.begin(), samples.end());
                    noProgressCount = 0;

                    while (accumulatedSamples.size() >= samplesPerBuffer * decoder.GetChannels() && !freeIndices.empty()) {
                        size_t bufIndex;
                        {
                            std::lock_guard<std::mutex> lock(freeQueueMutex);
                            if (freeIndices.empty()) break;
                            bufIndex = freeIndices.front();
                            freeIndices.pop();
                        }

                        auto& persistentBuffer = activeBuffers[bufIndex];
                        size_t submitSize = samplesPerBuffer * decoder.GetChannels();
                        persistentBuffer.assign(accumulatedSamples.begin(), accumulatedSamples.begin() + submitSize);
                        accumulatedSamples.erase(accumulatedSamples.begin(), accumulatedSamples.begin() + submitSize);

                        if (!persistentBuffer.empty()) {
                            if (audioEngine.SubmitBuffer(persistentBuffer, reinterpret_cast<void*>(static_cast<uintptr_t>(bufIndex)))) {
                                buffersSubmitted++;
                            } else {
                                std::lock_guard<std::mutex> lock(freeQueueMutex);
                                freeIndices.push(bufIndex);
                            }
                        }
                    }
                    break;

                case AudioDecoder::DecodeResult::NeedMoreData:
                    noProgressCount++;
                    if (noProgressCount > 10 && !accumulatedSamples.empty() && !freeIndices.empty()) {
                        size_t bufIndex;
                        {
                            std::lock_guard<std::mutex> lock(freeQueueMutex);
                            if (freeIndices.empty()) break;
                            bufIndex = freeIndices.front();
                            freeIndices.pop();
                        }

                        auto& persistentBuffer = activeBuffers[bufIndex];
                        persistentBuffer.assign(accumulatedSamples.begin(), accumulatedSamples.end());
                        accumulatedSamples.clear();

                        if (!persistentBuffer.empty()) {
                            if (audioEngine.SubmitBuffer(persistentBuffer, reinterpret_cast<void*>(static_cast<uintptr_t>(bufIndex)))) {
                                buffersSubmitted++;
                                noProgressCount = 0;
                            } else {
                                std::lock_guard<std::mutex> lock(freeQueueMutex);
                                freeIndices.push(bufIndex);
                            }
                        }
                    }
                    continue;

                case AudioDecoder::DecodeResult::EndOfFile:
                    if (!accumulatedSamples.empty() && !freeIndices.empty()) {
                        size_t bufIndex;
                        {
                            std::lock_guard<std::mutex> lock(freeQueueMutex);
                            if (freeIndices.empty()) break;
                            bufIndex = freeIndices.front();
                            freeIndices.pop();
                        }
                        auto& persistentBuffer = activeBuffers[bufIndex];
                        persistentBuffer.assign(accumulatedSamples.begin(), accumulatedSamples.end());
                        if (audioEngine.SubmitBuffer(persistentBuffer, reinterpret_cast<void*>(static_cast<uintptr_t>(bufIndex)))) {
                            buffersSubmitted++;
                        } else {
                            std::lock_guard<std::mutex> lock(freeQueueMutex);
                            freeIndices.push(bufIndex);
                        }
                        accumulatedSamples.clear();
                    }

                    if (loop) {
                        decoder.Reset();
                        Logger::Debug("Looping audio (buffers: " + std::to_string(buffersSubmitted) + ")");
                    } else {
                        Logger::Info("End of audio - submitted " + std::to_string(buffersSubmitted) + " buffers");
                        while (audioEngine.GetQueuedBufferCount() > 0 && !header->shouldExit) {
                            std::this_thread::sleep_for(std::chrono::milliseconds(10));
                            heartbeat.MaybeUpdate();
                        }
                        audioEngine.Stop();
                        header->decoderState = STATE_IDLE;
                        header->isPlaying = 0;
                        return true;
                    }
                    noProgressCount = 0;
                    break;

                case AudioDecoder::DecodeResult::Error:
                    Logger::Error("Audio decode error");
                    header->decoderState = STATE_ERROR;
                    while (audioEngine.GetQueuedBufferCount() > 0 && !header->shouldExit) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                        heartbeat.MaybeUpdate();
                    }
                    audioEngine.Stop();
                    return false;
                }
            }

            if (audioEngine.GetQueuedBufferCount() > 0) {
                audioEngine.Start();
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        while (audioEngine.GetQueuedBufferCount() > 0 && !header->shouldExit) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            heartbeat.MaybeUpdate();
        }

        audioEngine.Stop();
        header->decoderState = STATE_IDLE;
        header->isPlaying = 0;
        Logger::Info("Audio playback stopped");
        return true;
    }
};

// ============================================================================
// WARM AUDIO DECODER MANAGER
// ============================================================================
class WarmAudioDecoderManager {
    SharedAudioControl* header;
    AudioDecoder decoder;
    Audio3DEngine audioEngine;
    HeartbeatMonitor heartbeat;
    std::unique_ptr<AudioPlaybackLoop> currentPlayback;

public:
    WarmAudioDecoderManager(SharedAudioControl* h)
        : header(h), heartbeat(h) {}

    void Run() {
        header->decoderState = STATE_IDLE;
        Logger::Info("Warm audio decoder ready - waiting for commands");

        while (!header->shouldExit) {
            heartbeat.MaybeUpdate();

            uint32_t cmd = header->command;

            if (cmd == CMD_NONE) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }

            switch (cmd) {
            case CMD_PLAY:
                HandlePlayCommand();
                break;

            case CMD_STOP:
                HandleStopCommand();
                break;

            case CMD_SEEK:
                HandleSeekCommand();
                break;

            case CMD_PAUSE:
                Logger::Info("Pause command TODO");
                header->command = CMD_NONE;
                header->commandAck = 1;
                break;

            default:
                Logger::Error(std::format("Unknown command: {}", cmd));
                header->command = CMD_NONE;
                header->commandAck = 1;
                break;
            }
        }

        Logger::Info("Audio decoder shutting down");
    }

private:
    void HandlePlayCommand() {
        std::string videoPath = header->videoPath;
        bool loop = (header->loopEnabled != 0);

        Logger::Info(std::format("Play command: path='{}', loop={}",
            videoPath, loop));

        // Acknowledge command immediately
        header->command = CMD_NONE;
        header->commandAck = 1;

        // Check if we need to reinitialize with a different video
        if (!decoder.IsInitialized() || decoder.GetCurrentVideoPath() != videoPath) {
            Logger::Info("Initializing new audio stream");

            // Clean up old decoder if exists
            if (decoder.IsInitialized()) {
                if (currentPlayback) {
                    currentPlayback.reset();
                }
                audioEngine.Cleanup();
                decoder.Cleanup();
            }

            if (!decoder.Initialize(videoPath, header)) {
                Logger::Error("Failed to initialize audio - video may have no audio stream");
                header->decoderState = STATE_IDLE;
                return;
            }

            // Initialize audio engine for this stream
            if (!audioEngine.Initialize(decoder.GetSampleRate(), decoder.GetChannels(), header)) {
                Logger::Error("Failed to initialize audio engine");
                header->decoderState = STATE_ERROR;
                return;
            }
        } else {
            Logger::Info("Reusing existing audio decoder for same video");
            decoder.Reset();
        }

        // Reset state
        header->isPlaying = 0;

        // Create playback loop and run
        currentPlayback = std::make_unique<AudioPlaybackLoop>(
            header, decoder, audioEngine, heartbeat, loop
        );

        currentPlayback->Run();
        currentPlayback.reset();

        Logger::Info("Audio playback completed, returning to idle");
    }

    void HandleStopCommand() {
        Logger::Info("Stop command received");

        if (currentPlayback) {
            currentPlayback->Stop();
        }

        header->command = CMD_NONE;
        header->commandAck = 1;
        header->isPlaying = 0;
        header->decoderState = STATE_IDLE;
    }

    void HandleSeekCommand() {
        float seekTime = header->seekTime;
        Logger::Info(std::format("Seek command: time={}s", seekTime));

        if (decoder.IsInitialized()) {
            decoder.SeekToTime(static_cast<double>(seekTime));
            Logger::Info("Seek successful");
        } else {
            Logger::Error("Cannot seek - no audio loaded");
        }

        header->command = CMD_NONE;
        header->commandAck = 1;
    }
};

// ============================================================================
// MAIN ENTRY POINT
// ============================================================================
int main(int argc, char* argv[]) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);

    if (!Logger::Init()) {
        std::cerr << "Warning: Failed to initialize debug logging" << std::endl;
    }

    Logger::Info("========== MediaFramework Warm Audio Decoder Starting ==========");

    if (argc < 2) {
        Logger::Error("Usage: AudioDecoder.exe <shared_mem_name>");
        CoUninitialize();
        return 1;
    }

    std::string sharedMemName = argv[1];

    SharedMemoryMap sharedMem(sharedMemName);
    if (!sharedMem.IsValid()) {
        Logger::Error(std::format("Failed to open shared memory: {}", sharedMemName));
        CoUninitialize();
        return 1;
    }

    auto* header = sharedMem.GetHeader();

    // Initialize header state
    header->shouldExit = 0;
    header->isPlaying = 0;
    header->decoderState = STATE_INITIALIZING;
    header->errorCode = 0;
    header->errorMessage[0] = '\0';
    header->command = CMD_NONE;
    header->commandAck = 0;
    header->lastHeartbeat = GetTickCountMilliseconds();

    Logger::Info("Shared memory initialized, entering warm audio decoder loop");

    // Run warm audio decoder manager
    try {
        WarmAudioDecoderManager manager(header);
        manager.Run();
    }
    catch (const std::exception& e) {
        Logger::Error(std::format("Exception in audio decoder manager: {}", e.what()));
        header->decoderState = STATE_ERROR;
        strncpy_s(header->errorMessage, sizeof(header->errorMessage), e.what(), _TRUNCATE);
    }

    Logger::Info("========== MediaFramework Audio Decoder Exiting ==========");

    CoUninitialize();
    return 0;
}

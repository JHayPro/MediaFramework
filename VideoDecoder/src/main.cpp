#include "pch.h"

// ============================================================================
// CONSTANTS & CONFIGURATION
// ============================================================================
constexpr uint32_t PIXEL_FORMAT_BGRA = 0;
constexpr uint32_t BYTES_PER_PIXEL = 4;
constexpr int64_t NANOSECONDS_PER_SECOND = 1'000'000'000;
constexpr uint32_t HEARTBEAT_INTERVAL_MS = 100;
constexpr uint32_t STATE_INITIALIZING = 0;
constexpr uint32_t STATE_IDLE = 1;
constexpr uint32_t STATE_PLAYING = 2;
constexpr uint32_t STATE_PAUSED = 3;
constexpr uint32_t STATE_ERROR = 4;

// Command types (matching MediaFramework)
constexpr uint32_t CMD_NONE = 0;
constexpr uint32_t CMD_PLAY = 1;
constexpr uint32_t CMD_PAUSE = 2;
constexpr uint32_t CMD_STOP = 3;
constexpr uint32_t CMD_SEEK = 4;

// ============================================================================
// ENHANCED SHARED MEMORY STRUCTURES
// ============================================================================
struct SharedVideoHeader {
    // Video dimensions and format
    volatile uint32_t width{ 0 };
    volatile uint32_t height{ 0 };
    volatile uint32_t pixelFormat{ 0 };
    volatile uint32_t dataSize{ 0 };

    // Frame control
    volatile uint32_t frameIndex{ 0 };       // Frames decoded THIS session
    volatile int64_t absoluteFrame{ 0 };     // Total frames from video start
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

    // Command system
    volatile uint32_t command{ CMD_NONE };           // Current command
    volatile uint32_t commandAck{ 0 };               // Command acknowledged (set by decoder)
    char videoPath[512]{ 0 };                        // Path for play command
    volatile int64_t seekFrame{ -1 };                // Start/seek frame
    volatile uint32_t loopEnabled{ 1 };              // Loop flag
    volatile float targetFPS{ 0.0f };                // Target FPS (0 = native)

    uint32_t reserved[2]{ 0 };  // Adjusted for new fields
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

        std::filesystem::path logPath = logDir / "MediaFramework_VideoDecoder.log";
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
// RAII WRAPPERS FOR RESOURCE MANAGEMENT
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
    SharedVideoHeader* GetHeader() const { return static_cast<SharedVideoHeader*>(buffer); }

    SharedMemoryMap(const SharedMemoryMap&) = delete;
    SharedMemoryMap& operator=(const SharedMemoryMap&) = delete;
};

struct AVDeleter {
    void operator()(AVFormatContext* ctx) const { if (ctx) avformat_close_input(&ctx); }
    void operator()(AVCodecContext* ctx) const { if (ctx) avcodec_free_context(&ctx); }
    void operator()(AVFrame* frame) const { if (frame) av_frame_free(&frame); }
    void operator()(AVPacket* packet) const { if (packet) av_packet_free(&packet); }
    void operator()(SwsContext* ctx) const { if (ctx) sws_freeContext(ctx); }
};

template<typename T>
using AVPtr = std::unique_ptr<T, AVDeleter>;

// ============================================================================
// HEARTBEAT MONITOR
// ============================================================================
class HeartbeatMonitor {
    SharedVideoHeader* header;
    std::chrono::steady_clock::time_point lastBeat;
    std::chrono::milliseconds interval;

public:
    HeartbeatMonitor(SharedVideoHeader* h, uint32_t intervalMs = HEARTBEAT_INTERVAL_MS)
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
// VIDEO DECODER CLASS
// ============================================================================
class VideoDecoder {
    AVPtr<AVFormatContext> formatCtx;
    AVPtr<AVCodecContext> codecCtx;
    AVPtr<SwsContext> swsCtx;
    AVPtr<AVFrame> rawFrame;
    AVPtr<AVFrame> bgraFrame;
    AVPtr<AVPacket> packet;

    AVStream* videoStream{ nullptr };
    int videoStreamIdx{ -1 };
    uint8_t* bgraBuffer{ nullptr };

    uint32_t width{ 0 };
    uint32_t height{ 0 };
    uint32_t dataSize{ 0 };
    double nativeFps{ 0.0 };
    bool hasAudio{ false };
    int64_t currentAbsoluteFrame{ 0 };
    std::string currentVideoPath;

    SharedVideoHeader* header{ nullptr };

public:
    ~VideoDecoder() {
        Cleanup();
    }

    void Cleanup() {
        if (bgraBuffer) {
            av_free(bgraBuffer);
            bgraBuffer = nullptr;
        }
        
        packet.reset();
        bgraFrame.reset();
        rawFrame.reset();
        swsCtx.reset();
        codecCtx.reset();
        formatCtx.reset();
        
        videoStream = nullptr;
        videoStreamIdx = -1;
        width = 0;
        height = 0;
        dataSize = 0;
        currentVideoPath.clear();
    }

    bool IsInitialized() const { return formatCtx != nullptr; }
    std::string GetCurrentVideoPath() const { return currentVideoPath; }

    bool Initialize(const std::string& videoPath, SharedVideoHeader* h) {
        header = h;
        header->decoderState = STATE_INITIALIZING;

        // Open video file
        AVFormatContext* rawFormatCtx = nullptr;
        int ret = avformat_open_input(&rawFormatCtx, videoPath.c_str(), nullptr, nullptr);
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

        // Find video stream
        videoStreamIdx = av_find_best_stream(formatCtx.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        if (videoStreamIdx < 0) {
            SetError("No video stream found", videoStreamIdx);
            return false;
        }
        videoStream = formatCtx->streams[videoStreamIdx];

        // Check for audio stream
        int audioStreamIdx = av_find_best_stream(formatCtx.get(), AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
        hasAudio = (audioStreamIdx >= 0);
        header->hasAudio = hasAudio ? 1 : 0;

        Logger::Info(std::format("Video has audio: {}", hasAudio ? "YES" : "NO"));

        // Setup decoder
        const AVCodec* decoder = avcodec_find_decoder(videoStream->codecpar->codec_id);
        if (!decoder) {
            SetError("Video decoder not found");
            return false;
        }

        AVCodecContext* rawCodecCtx = avcodec_alloc_context3(decoder);
        if (!rawCodecCtx) {
            SetError("Failed to allocate codec context");
            return false;
        }
        codecCtx.reset(rawCodecCtx);

        ret = avcodec_parameters_to_context(codecCtx.get(), videoStream->codecpar);
        if (ret < 0) {
            SetError("Failed to copy codec parameters", ret);
            return false;
        }

        ret = avcodec_open2(codecCtx.get(), decoder, nullptr);
        if (ret < 0) {
            SetError("Failed to open codec", ret);
            return false;
        }

        // Store video properties
        width = codecCtx->width;
        height = codecCtx->height;
        dataSize = width * height * BYTES_PER_PIXEL;
        nativeFps = av_q2d(videoStream->r_frame_rate);

        // Setup scaler
        SwsContext* rawSwsCtx = sws_getContext(
            width, height, codecCtx->pix_fmt,
            width, height, AV_PIX_FMT_BGRA,
            SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (!rawSwsCtx) {
            SetError("Failed to create scaler");
            return false;
        }
        swsCtx.reset(rawSwsCtx);

        // Allocate frames
        AVFrame* rawRawFrame = av_frame_alloc();
        AVFrame* rawBgraFrame = av_frame_alloc();
        if (!rawRawFrame || !rawBgraFrame) {
            SetError("Failed to allocate frames");
            if (rawRawFrame) av_frame_free(&rawRawFrame);
            if (rawBgraFrame) av_frame_free(&rawBgraFrame);
            return false;
        }
        rawFrame.reset(rawRawFrame);
        bgraFrame.reset(rawBgraFrame);

        // Allocate BGRA buffer
        size_t bufferSize = static_cast<size_t>(width * BYTES_PER_PIXEL) * height;
        bgraBuffer = static_cast<uint8_t*>(av_malloc(bufferSize));
        if (!bgraBuffer) {
            SetError("Failed to allocate BGRA buffer");
            return false;
        }

        av_image_fill_arrays(bgraFrame->data, bgraFrame->linesize, bgraBuffer,
            AV_PIX_FMT_BGRA, width, height, 1);
        bgraFrame->width = width;
        bgraFrame->height = height;
        bgraFrame->format = AV_PIX_FMT_BGRA;

        // Allocate packet
        AVPacket* rawPacket = av_packet_alloc();
        if (!rawPacket) {
            SetError("Failed to allocate packet");
            return false;
        }
        packet.reset(rawPacket);

        currentVideoPath = videoPath;
        currentAbsoluteFrame = 0;

        // Update header with video info
        header->width = width;
        header->height = height;
        header->pixelFormat = PIXEL_FORMAT_BGRA;
        header->dataSize = dataSize;

        Logger::Info(std::format("Decoder initialized: {}x{} @ {} FPS", width, height, nativeFps));
        return true;
    }

    bool SeekToFrame(int64_t frameNumber) {
        if (frameNumber <= 0) {
            currentAbsoluteFrame = 0;
            av_seek_frame(formatCtx.get(), videoStreamIdx, 0, AVSEEK_FLAG_BACKWARD);
            avcodec_flush_buffers(codecCtx.get());
            return true;
        }

        int64_t timestamp = av_rescale_q(frameNumber,
            av_inv_q(videoStream->r_frame_rate), videoStream->time_base);

        int ret = av_seek_frame(formatCtx.get(), videoStreamIdx, timestamp, AVSEEK_FLAG_BACKWARD);
        if (ret < 0) {
            Logger::Error("Failed to seek to frame " + std::to_string(frameNumber), ret);
            return false;
        }

        avcodec_flush_buffers(codecCtx.get());
        currentAbsoluteFrame = frameNumber;

        Logger::Debug("Seeked to absolute frame: " + std::to_string(frameNumber));
        return true;
    }

    void ResetToStart() {
        av_seek_frame(formatCtx.get(), videoStreamIdx, 0, AVSEEK_FLAG_BACKWARD);
        avcodec_flush_buffers(codecCtx.get());
        currentAbsoluteFrame = 0;
    }

    enum class DecodeResult { Success, NeedMoreData, EndOfFile, Error };

    DecodeResult DecodeNextFrame(uint8_t* outputBuffer) {
        int ret = avcodec_receive_frame(codecCtx.get(), rawFrame.get());

        if (ret == 0) {
            sws_scale(swsCtx.get(), rawFrame->data, rawFrame->linesize,
                0, height, bgraFrame->data, bgraFrame->linesize);

            memcpy(outputBuffer, bgraFrame->data[0], dataSize);

            if (rawFrame->pts != AV_NOPTS_VALUE) {
                double pts = rawFrame->pts * av_q2d(videoStream->time_base);
                header->videoPTS = pts;
            }

            currentAbsoluteFrame++;
            header->absoluteFrame = currentAbsoluteFrame;

            av_frame_unref(rawFrame.get());
            return DecodeResult::Success;
        }

        if (ret != AVERROR(EAGAIN)) {
            SetError("avcodec_receive_frame failed", ret);
            return DecodeResult::Error;
        }

        // Need to feed more packets
        while (true) {
            ret = av_read_frame(formatCtx.get(), packet.get());

            if (ret == AVERROR_EOF) {
                return DecodeResult::EndOfFile;
            }

            if (ret < 0) {
                SetError("av_read_frame failed", ret);
                return DecodeResult::Error;
            }

            if (packet->stream_index == videoStreamIdx) {
                ret = avcodec_send_packet(codecCtx.get(), packet.get());
                av_packet_unref(packet.get());

                if (ret < 0) {
                    SetError("avcodec_send_packet failed", ret);
                    return DecodeResult::Error;
                }

                return DecodeResult::NeedMoreData;
            }

            av_packet_unref(packet.get());
        }
    }

    uint32_t GetWidth() const { return width; }
    uint32_t GetHeight() const { return height; }
    uint32_t GetDataSize() const { return dataSize; }
    double GetNativeFps() const { return nativeFps; }
    bool HasAudio() const { return hasAudio; }

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
// PLAYBACK LOOP
// ============================================================================
class PlaybackLoop {
    SharedVideoHeader* header;
    VideoDecoder& decoder;
    HeartbeatMonitor& heartbeat;

    double effectiveFps;
    std::chrono::nanoseconds frameDuration;
    std::chrono::steady_clock::time_point nextFrameTime;
    bool loop;
    bool shouldStop{ false };

public:
    PlaybackLoop(SharedVideoHeader* h, VideoDecoder& d, HeartbeatMonitor& hb, double targetFps, bool loopVideo)
        : header(h), decoder(d), heartbeat(hb), loop(loopVideo)
    {
        effectiveFps = (targetFps > 0.0) ? targetFps : decoder.GetNativeFps();
        frameDuration = std::chrono::nanoseconds(static_cast<int64_t>(NANOSECONDS_PER_SECOND / effectiveFps));
        nextFrameTime = std::chrono::steady_clock::now();

        header->videoFPS = effectiveFps;

        Logger::Info(std::format("Playback configured: {} FPS, Loop: {}", effectiveFps, loop ? "YES" : "NO"));
    }

    void Stop() { shouldStop = true; }

    bool Run(uint8_t* buffer0, uint8_t* buffer1) {
        header->decoderState = STATE_PLAYING;
        Logger::Info("Starting playback loop");

        uint32_t framesDecoded = 0;
        auto startTime = std::chrono::steady_clock::now();
        shouldStop = false;

        while (!header->shouldExit && !shouldStop) {
            heartbeat.MaybeUpdate();

            // Check for stop command
            if (header->command == CMD_STOP) {
                header->command = CMD_NONE;
                header->commandAck = 1;
                Logger::Info("Stop command received");
                break;
            }

            uint8_t* currentBuffer = (header->writeIndex == 0) ? buffer0 : buffer1;

            auto result = decoder.DecodeNextFrame(currentBuffer);

            switch (result) {
            case VideoDecoder::DecodeResult::Success:
                PublishFrame();
                ThrottleFramerate();
                framesDecoded++;
                break;

            case VideoDecoder::DecodeResult::NeedMoreData:
                heartbeat.MaybeUpdate();
                continue;

            case VideoDecoder::DecodeResult::EndOfFile:
                if (loop) {
                    decoder.ResetToStart();
                    nextFrameTime = std::chrono::steady_clock::now();
                    Logger::Debug(std::format("Looping video (frame {})", framesDecoded));
                }
                else {
                    Logger::Info(std::format("End of video - decoded {} frames", framesDecoded));
                    header->decoderState = STATE_IDLE;
                    return true;  // Natural end
                }
                break;

            case VideoDecoder::DecodeResult::Error:
                Logger::Error(std::format("Decode error at frame {}", framesDecoded));
                header->decoderState = STATE_ERROR;
                return false;
            }
        }

        auto endTime = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::seconds>(endTime - startTime).count();
        Logger::Info(std::format("Playback stopped: {} frames in {}s", framesDecoded, duration));
        header->decoderState = STATE_IDLE;
        return true;
    }

private:
    void PublishFrame() {
        header->readIndex = header->writeIndex;
        header->writeIndex = 1 - header->writeIndex;
        header->frameIndex++;
        header->isReady = 1;
    }

    void ThrottleFramerate() {
        nextFrameTime += frameDuration;
        auto now = std::chrono::steady_clock::now();

        if (nextFrameTime > now) {
            std::this_thread::sleep_until(nextFrameTime);
        }
        else {
            if ((now - nextFrameTime) > std::chrono::milliseconds(100)) {
                Logger::Debug(std::format("Frame timing reset - was behind by {}ms",
                    std::chrono::duration_cast<std::chrono::milliseconds>(now - nextFrameTime).count()));
                nextFrameTime = now;
            }
        }
    }
};

// ============================================================================
// WARM DECODER MANAGER
// ============================================================================
class WarmDecoderManager {
    SharedVideoHeader* header;
    VideoDecoder decoder;
    HeartbeatMonitor heartbeat;
    std::unique_ptr<PlaybackLoop> currentPlayback;
    
    uint8_t* buffer0{ nullptr };
    uint8_t* buffer1{ nullptr };

public:
    WarmDecoderManager(SharedVideoHeader* h, void* sharedBuffer)
        : header(h), heartbeat(h)
    {
        // Calculate buffer pointers
        uint8_t* baseBuffer = static_cast<uint8_t*>(sharedBuffer) + sizeof(SharedVideoHeader);
        buffer0 = baseBuffer;
        // buffer1 will be set after we know dataSize
    }

    void Run() {
        header->decoderState = STATE_IDLE;
        Logger::Info("Warm decoder ready - waiting for commands");

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

        Logger::Info("Decoder shutting down");
    }

private:
    void HandlePlayCommand() {
        std::string videoPath = header->videoPath;
        int64_t seekFrame = header->seekFrame;
        bool loop = (header->loopEnabled != 0);
        float targetFps = header->targetFPS;

        Logger::Info(std::format("Play command: path='{}', seekFrame={}, loop={}, fps={}",
            videoPath, seekFrame, loop, targetFps));

        // Acknowledge command immediately
        header->command = CMD_NONE;
        header->commandAck = 1;

        // Check if we need to reinitialize with a different video
        if (!decoder.IsInitialized() || decoder.GetCurrentVideoPath() != videoPath) {
            Logger::Info("Initializing new video");
            
            // Clean up old decoder if exists
            if (decoder.IsInitialized()) {
                decoder.Cleanup();
            }

            if (!decoder.Initialize(videoPath, header)) {
                Logger::Error("Failed to initialize video");
                header->decoderState = STATE_ERROR;
                return;
            }

            // Update buffer1 pointer now that we know dataSize
            buffer1 = buffer0 + header->dataSize;
        }
        else {
            Logger::Info("Reusing existing decoder for same video");
        }

        // Seek if requested
        if (seekFrame >= 0) {
            if (!decoder.SeekToFrame(seekFrame)) {
                Logger::Error("Failed to seek to frame, starting from beginning");
            }
        }
        else {
            decoder.ResetToStart();
        }

        // Reset frame counters
        header->frameIndex = 0;
        header->isReady = 0;

        // Create playback loop and run
        currentPlayback = std::make_unique<PlaybackLoop>(
            header, decoder, heartbeat, 
            static_cast<double>(targetFps), loop
        );

        currentPlayback->Run(buffer0, buffer1);
        currentPlayback.reset();

        Logger::Info("Playback completed, returning to idle");
    }

    void HandleStopCommand() {
        Logger::Info("Stop command received");
        
        if (currentPlayback) {
            currentPlayback->Stop();
        }

        header->command = CMD_NONE;
        header->commandAck = 1;
        header->isReady = 0;
        header->decoderState = STATE_IDLE;
    }

    void HandleSeekCommand() {
        int64_t seekFrame = header->seekFrame;
        Logger::Info(std::format("Seek command: frame={}", seekFrame));

        if (decoder.IsInitialized()) {
            if (decoder.SeekToFrame(seekFrame)) {
                Logger::Info("Seek successful");
            }
            else {
                Logger::Error("Seek failed");
            }
        }
        else {
            Logger::Error("Cannot seek - no video loaded");
        }

        header->command = CMD_NONE;
        header->commandAck = 1;
    }
};

// ============================================================================
// MAIN ENTRY POINT
// ============================================================================
int main(int argc, char* argv[]) {
    if (!Logger::Init()) {
        std::cerr << "Warning: Failed to initialize debug logging" << std::endl;
    }

    Logger::Info("========== MediaFramework Warm Video Decoder Starting ==========");

    if (argc < 2) {
        Logger::Error("Usage: VideoDecoder.exe <shared_mem_name>");
        return 1;
    }

    std::string sharedMemName = argv[1];

    // Open shared memory
    SharedMemoryMap sharedMem(sharedMemName);
    if (!sharedMem.IsValid()) {
        Logger::Error(std::format("Failed to open shared memory: {}", sharedMemName));
        return 1;
    }

    auto* header = sharedMem.GetHeader();

    // Initialize header state
    header->shouldExit = 0;
    header->writeIndex = 0;
    header->readIndex = 1;
    header->frameIndex = 0;
    header->isReady = 0;
    header->errorCode = 0;
    header->errorMessage[0] = '\0';
    header->decoderState = STATE_INITIALIZING;
    header->command = CMD_NONE;
    header->commandAck = 0;

    // Update heartbeat immediately
    header->lastHeartbeat = GetTickCountMilliseconds();

    Logger::Info("Shared memory initialized, entering warm decoder loop");

    // Run warm decoder manager
    try {
        WarmDecoderManager manager(header, sharedMem.GetBuffer());
        manager.Run();
    }
    catch (const std::exception& e) {
        Logger::Error(std::format("Exception in decoder manager: {}", e.what()));
        header->decoderState = STATE_ERROR;
        strncpy_s(header->errorMessage, sizeof(header->errorMessage), e.what(), _TRUNCATE);
    }

    // Cleanup
    header->isReady = 0;
    Logger::Info("========== MediaFramework Video Decoder Exiting ==========");

    return 0;
}

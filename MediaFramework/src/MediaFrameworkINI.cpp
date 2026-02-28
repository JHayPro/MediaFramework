// MediaFrameworkINI.cpp
// Complete implementation with packet building
#include "MediaFrameworkINI.h"

namespace MediaFramework::INI
{
    // Thread-local storage for last error message
    static thread_local std::string g_lastParseError;

    /* =========================================================
       IniFile implementations
       ========================================================= */

    bool IniFile::Load(const std::string& path)
    {
        std::ifstream file(path);
        if (!file) return false;

        std::string line;
        std::string currentSection;

        while (std::getline(file, line))
        {
            line = Parsers::Trim(line);
            if (line.empty() || line[0] == ';' || line[0] == '#') continue;

            if (line[0] == '[')
            {
                size_t end = line.find(']');
                if (end != std::string::npos)
                {
                    currentSection = Parsers::Trim(line.substr(1, end - 1));
                }
                continue;
            }

            size_t eq = line.find('=');
            if (eq != std::string::npos)
            {
                std::string key = Parsers::Trim(line.substr(0, eq));
                std::string value = Parsers::Trim(line.substr(eq + 1));
                data_[currentSection][key] = value;
            }
        }

        return true;
    }

    void IniFile::Merge(const IniFile& other)
    {
        // Actually override values
        for (const auto& [section, keys] : other.data_)
        {
            for (const auto& [key, value] : keys)
            {
                data_[section][key] = value;  // ✅ Override!
            }
        }
    }

    IniValue IniFile::Get(const std::string& section, const std::string& key) const
    {
        IniValue v;
        auto sit = data_.find(section);
        if (sit != data_.end())
        {
            auto kit = sit->second.find(key);
            if (kit != sit->second.end())
            {
                v.raw = kit->second;
                v.exists = true;
            }
        }
        return v;
    }

    bool IniFile::HasSection(const std::string& section) const
    {
        return data_.find(section) != data_.end();
    }

    /* =========================================================
       Cascading INI loading
       ========================================================= */

    std::string MediaFrameworkINIParser::GetFolderINIPath(const std::string& filePath)
    {
        std::filesystem::path p(filePath);
        auto parent = p.parent_path();
        return (parent / "MediaFramework.ini").string();
    }

    IniFile MediaFrameworkINIParser::LoadCascadingINI(const std::string& childIniPath,
                                                             const std::string& fileIniPath)
    {
        IniFile result;

        if (std::filesystem::exists(childIniPath))
        {
            result.Load(childIniPath);
        }

        std::string folderPath = GetFolderINIPath(fileIniPath);
        if (std::filesystem::exists(folderPath))
        {
            IniFile folderIni;
            folderIni.Load(folderPath);
            result.Merge(folderIni);
        }

        if (std::filesystem::exists(fileIniPath))
        {
            IniFile fileIni;
            fileIni.Load(fileIniPath);
            result.Merge(fileIni);
        }

        return result;
    }

    /* =========================================================
       Helper: Build MediaCommandPackets from parsed config
       ========================================================= */

    struct ParsedConfig
    {
        // [Media]
        repeatSetting repeatMode = repeatSetting::None;
        bool resumePlayback = false;
        RenderMode renderMode = RenderMode::Fullscreen;
        float windowX = 0.0f, windowY = 0.0f, windowW = 1.0f, windowH = 1.0f;
        RenderPipelineStage renderStage = RenderPipelineStage::Post;
        RE::UI_DEPTH_PRIORITY depth = RE::UI_DEPTH_PRIORITY::kStandard;
        std::string preUILevel;
        std::string preUICustomMenuName;
        HookedMenuName menuToHook = HookedMenuName::LoadingMenu;

        // [Video]
        float fadeColorRGBA[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        float fadeInSeconds = 0.0f;
        float fadeOutSeconds = 0.0f;
        ScaleModes scaleMode = ScaleModes::Fit;
        bool maintainAspect = true;
        bool blackBars = true;

        // [Audio]
        bool enableAudio = true;
        float volume = 1.0f; // 0-1.25
        bool spatialAudio = false;

        // [Input]
        bool continueKey = false;
        bool allowSkip = false;

        // [Loading Menu]
        bool enableLoadingIcon = false;
        bool enableLevel = false;
        bool enableTextHint = false;
        std::string textHintString;
    };

    static void BuildCommandPackets(const ParsedConfig& config, 
                                    std::vector<MediaCommandPacket>& outCommands,
                                    ParsedMediaDefinition& outResult)
    {
        outCommands.clear();

        // 1. SetVolume command
        if (config.enableAudio)
        {
            MediaCommandPacket volCmd{};
            volCmd.type = MediaCommandType::SetVolume;
            volCmd.params.volume.size = sizeof(VolumeParams);
            volCmd.params.volume.volume = config.volume;
            outCommands.push_back(volCmd);
        }

        // 2. SetFade command (if fading enabled)
        if (config.fadeInSeconds > 0.0f || config.fadeOutSeconds > 0.0f)
        {
            MediaCommandPacket fadeCmd{};
            fadeCmd.type = MediaCommandType::SetFade;
            fadeCmd.params.fade.size = sizeof(FadeParams);
            fadeCmd.params.fade.fadeInSeconds = config.fadeInSeconds;
            fadeCmd.params.fade.fadeOutSeconds = config.fadeOutSeconds;
            memcpy(fadeCmd.params.fade.color, config.fadeColorRGBA, sizeof(float) * 4);
            outCommands.push_back(fadeCmd);
        }

        // 3. SetRenderMode command
        {
            MediaCommandPacket renderCmd{};
            renderCmd.type = MediaCommandType::SetRenderMode;
            renderCmd.params.renderMode.size = sizeof(RenderModeParams);
            renderCmd.params.renderMode.mode = config.renderMode;
            renderCmd.params.renderMode.x = config.windowX;
            renderCmd.params.renderMode.y = config.windowY;
            renderCmd.params.renderMode.w = config.windowW;
            renderCmd.params.renderMode.h = config.windowH;
            renderCmd.params.renderMode.stage = config.renderStage;
            renderCmd.params.renderMode.depth = config.depth;
            renderCmd.params.renderMode.menuName = config.menuToHook;
            
            // Store custom menu name in outResult and use its c_str()
            outResult.ownedCustomMenuName = config.preUICustomMenuName;
            renderCmd.params.renderMode.customMenuName = 
                outResult.ownedCustomMenuName.empty() ? nullptr : outResult.ownedCustomMenuName.c_str();
            
            renderCmd.params.renderMode.scaleMode = config.scaleMode;
            renderCmd.params.renderMode.maintainAspect = config.maintainAspect;
            renderCmd.params.renderMode.blackBars = config.blackBars;
            
            outCommands.push_back(renderCmd);
        }

        // 4. Play command (auto-start)
        {
            MediaCommandPacket playCmd{};
            playCmd.type = MediaCommandType::Play;
            outCommands.push_back(playCmd);
        }
    }

    /* =========================================================
       Schema building with config capture
       ========================================================= */

    static Schema BuildSchemaWithConfig(ParsedConfig& config)
    {
        Schema schema;

        /* ========== [Media] Section ========== */

        schema.AddField<repeatSetting>(
            "Media", "RepeatSetting", 1, repeatSetting::None,
            &config.repeatMode,
            [](const IniValue& v, repeatSetting& out, const SchemaContext&)
            {
                if (!v.exists) return;
                
                static const std::unordered_map<std::string, repeatSetting> mapping = {
                    {"None", repeatSetting::None},
                    {"Loop", repeatSetting::Loop},
                    {"PingPong", repeatSetting::PingPong}
                };
                
                out = Parsers::ParseEnum(v.raw, mapping, repeatSetting::None);
            }
        );

        schema.AddField<bool>(
            "Media", "ResumeVideoPlayback", 1, false,
            &config.resumePlayback,
            [](const IniValue& v, bool& out, const SchemaContext&)
            {
                if (!v.exists) return;
                out = Parsers::ParseBool(v.raw, false);
            }
        );

        schema.AddField<RenderMode>(
            "Media", "RenderMode", 1, RenderMode::Fullscreen,
            &config.renderMode,
            [](const IniValue& v, RenderMode& out, const SchemaContext&)
            {
                if (!v.exists) return;
                
                static const std::unordered_map<std::string, RenderMode> mapping = {
                    {"Fullscreen", RenderMode::Fullscreen},
                    {"Window", RenderMode::Window}
                };
                
                out = Parsers::ParseEnum(v.raw, mapping, RenderMode::Fullscreen);
            }
        );

        schema.AddField<float>(
            "Media", "WindowBounds", 1, 0.0f,
            &config.windowX,
            [&config](const IniValue& v, float&, const SchemaContext&)
            {
                if (!v.exists) return;
                Parsers::ParseBounds(v.raw, config.windowX, config.windowY, 
                                   config.windowW, config.windowH);
            }
        );

        schema.AddField<RenderPipelineStage>(
            "Media", "RenderPipelineStage", 1, RenderPipelineStage::Post,
            &config.renderStage,
            [](const IniValue& v, RenderPipelineStage& out, const SchemaContext&)
            {
                if (!v.exists) return;
                
                static const std::unordered_map<std::string, RenderPipelineStage> mapping = {
                    {"Post", RenderPipelineStage::Post},
                    {"PreUI", RenderPipelineStage::PreUI},
                    {"MenuHook", RenderPipelineStage::MenuHook},
                    {"TextureSwap", RenderPipelineStage::TextureSwap}
                };
                
                out = Parsers::ParseEnum(v.raw, mapping, RenderPipelineStage::Post);
            }
        );

        schema.AddField<std::string>(
            "Media", "PreUILevel", 1, "",
            &config.preUILevel,
            [](const IniValue& v, std::string& out, const SchemaContext&)
            {
                if (!v.exists) return;
                out = Parsers::Trim(v.raw);
            }
        );

        schema.AddField<std::string>(
            "Media", "PreUICustomMenuName", 1, "MediaFrameworkMenu",
            &config.preUICustomMenuName,
            [](const IniValue& v, std::string& out, const SchemaContext&)
            {
                if (!v.exists) return;
                out = Parsers::Trim(v.raw);
            }
        );

        schema.AddField<HookedMenuName>(
            "Media", "MenuToHook", 1, HookedMenuName::LoadingMenu,
            &config.menuToHook,
            [](const IniValue& v, HookedMenuName& out, const SchemaContext&)
            {
                if (!v.exists) return;
                
                static const std::unordered_map<std::string, HookedMenuName> mapping = {
                    {"LoadingMenu", HookedMenuName::LoadingMenu}
                };
                
                out = Parsers::ParseEnum(v.raw, mapping, HookedMenuName::LoadingMenu);
            }
        );

        /* ========== [Video] Section ========== */

        schema.AddField<float>(
            "Video", "FadeColorRGBA", 1, 0.0f,
            &config.fadeColorRGBA[0],
            [&config](const IniValue& v, float&, const SchemaContext&)
            {
                if (!v.exists) return;
                Parsers::ParseRGBA(v.raw, config.fadeColorRGBA);
            }
        );

        schema.AddField<float>(
            "Video", "FadeInSeconds", 1, 0.0f,
            &config.fadeInSeconds,
            [](const IniValue& v, float& out, const SchemaContext&)
            {
                if (!v.exists) return;
                out = Parsers::ParseFloat(v.raw, 0.0f, 0.0f, FLT_MAX);
            }
        );

        schema.AddField<float>(
            "Video", "FadeOutSeconds", 1, 0.0f,
            &config.fadeOutSeconds,
            [](const IniValue& v, float& out, const SchemaContext&)
            {
                if (!v.exists) return;
                out = Parsers::ParseFloat(v.raw, 0.0f, 0.0f, FLT_MAX);
            }
        );

        schema.AddField<ScaleModes>(
            "Video", "ScaleMode", 1, ScaleModes::Fit,
            &config.scaleMode,
            [](const IniValue& v, ScaleModes& out, const SchemaContext&)
            {
                if (!v.exists) return;
                
                static const std::unordered_map<std::string, ScaleModes> mapping = {
                    {"Fit", ScaleModes::Fit},
                    {"Fill", ScaleModes::Fill},
                    {"Stretch", ScaleModes::Stretch}
                };
                
                out = Parsers::ParseEnum(v.raw, mapping, ScaleModes::Fit);
            }
        );

        schema.AddField<bool>(
            "Video", "MaintainAspect", 1, true,
            &config.maintainAspect,
            [](const IniValue& v, bool& out, const SchemaContext&)
            {
                if (!v.exists) return;
                out = Parsers::ParseBool(v.raw, true);
            }
        );

        schema.AddField<bool>(
            "Video", "BlackBars", 1, true,
            &config.blackBars,
            [](const IniValue& v, bool& out, const SchemaContext&)
            {
                if (!v.exists) return;
                out = Parsers::ParseBool(v.raw, true);
            }
        );

        /* ========== [Audio] Section ========== */

        schema.AddField<bool>(
            "Audio", "EnableAudio", 1, true,
            &config.enableAudio,
            [](const IniValue& v, bool& out, const SchemaContext&)
            {
                if (!v.exists) return;
                out = Parsers::ParseBool(v.raw, true);
            }
        );

        schema.AddField<float>(
            "Audio", "Volume", 1, 1.0f,
            &config.volume,
            [](const IniValue& v, float& out, const SchemaContext&)
            {
                if (!v.exists) return;
                int percent = Parsers::ParseInt(v.raw, 100, 0, 125);
                float normalized = percent / 100.0f;
                out = normalized * normalized; // (Volume/100)^2
            }
        );

        schema.AddField<bool>(
            "Audio", "SpatialAudio", 1, false,
            &config.spatialAudio,
            [](const IniValue& v, bool& out, const SchemaContext&)
            {
                if (!v.exists) return;
                out = Parsers::ParseBool(v.raw, false);
            }
        );

        /* ========== [Input] Section ========== */

        schema.AddField<bool>(
            "Input", "ContinueKey", 1, false,
            &config.continueKey,
            [](const IniValue& v, bool& out, const SchemaContext&)
            {
                if (!v.exists) return;
                out = !Parsers::Trim(v.raw).empty();
            }
        );

        schema.AddField<bool>(
            "Input", "AllowSkip", 1, false,
            &config.allowSkip,
            [](const IniValue& v, bool& out, const SchemaContext&)
            {
                if (!v.exists) return;
                out = Parsers::ParseBool(v.raw, false);
            }
        );

        /* ========== [Loading Menu] Section ========== */

        schema.AddField<bool>(
            "Loading Menu", "EnableLoadingIcon", 1, false,
            &config.enableLoadingIcon,
            [](const IniValue& v, bool& out, const SchemaContext&)
            {
                if (!v.exists) return;
                out = Parsers::ParseBool(v.raw, false);
            }
        );

        schema.AddField<bool>(
            "Loading Menu", "EnableLevel", 1, false,
            &config.enableLevel,
            [](const IniValue& v, bool& out, const SchemaContext&)
            {
                if (!v.exists) return;
                out = Parsers::ParseBool(v.raw, false);
            }
        );

        schema.AddField<bool>(
            "Loading Menu", "EnableTextHint", 1, false,
            &config.enableTextHint,
            [](const IniValue& v, bool& out, const SchemaContext&)
            {
                if (!v.exists) return;
                out = Parsers::ParseBool(v.raw, false);
            }
        );

        schema.AddField<std::string>(
            "Loading Menu", "TextHintString", 1, "",
            &config.textHintString,
            [](const IniValue& v, std::string& out, const SchemaContext&)
            {
                if (!v.exists) return;
                out = Parsers::Trim(v.raw);
            }
        );

        return schema;
    }

    /* =========================================================
       Updated main parsing function
       ========================================================= */

    ParsedMediaDefinition MediaFrameworkINIParser::ParseMediaDefinition(
        const std::string& childIniPath,
        const std::string& fileIniPath)
    {
        ParsedMediaDefinition result{};
        result.parseSuccess = false;
        g_lastParseError.clear();

        try
        {
            // Load cascading INI
            IniFile ini = LoadCascadingINI(childIniPath, fileIniPath);

            // Create config storage
            ParsedConfig config;

            // Build schema and parse
            Schema schema = BuildSchemaWithConfig(config);
            SchemaContext ctx;
            ctx.iniVersion = 1;
            
            schema.ApplyAll(ini, ctx);

            // Build command packets from parsed config
            BuildCommandPackets(config, result.initialCommands, result);

            // Set media path (extract from fileIniPath)
            //TODO Don't hard code this
            //std::filesystem::path p(fileIniPath);
            //result.ownedMediaPath = p.replace_extension("dds").string();
            //result.createParams.size = sizeof(MediaCreateParams);
            //result.createParams.mediaPath = result.ownedMediaPath.c_str();
            
            result.parseSuccess = true;
            
        }
        catch (const std::exception& ex)
        {
            result.parseSuccess = false;
            g_lastParseError = std::string("Parse error: ") + ex.what();
            result.errorMessage = g_lastParseError;
        }
        catch (...)
        {
            result.parseSuccess = false;
            g_lastParseError = "Unknown parse error";
            result.errorMessage = g_lastParseError;
        }

        return result;
    }

    const char* GetLastParseErrorInternal()
    {
        return g_lastParseError.c_str();
    }

} // namespace MediaFramework::INI

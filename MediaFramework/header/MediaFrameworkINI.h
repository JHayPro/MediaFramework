// MediaFrameworkINI.h
// ABI-safe INI parsing extension for MediaFramework
#pragma once

#include "MediaFrameworkAPI.h"

namespace MediaFramework::INI
{
    /* =========================================================
       Core INI structures (internal use only)
       ========================================================= */

    struct IniValue
    {
        std::string raw;
        bool exists = false;
    };

    class IniFile
    {
    public:
        bool Load(const std::string& path);
        void Merge(const IniFile& other); // Override with other's values
        IniValue Get(const std::string& section, const std::string& key) const;
        bool HasSection(const std::string& section) const;

    private:
        using Section = std::unordered_map<std::string, std::string>;
        std::unordered_map<std::string, Section> data_;
    };

    /* =========================================================
       Version context (safe across ABI boundaries)
       ========================================================= */

    struct SchemaContext
    {
        uint32_t iniVersion = 1;           // Version in INI
        uint32_t frameworkVersion = 1;      // MediaFramework version
        uint32_t childVersion = 1;          // Child plugin version
        bool strictMode = false;            // Error on unknown keys
    };

    /* =========================================================
       Parsed result (ABI-safe output)
       ========================================================= */

    struct ParsedMediaDefinition
    {
        std::vector<MediaCommandPacket> initialCommands;
        bool parseSuccess;
        std::string errorMessage; // Empty on success
        std::string ownedMediaPath;
        std::string ownedCustomMenuName;
    };

    /* =========================================================
       Field schema system
       ========================================================= */

    class ISchemaField
    {
    public:
        virtual ~ISchemaField() = default;
        virtual void Apply(const IniFile& ini, SchemaContext& ctx) = 0;
    };

    template<typename T>
    class SchemaField final : public ISchemaField
    {
    public:
        using ParserFn = std::function<void(const IniValue&, T&, const SchemaContext&)>;

        SchemaField(
            std::string section,
            std::string key,
            uint32_t introducedVersion,
            const T& defaultValue,
            T* output,
            ParserFn parser)
            :
            section_(std::move(section)),
            key_(std::move(key)),
            introducedVersion_(introducedVersion),
            default_(defaultValue),
            out_(output),
            parser_(std::move(parser))
        {}

        void Apply(const IniFile& ini, SchemaContext& ctx) override
        {
            // Field not introduced yet → use default
            if (ctx.iniVersion < introducedVersion_)
            {
                *out_ = default_;
                return;
            }

            IniValue v = ini.Get(section_, key_);

            // Always start from default (safe fallback)
            *out_ = default_;

            // Let parser decide what to do (can override default)
            parser_(v, *out_, ctx);
        }

    private:
        std::string section_;
        std::string key_;
        uint32_t introducedVersion_;
        T default_;
        T* out_;
        ParserFn parser_;
    };

    /* =========================================================
       Schema registry
       ========================================================= */

    class Schema
    {
    public:
        Schema() = default;

        template<typename T>
        void AddField(
            std::string section,
            std::string key,
            uint32_t introducedVersion,
            const T& defaultValue,
            T* output,
            typename SchemaField<T>::ParserFn parser)
        {
            fields_.emplace_back(
                std::make_unique<SchemaField<T>>(
                    std::move(section),
                    std::move(key),
                    introducedVersion,
                    defaultValue,
                    output,
                    std::move(parser)
                )
            );
        }

        void ApplyAll(const IniFile& ini, SchemaContext& ctx) const
        {
            for (const auto& field : fields_)
            {
                field->Apply(ini, ctx);
            }
        }

    private:
        std::vector<std::unique_ptr<ISchemaField>> fields_;
    };

    /* =========================================================
       Parsing utilities (internal helpers)
       ========================================================= */

    namespace Parsers
    {
        // Trim whitespace
        inline std::string Trim(const std::string& str)
        {
            size_t start = str.find_first_not_of(" \t\r\n");
            if (start == std::string::npos) return "";
            size_t end = str.find_last_not_of(" \t\r\n");
            return str.substr(start, end - start + 1);
        }

        // Parse bool (True/False, case-insensitive)
        inline bool ParseBool(const std::string& str, bool defaultVal = false)
        {
            std::string s = Trim(str);
            if (s.empty()) return defaultVal;
            
            // Convert to lowercase for comparison
            std::string lower = s;
            for (char& c : lower) c = std::tolower(c);
            
            if (lower == "true" || lower == "1" || lower == "yes" || lower == "on")
                return true;
            if (lower == "false" || lower == "0" || lower == "no" || lower == "off")
                return false;
            
            return defaultVal;
        }

        // Parse float with bounds checking
        inline float ParseFloat(const std::string& str, float defaultVal = 0.0f, 
                              float minVal = -FLT_MAX, float maxVal = FLT_MAX)
        {
            try
            {
                float val = std::stof(Trim(str));
                if (val < minVal) return minVal;
                if (val > maxVal) return maxVal;
                return val;
            }
            catch (...)
            {
                return defaultVal;
            }
        }

        // Parse int with bounds checking
        inline int ParseInt(const std::string& str, int defaultVal = 0, 
                          int minVal = INT_MIN, int maxVal = INT_MAX)
        {
            try
            {
                int val = std::stoi(Trim(str));
                if (val < minVal) return minVal;
                if (val > maxVal) return maxVal;
                return val;
            }
            catch (...)
            {
                return defaultVal;
            }
        }

        // Parse RGBA (0-255, 0-255, 0-255, 0-100)
        inline void ParseRGBA(const std::string& str, float out[4])
        {
            std::vector<int> values;
            std::string current;
            
            for (char c : str + ",")
            {
                if (c == ',')
                {
                    if (!current.empty())
                    {
                        values.push_back(ParseInt(current, 0, 0, 255));
                        current.clear();
                    }
                }
                else if (!std::isspace(c))
                {
                    current += c;
                }
            }

            // Fill output (normalized to 0-1)
            out[0] = values.size() > 0 ? values[0] / 255.0f : 0.0f; // R
            out[1] = values.size() > 1 ? values[1] / 255.0f : 0.0f; // G
            out[2] = values.size() > 2 ? values[2] / 255.0f : 0.0f; // B
            out[3] = values.size() > 3 ? values[3] / 100.0f : 1.0f; // A (0-100 -> 0-1)
        }

        // Parse normalized bounds (X, Y, W, H)
        inline void ParseBounds(const std::string& str, float& x, float& y, float& w, float& h)
        {
            std::vector<float> values;
            std::string current;
            
            for (char c : str + ",")
            {
                if (c == ',')
                {
                    if (!current.empty())
                    {
                        values.push_back(ParseFloat(current, 0.0f, 0.0f, 1.0f));
                        current.clear();
                    }
                }
                else if (!std::isspace(c))
                {
                    current += c;
                }
            }

            x = values.size() > 0 ? values[0] : 0.0f;
            y = values.size() > 1 ? values[1] : 0.0f;
            w = values.size() > 2 ? values[2] : 1.0f;
            h = values.size() > 3 ? values[3] : 1.0f;
        }

        // Parse enum by name
        template<typename EnumType>
        inline EnumType ParseEnum(const std::string& str, 
                                 const std::unordered_map<std::string, EnumType>& mapping,
                                 EnumType defaultVal)
        {
            std::string s = Trim(str);
            if (s.empty()) return defaultVal;
            
            // Try case-sensitive first
            auto it = mapping.find(s);
            if (it != mapping.end())
                return it->second;
            
            // Try case-insensitive
            std::string lower = s;
            for (char& c : lower) c = std::tolower(c);
            
            for (const auto& [key, value] : mapping)
            {
                std::string keyLower = key;
                for (char& c : keyLower) c = std::tolower(c);
                if (keyLower == lower)
                    return value;
            }
            
            return defaultVal;
        }
    }

    /* =========================================================
       Main parsing API
       ========================================================= */

    class MediaFrameworkINIParser
    {
    public:
        // Parse with cascading INI files (child -> folder -> file)
        // childIniPath: e.g., "MediaLoadscreen.ini"
        // fileIniPath: e.g., "Videos/MyVideo.ini"
        static ParsedMediaDefinition ParseMediaDefinition(
            const std::string& childIniPath,
            const std::string& fileIniPath);

    private:
        static Schema BuildSchema(ParsedMediaDefinition& outResult);
        static IniFile LoadCascadingINI(const std::string& childPath, const std::string& filePath);
        static std::string GetFolderINIPath(const std::string& filePath);
    };

	const char* GetLastParseErrorInternal();

} // namespace MediaFramework::INI

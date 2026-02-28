// MediaFileResolver.cpp
#include "MediaFileResolver.h"

namespace MediaFramework::FileResolver
{
	// ===================================================================
	// Extension helpers
	// ===================================================================
	bool MediaFileResolver::IsVideoExtension(const std::string& ext)
	{
		static const std::vector<std::string> exts = { ".mp4", ".webm", ".mkv", ".mov", ".avi" };
		std::string l = ext;
		std::transform(l.begin(), l.end(), l.begin(), ::tolower);
		return std::find(exts.begin(), exts.end(), l) != exts.end();
	}

	bool MediaFileResolver::IsImageExtension(const std::string& ext)
	{
		static const std::vector<std::string> exts = { ".dds", ".png", ".jpg", ".jpeg", ".bmp", ".tiff"};
		std::string l = ext;
		std::transform(l.begin(), l.end(), l.begin(), ::tolower);
		return std::find(exts.begin(), exts.end(), l) != exts.end();
	}

	bool MediaFileResolver::IsAudioExtension(const std::string& ext)
	{
		static const std::vector<std::string> exts = { ".wav", ".mp3", ".ogg", ".flac", ".m4a" };
		std::string l = ext;
		std::transform(l.begin(), l.end(), l.begin(), ::tolower);
		return std::find(exts.begin(), exts.end(), l) != exts.end();
	}

	bool MediaFileResolver::IsIniExtension(const std::string& ext)
	{
		std::string l = ext;
		std::transform(l.begin(), l.end(), l.begin(), ::tolower);
		return l == ".ini";
	}

	void CopyStringSafe(char* dst, size_t dstSize, const std::string& src)
	{
		if (dstSize == 0)
			return;
		strncpy_s(dst, dstSize, src.c_str(), _TRUNCATE);
	}

	// ===================================================================
	// Single file
	// ===================================================================
	InternalMediaDescriptor MediaFileResolver::BuildFromSingleFile(const std::filesystem::path& file)
	{
		InternalMediaDescriptor desc;
		desc.primary = file;

		const auto stem = file.stem().string();
		const auto parent = file.parent_path();

		// Companion .ini
		if (std::filesystem::exists(parent / (stem + ".ini")))
			desc.ini = parent / (stem + ".ini");

		// Companion audio
		for (const char* ext : { ".wav", ".mp3", ".ogg", ".flac", ".m4a" }) {
			auto candidate = parent / (stem + ext);
			if (std::filesystem::exists(candidate)) {
				desc.audio = candidate;
				break;
			}
		}

		// Determine mediaType
		const auto ext = file.extension().string();
		if (IsVideoExtension(ext))
			desc.mediaComposition.visualType = VisualType::Video;
		else if (IsImageExtension(ext))
			desc.mediaComposition.visualType = VisualType::Image;

		if (IsAudioExtension(ext) || !desc.audio.empty())
			desc.mediaComposition.audioType = AudioType::Enabled;

		// Reject unsupported files
		if (desc.mediaComposition.visualType == VisualType::None &&
			desc.mediaComposition.audioType == AudioType::None)
			desc.primary.clear();

		return desc;
	}

	// ===================================================================
	// Directory grouping (with conflict resolution)
	// ===================================================================
	void MediaFileResolver::GroupDirectory(const std::filesystem::path& dir,
		std::vector<InternalMediaDescriptor>& out)
	{
		std::unordered_map<std::string, InternalMediaDescriptor> groups;

		for (const auto& entry : std::filesystem::directory_iterator(dir)) {
			if (!entry.is_regular_file())
				continue;

			const auto path = entry.path();
			const auto stem = path.stem().string();
			const auto ext = path.extension().string();

			auto& desc = groups[stem];

			if (IsVideoExtension(ext)) {
				if (!desc.primary.empty()) {
					// Conflict: existing primary
					if (IsImageExtension(desc.primary.extension().string())) {
						logger::warn("MediaFileResolver: stem '{}' has both video and image. Preferring video.", stem);
					}
				}
				desc.primary = path;
				desc.mediaComposition.visualType = VisualType::Video;
				if (!desc.audio.empty())
					desc.mediaComposition.audioType = AudioType::Enabled;
			} else if (IsImageExtension(ext)) {
				if (!desc.primary.empty() && IsVideoExtension(desc.primary.extension().string())) {
					// Video already won — ignore this image
					continue;
				}
				desc.primary = path;
				desc.mediaComposition.visualType = VisualType::Image;
				if (!desc.audio.empty())
					desc.mediaComposition.audioType = AudioType::Enabled;
			} else if (IsAudioExtension(ext)) {
				desc.audio = path;
				desc.mediaComposition.audioType = AudioType::Enabled;
			} else if (IsIniExtension(ext)) {
				desc.ini = path;
			}
		}

		for (auto& [stem, desc] : groups) {
			if (desc.primary.empty() && desc.audio.empty())
				continue;
			if (desc.mediaComposition.visualType == VisualType::None &&
				desc.mediaComposition.audioType == AudioType::None)
				continue;

			out.push_back(std::move(desc));
		}
	}

	// ===================================================================
	// Public Resolve
	// ===================================================================
	std::vector<InternalMediaDescriptor> MediaFileResolver::Resolve(const std::filesystem::path& path)
	{
		std::vector<InternalMediaDescriptor> result;

		if (!std::filesystem::exists(path))
			return result;

		if (!std::filesystem::is_directory(path)) {
			auto desc = BuildFromSingleFile(path);
			if (!desc.primary.empty())  // filters out Unknown
				result.push_back(std::move(desc));
			return result;
		}

		GroupDirectory(path, result);
		return result;
	}
}

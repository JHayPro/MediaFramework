// MediaFileResolver.h
#pragma once
#include "MediaFrameworkAPI.h"

namespace MediaFramework::FileResolver
{
	// Internal rich type (real STL, no ABI concerns)
	struct InternalMediaDescriptor
	{
		std::filesystem::path primary;
		std::filesystem::path audio;
		std::filesystem::path ini;
		MediaComposition mediaComposition{ VisualType::None, AudioType::None };
	};

	class MediaFileResolver
	{
	public:
		std::vector<InternalMediaDescriptor> Resolve(const std::filesystem::path& path);

	private:
		InternalMediaDescriptor BuildFromSingleFile(const std::filesystem::path& file);
		void GroupDirectory(const std::filesystem::path& dir,
			std::vector<InternalMediaDescriptor>& out);

		static bool IsVideoExtension(const std::string& ext);
		static bool IsImageExtension(const std::string& ext);
		static bool IsAudioExtension(const std::string& ext);
		static bool IsIniExtension(const std::string& ext);
	};

	// Free helper (accessible from API.cpp)
	void CopyStringSafe(char* dst, size_t dstSize, const std::string& src);
}

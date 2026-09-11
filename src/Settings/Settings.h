#pragma once

#include <spdlog/spdlog.h>

namespace Settings
{
	struct Config
	{
		spdlog::level::level_enum level{ spdlog::level::info };
		bool                      speak{ true };
		bool                      onlyWhenReplaced{ false };
		bool                      fuzRoDoh{ true };
		bool                      misses{ true };
		bool                      unchanged{ false };
	};

	[[nodiscard]] const Config& Get();
	void                        Load();
}

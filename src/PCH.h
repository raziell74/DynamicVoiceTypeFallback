#pragma once

#include "SKSE/Impl/PCH.h"
#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"

using namespace std::literals;

SKSE_EXPORT constinit SKSE::PluginVersionData SKSEPlugin_Version = []() noexcept {
	SKSE::PluginVersionData v;
	v.PluginName("DynamicVoiceTypeFallback");
	v.AuthorName("Raziell74"sv);
	v.PluginVersion({ 0, 1, 0, 0 });
	v.UsesAddressLibrary();
	v.UsesUpdatedStructs();
	v.CompatibleVersions({
		SKSE::RUNTIME_SSE_1_5_97,
		SKSE::RUNTIME_SSE_1_6_1170,
		SKSE::RUNTIME_SSE_1_7_99,
	});
	return v;
}();

SKSE_EXPORT bool SKSEPlugin_Query(SKSE::QueryInterface*, SKSE::PluginInfo* pluginInfo)
{
	pluginInfo->infoVersion = SKSE::PluginInfo::kVersion;
	pluginInfo->name = SKSEPlugin_Version.GetPluginName().data();
	pluginInfo->version = SKSEPlugin_Version.GetPluginVersion().pack();
	return true;
}

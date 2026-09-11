#include "PCH.h"

#include "Hooks/Hooks.h"

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	// Debug CRT: Module::_instance is not constinit. If any Relocation resolved
	// during static init, get() marks the singleton initialized and then the
	// constructor zeros _base — later ID lookups jump to a raw offset (crash).
	REL::Module::reset();
	SKSE::Init(a_skse);

	const auto* plugin = SKSE::PluginVersionData::GetSingleton();
	SKSE::log::info("{} v{} loaded", plugin->GetPluginName(), plugin->GetPluginVersion().string());

	return Hooks::Register();
}

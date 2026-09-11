#pragma once

namespace VoiceMap
{
	bool InstallLoadHook();
	void Publish();
	[[nodiscard]] RE::FormID OriginalVoiceTypeID(RE::FormID a_npcID);
}

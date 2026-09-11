#pragma once

namespace VoiceMap
{
	bool InstallLoadHook();
	void Publish();
	[[nodiscard]] RE::BSFixedString OriginalEditorID(RE::FormID a_npcID);
}

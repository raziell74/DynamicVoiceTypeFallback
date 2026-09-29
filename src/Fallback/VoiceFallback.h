#pragma once

namespace VoiceFallback
{
	void Apply(RE::DialogueItem* a_item, RE::TESObjectREFR* a_speaker);

	// Dialogue Menu lines are not DialogueItems. PopulateTopicInfo writes the voice
	// path into a fixed buffer; rewrite that buffer when the current VoiceType file is missing.
	void ApplyBuffer(
		char*                                 a_filePath,
		std::size_t                           a_capacity,
		RE::TESObjectREFR*                    a_speaker,
		RE::TESTopic*                         a_topic,
		RE::TESTopicInfo*                     a_info,
		const RE::TESTopicInfo::TESResponse*  a_response);
}

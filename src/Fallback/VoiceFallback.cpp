#include "PCH.h"

#include "Fallback/VoiceFallback.h"
#include "VoiceMap/VoiceMap.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	constexpr std::string_view kExtensions[]{ ".fuz", ".wav", ".xwm" };

	std::atomic<std::uint32_t> g_loggedPaths{ 0 };

	[[nodiscard]] bool IeEquals(std::string_view a_lhs, std::string_view a_rhs)
	{
		if (a_lhs.size() != a_rhs.size()) {
			return false;
		}
		return std::equal(a_lhs.begin(), a_lhs.end(), a_rhs.begin(), [](unsigned char a, unsigned char b) {
			return std::tolower(a) == std::tolower(b);
		});
	}

	[[nodiscard]] bool ResourceExists(const char* a_path)
	{
		RE::BSResourceNiBinaryStream stream(a_path);
		return stream.good();
	}

	[[nodiscard]] std::string StripDataPrefix(std::string a_path)
	{
		constexpr std::string_view prefixes[]{ "Data\\", "Data/", "data\\", "data/" };
		for (const auto prefix : prefixes) {
			if (a_path.size() >= prefix.size() && IeEquals(std::string_view(a_path).substr(0, prefix.size()), prefix)) {
				a_path.erase(0, prefix.size());
				break;
			}
		}
		return a_path;
	}

	[[nodiscard]] bool HasKnownExtension(std::string_view a_path)
	{
		const auto slash = a_path.find_last_of("\\/");
		const auto dot = a_path.find_last_of('.');
		if (dot == std::string_view::npos || (slash != std::string_view::npos && dot < slash)) {
			return false;
		}
		const auto ext = a_path.substr(dot);
		for (const auto known : kExtensions) {
			if (IeEquals(ext, known)) {
				return true;
			}
		}
		return false;
	}

	[[nodiscard]] bool VoiceAssetExists(std::string_view a_path)
	{
		if (a_path.empty()) {
			return false;
		}

		std::vector<std::string> candidates;
		candidates.emplace_back(a_path);
		const auto stripped = StripDataPrefix(std::string(a_path));
		if (stripped != a_path) {
			candidates.emplace_back(stripped);
		}

		std::vector<std::string> toCheck;
		for (const auto& base : candidates) {
			toCheck.push_back(base);
			if (!HasKnownExtension(base)) {
				for (const auto ext : kExtensions) {
					toCheck.emplace_back(base + std::string(ext));
				}
			}
		}

		for (const auto& candidate : toCheck) {
			if (ResourceExists(candidate.c_str())) {
				return true;
			}
		}
		return false;
	}

	[[nodiscard]] std::optional<std::string> ReplaceFolder(
		std::string_view a_path,
		std::string_view a_from,
		std::string_view a_to)
	{
		if (a_from.empty() || IeEquals(a_from, a_to)) {
			return std::nullopt;
		}

		std::string out;
		out.reserve(a_path.size() + a_to.size());
		bool replaced = false;
		std::size_t i = 0;
		while (i < a_path.size()) {
			const auto next = a_path.find_first_of("\\/", i);
			const auto len = next == std::string_view::npos ? a_path.size() - i : next - i;
			const auto component = a_path.substr(i, len);
			const bool last = next == std::string_view::npos;
			if (!replaced && !last && IeEquals(component, a_from)) {
				out.append(a_to);
				replaced = true;
			} else {
				out.append(component);
			}
			if (next == std::string_view::npos) {
				break;
			}
			out.push_back(a_path[next]);
			i = next + 1;
		}

		return replaced ? std::optional<std::string>{ std::move(out) } : std::nullopt;
	}

	[[nodiscard]] RE::TESNPC* SpeakerNPC(RE::TESObjectREFR* a_speaker)
	{
		if (!a_speaker || a_speaker->IsPlayerRef()) {
			return nullptr;
		}
		if (auto* actor = a_speaker->As<RE::Actor>()) {
			return actor->GetActorBase();
		}
		auto* base = a_speaker->GetBaseObject();
		return base ? base->As<RE::TESNPC>() : nullptr;
	}

	void LogSamplePath(std::string_view a_path)
	{
		const auto n = g_loggedPaths.fetch_add(1, std::memory_order_relaxed);
		if (n < 8) {
			SKSE::log::debug("DialogueResponse::voice[{}] = {}", n, a_path);
		}
	}
}

namespace VoiceFallback
{
	void Apply(RE::DialogueItem* a_item, RE::TESObjectREFR* a_speaker)
	{
		if (!a_item) {
			return;
		}

		for (RE::DialogueResponse* response : a_item->responses) {
			if (!response || response->voice.empty()) {
				continue;
			}
			LogSamplePath(response->voice.c_str());
		}

		auto* npc = SpeakerNPC(a_speaker);
		if (!npc) {
			return;
		}

		const auto original = VoiceMap::OriginalEditorID(npc->GetFormID());
		const char* originalEdid = original.c_str();
		if (!originalEdid || !*originalEdid) {
			return;
		}

		const auto* currentVt = npc->GetObjectVoiceType();
		const char* currentEdid = currentVt ? currentVt->GetFormEditorID() : nullptr;
		if (!currentEdid || !*currentEdid || IeEquals(currentEdid, originalEdid)) {
			return;
		}

		for (RE::DialogueResponse* response : a_item->responses) {
			if (!response || response->voiceSound) {
				continue;
			}

			const char* voice = response->voice.c_str();
			if (!voice || !*voice) {
				continue;
			}

			if (VoiceAssetExists(voice)) {
				continue;
			}

			auto swapped = ReplaceFolder(voice, currentEdid, originalEdid);
			if (!swapped) {
				continue;
			}
			if (!VoiceAssetExists(*swapped)) {
				continue;
			}

			SKSE::log::info(
				"Voice fallback {:08X} {} -> {} ({})",
				npc->GetFormID(),
				currentEdid,
				originalEdid,
				*swapped);
			response->voice = RE::BSFixedString(swapped->c_str());
		}
	}
}

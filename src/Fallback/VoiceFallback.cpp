#include "PCH.h"

#include "Fallback/VoiceFallback.h"
#include "Log/Format.h"
#include "Settings/Settings.h"
#include "VoiceMap/VoiceMap.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	constexpr std::string_view kExtensions[]{ ".fuz", ".wav", ".xwm" };

	std::atomic<bool> g_loggedFirstCtor{ false };

	[[nodiscard]] bool IeEquals(std::string_view a_lhs, std::string_view a_rhs)
	{
		if (a_lhs.size() != a_rhs.size()) {
			return false;
		}
		return std::equal(a_lhs.begin(), a_lhs.end(), a_rhs.begin(), [](unsigned char a, unsigned char b) {
			return std::tolower(a) == std::tolower(b);
		});
	}

	[[nodiscard]] bool PathHasFolder(std::string_view a_path, std::string_view a_folder)
	{
		if (a_folder.empty()) {
			return false;
		}
		std::size_t i = 0;
		while (i < a_path.size()) {
			const auto next = a_path.find_first_of("\\/", i);
			const auto len = next == std::string_view::npos ? a_path.size() - i : next - i;
			if (IeEquals(a_path.substr(i, len), a_folder)) {
				return true;
			}
			if (next == std::string_view::npos) {
				break;
			}
			i = next + 1;
		}
		return false;
	}

	[[nodiscard]] bool IsFuzStubPath(std::string_view a_path)
	{
		return PathHasFolder(a_path, "Fuz Ro Doh") || PathHasFolder(a_path, "Fuz Ro D-oh");
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

	[[nodiscard]] std::optional<std::string> FindVoiceAsset(std::string_view a_path)
	{
		if (a_path.empty() || IsFuzStubPath(a_path)) {
			return std::nullopt;
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
			if (HasKnownExtension(base)) {
				const auto dot = base.find_last_of('.');
				const auto stem = base.substr(0, dot);
				for (const auto ext : kExtensions) {
					std::string alt;
					alt.reserve(stem.size() + ext.size());
					alt.append(stem);
					alt.append(ext);
					if (!IeEquals(alt, base)) {
						toCheck.push_back(std::move(alt));
					}
				}
			} else {
				for (const auto ext : kExtensions) {
					toCheck.emplace_back(base + std::string(ext));
				}
			}
		}

		for (const auto& candidate : toCheck) {
			if (ResourceExists(candidate.c_str())) {
				return candidate;
			}
		}
		return std::nullopt;
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

	[[nodiscard]] std::string TruncEdid(const char* a_edid, std::size_t a_max)
	{
		if (!a_edid || !*a_edid) {
			return {};
		}
		std::string out(a_edid);
		if (out.size() > a_max) {
			out.resize(a_max);
		}
		return out;
	}

	[[nodiscard]] RE::FormID VoiceFilenameFormID(const RE::TESForm* a_form)
	{
		if (!a_form) {
			return 0;
		}
		const auto id = a_form->GetFormID();
		const auto* file = a_form->GetFile();
		if (file && file->IsLight()) {
			return id & 0xFFFu;
		}
		return id & 0x00FFFFFFu;
	}

	[[nodiscard]] std::string BuildVoicePath(
		std::string_view a_plugin,
		std::string_view a_voiceEdid,
		std::string_view a_questEdid,
		std::string_view a_topicEdid,
		RE::FormID       a_infoLocal,
		int              a_responseIndex)
	{
		char buf[512];
		std::snprintf(
			buf,
			sizeof(buf),
			"Data\\Sound\\Voice\\%.*s\\%.*s\\%.*s_%.*s_%08X_%d.wav",
			static_cast<int>(a_plugin.size()),
			a_plugin.data(),
			static_cast<int>(a_voiceEdid.size()),
			a_voiceEdid.data(),
			static_cast<int>(a_questEdid.size()),
			a_questEdid.data(),
			static_cast<int>(a_topicEdid.size()),
			a_topicEdid.data(),
			a_infoLocal,
			a_responseIndex);
		return buf;
	}

	[[nodiscard]] const char* Nz(const char* a_s)
	{
		return (a_s && a_s[0]) ? a_s : "-";
	}

	[[nodiscard]] const char* FormEdid(const RE::TESForm* a_form)
	{
		return a_form ? Nz(a_form->GetFormEditorID()) : "-";
	}

	[[nodiscard]] const char* FormName(const RE::TESForm* a_form)
	{
		return a_form ? Nz(a_form->GetName()) : "-";
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

	// GetObjectVoiceType / VTCK can hold a packed FormID or a non-VoiceType form.
	// Never call GetFormID / GetFormEditorID until this has produced a live BGSVoiceType.
	[[nodiscard]] const RE::BGSVoiceType* LiveVoiceType(const RE::TESForm* a_form)
	{
		const auto raw = reinterpret_cast<std::uintptr_t>(a_form);
		if (raw == 0) {
			return nullptr;
		}
		if (raw <= 0xFFFFFFFFu) {
			return RE::TESForm::LookupByID<RE::BGSVoiceType>(static_cast<RE::FormID>(raw));
		}
		return a_form->As<RE::BGSVoiceType>();
	}

	[[nodiscard]] bool ShouldLogSpeak(std::string_view a_action, bool a_fuz)
	{
		const auto& cfg = Settings::Get();
		if (!cfg.speak) {
			return false;
		}

		const bool replaced = a_action == "fallback" || a_action == "fallback_recon";
		const bool fuz = a_fuz || a_action == "fallback_recon" || a_action.starts_with("skip_recon_");
		const bool unchanged = a_action == "skip_same_voicetype" || a_action == "skip_current_exists";

		if (cfg.onlyWhenReplaced) {
			return replaced || (cfg.fuzRoDoh && fuz);
		}
		if (replaced) {
			return true;
		}
		if (fuz && cfg.fuzRoDoh) {
			return true;
		}
		if (unchanged) {
			return cfg.unchanged;
		}
		return cfg.misses;
	}

	void LogSpeak(
		std::string_view                 a_action,
		RE::TESNPC*                      a_npc,
		RE::TESObjectREFR*               a_speaker,
		RE::DialogueItem*                a_item,
		RE::FormID                       a_currentID,
		const RE::BGSVoiceType*          a_currentVt,
		RE::FormID                       a_originalID,
		const RE::BGSVoiceType*          a_originalVt,
		const char*                      a_voice,
		const std::optional<std::string>& a_foundCurrent,
		std::string_view                 a_swapped,
		const std::optional<std::string>& a_foundFallback,
		std::string_view                 a_preFuz = {})
	{
		const bool fuz = !a_preFuz.empty() || IsFuzStubPath(Nz(a_voice));
		if (!ShouldLogSpeak(a_action, fuz)) {
			return;
		}

		SKSE::log::debug(
			"{}",
			Log::Block(fmt::format("Speak  {}", a_action))
				.Hex("npc", a_npc->GetFormID(), FormName(a_npc))
				.Hex("ref", a_speaker->GetFormID())
				.Hex("topic", a_item->topic ? a_item->topic->GetFormID() : 0, FormEdid(a_item->topic))
				.Hex("info", a_item->info ? a_item->info->GetFormID() : 0)
				.Hex("current", a_currentID, FormEdid(a_currentVt))
				.Hex("original", a_originalID, FormEdid(a_originalVt))
				.Field("path", Nz(a_voice))
				.Field("exists", a_foundCurrent ? a_foundCurrent->c_str() : "no")
				.FieldIf(Settings::Get().fuzRoDoh && !a_preFuz.empty(), "preFuz", a_preFuz)
				.Field("swapped", a_swapped.empty() ? "-" : a_swapped)
				.Field("fallbackExists", a_foundFallback ? a_foundFallback->c_str() : "no")
				.Str());
	}
}

namespace VoiceFallback
{
	void Apply(RE::DialogueItem* a_item, RE::TESObjectREFR* a_speaker)
	{
		if (!g_loggedFirstCtor.exchange(true, std::memory_order_relaxed)) {
			SKSE::log::debug(
				"{}",
				Log::Block("First DialogueItem::Ctor after hook")
					.Field("item", static_cast<const void*>(a_item))
					.Field("speaker", static_cast<const void*>(a_speaker))
					.Str());
		}

		if (!a_item) {
			SKSE::log::warn("DialogueItem::Ctor returned null");
			return;
		}

		if (!a_speaker) {
			if (ShouldLogSpeak("skip_null_speaker", false)) {
				SKSE::log::debug(
					"{}",
					Log::Block("Speak  skip_null_speaker")
						.Hex("topic", a_item->topic ? a_item->topic->GetFormID() : 0)
						.Hex("info", a_item->info ? a_item->info->GetFormID() : 0)
						.Str());
			}
			return;
		}

		if (a_speaker->IsPlayerRef()) {
			return;
		}

		auto* npc = SpeakerNPC(a_speaker);
		if (!npc) {
			if (ShouldLogSpeak("skip_no_npc", false)) {
				SKSE::log::debug(
					"{}",
					Log::Block("Speak  skip_no_npc")
						.Hex("ref", a_speaker->GetFormID())
						.Hex("topic", a_item->topic ? a_item->topic->GetFormID() : 0)
						.Str());
			}
			return;
		}

		const auto npcID = npc->GetFormID();
		const auto originalID = VoiceMap::OriginalVoiceTypeID(npcID);
		const auto* currentVt = LiveVoiceType(npc->voiceType);
		if (!currentVt) {
			currentVt = LiveVoiceType(npc->GetObjectVoiceType());
		}
		const auto* originalVt = originalID ? RE::TESForm::LookupByID<RE::BGSVoiceType>(originalID) : nullptr;
		const auto currentID = currentVt ? currentVt->GetFormID() : 0;

		bool anyResponse = false;
		int responseIndex = 0;
		for (RE::DialogueResponse* response : a_item->responses) {
			if (!response) {
				continue;
			}
			anyResponse = true;
			++responseIndex;

			const char* voice = response->voice.c_str();
			const auto foundCurrent = (voice && *voice) ? FindVoiceAsset(voice) : std::nullopt;

			const char* action = "none";
			std::string swappedPath;
			std::string preFuz;
			std::optional<std::string> foundFallback;

			if (!originalID) {
				action = "skip_no_original";
			} else if (!currentVt) {
				action = "skip_no_current";
			} else if (currentID == originalID) {
				action = "skip_same_voicetype";
			} else if (response->voiceSound) {
				action = "skip_sndd";
			} else if (!voice || !*voice) {
				action = "skip_empty_path";
			} else if (foundCurrent) {
				action = "skip_current_exists";
			} else {
				const char* currentEdid = currentVt->GetFormEditorID();
				const char* originalEdid = originalVt ? originalVt->GetFormEditorID() : nullptr;
				if (!currentEdid || !*currentEdid || !originalEdid || !*originalEdid) {
					action = "skip_missing_edid";
				} else if (auto swapped = ReplaceFolder(voice, currentEdid, originalEdid)) {
					swappedPath = std::move(*swapped);
					foundFallback = FindVoiceAsset(swappedPath);
					if (foundFallback) {
						response->voice = RE::BSFixedString(foundFallback->c_str());
						action = "fallback";
					} else {
						action = "skip_fallback_missing";
					}
				} else {
					action = "skip_no_folder_match";
					if (IsFuzStubPath(voice)) {
						auto* info = a_item->info;
						auto* voiceInfo = (info && info->dataInfo) ? info->dataInfo : info;
						auto* topic = voiceInfo && voiceInfo->parentTopic ? voiceInfo->parentTopic : a_item->topic;
						auto* quest = a_item->quest;
						if (!quest && topic) {
							quest = topic->ownerQuest;
						}
						const auto* file = voiceInfo ? voiceInfo->GetFile() : nullptr;
						const std::string plugin = file ? std::string(file->GetFilename()) : std::string{};
						const auto questEdid = TruncEdid(quest ? quest->GetFormEditorID() : nullptr, 10);
						const auto topicEdid = TruncEdid(topic ? topic->GetFormEditorID() : nullptr, 15);
						const auto infoLocal = VoiceFilenameFormID(voiceInfo);
						preFuz = (!plugin.empty() && currentEdid && *currentEdid)
							? BuildVoicePath(plugin, currentEdid, questEdid, topicEdid, infoLocal, responseIndex)
							: std::string{};
						if (!plugin.empty() && originalEdid && *originalEdid) {
							swappedPath = BuildVoicePath(
								plugin, originalEdid, questEdid, topicEdid, infoLocal, responseIndex);
							if (auto foundRecon = FindVoiceAsset(swappedPath)) {
								foundFallback = std::move(foundRecon);
								response->voice = RE::BSFixedString(foundFallback->c_str());
								action = "fallback_recon";
							} else {
								action = "skip_recon_missing";
							}
						} else {
							action = "skip_recon_incomplete";
						}
					}
				}
			}

			LogSpeak(
				action,
				npc,
				a_speaker,
				a_item,
				currentID,
				currentVt,
				originalID,
				originalVt,
				voice,
				foundCurrent,
				swappedPath,
				foundFallback,
				preFuz);
		}

		if (!anyResponse) {
			LogSpeak(
				"skip_no_responses",
				npc,
				a_speaker,
				a_item,
				currentID,
				currentVt,
				originalID,
				originalVt,
				nullptr,
				std::nullopt,
				{},
				std::nullopt);
		}
	}
}

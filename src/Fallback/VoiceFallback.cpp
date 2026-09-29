#include "PCH.h"

#include "Fallback/VoiceFallback.h"
#include "Log/Format.h"
#include "Settings/Settings.h"
#include "VoiceMap/VoiceMap.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstring>
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
		std::string_view                  a_action,
		RE::TESNPC*                       a_npc,
		RE::TESObjectREFR*                a_speaker,
		RE::TESTopic*                     a_topic,
		RE::TESTopicInfo*                 a_info,
		RE::FormID                        a_currentID,
		const RE::BGSVoiceType*           a_currentVt,
		RE::FormID                        a_originalID,
		const RE::BGSVoiceType*           a_originalVt,
		const char*                       a_voice,
		const std::optional<std::string>& a_foundCurrent,
		std::string_view                  a_swapped,
		const std::optional<std::string>& a_foundFallback,
		std::string_view                  a_via,
		std::string_view                  a_preFuz = {})
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
				.Hex("topic", a_topic ? a_topic->GetFormID() : 0, FormEdid(a_topic))
				.Hex("info", a_info ? a_info->GetFormID() : 0)
				.Field("via", a_via)
				.Hex("current", a_currentID, FormEdid(a_currentVt))
				.Hex("original", a_originalID, FormEdid(a_originalVt))
				.Field("path", Nz(a_voice))
				.Field("exists", a_foundCurrent ? a_foundCurrent->c_str() : "no")
				.FieldIf(Settings::Get().fuzRoDoh && !a_preFuz.empty(), "preFuz", a_preFuz)
				.Field("swapped", a_swapped.empty() ? "-" : a_swapped)
				.Field("fallbackExists", a_foundFallback ? a_foundFallback->c_str() : "no")
				.Str());
	}

	struct SpeakerVoice
	{
		RE::TESNPC*             npc{ nullptr };
		const RE::BGSVoiceType* current{ nullptr };
		const RE::BGSVoiceType* original{ nullptr };
		RE::FormID              currentID{ 0 };
		RE::FormID              originalID{ 0 };
	};

	enum class BindResult
	{
		ok,
		nullSpeaker,
		player,
		noNpc
	};

	[[nodiscard]] BindResult BindSpeaker(
		RE::TESObjectREFR* a_speaker,
		RE::TESTopic*      a_topic,
		RE::TESTopicInfo*  a_info,
		std::string_view   a_via,
		SpeakerVoice&      a_out)
	{
		if (!a_speaker) {
			if (ShouldLogSpeak("skip_null_speaker", false)) {
				SKSE::log::debug(
					"{}",
					Log::Block("Speak  skip_null_speaker")
						.Hex("topic", a_topic ? a_topic->GetFormID() : 0)
						.Hex("info", a_info ? a_info->GetFormID() : 0)
						.Field("via", a_via)
						.Str());
			}
			return BindResult::nullSpeaker;
		}

		if (a_speaker->IsPlayerRef()) {
			return BindResult::player;
		}

		a_out.npc = SpeakerNPC(a_speaker);
		if (!a_out.npc) {
			if (ShouldLogSpeak("skip_no_npc", false)) {
				SKSE::log::debug(
					"{}",
					Log::Block("Speak  skip_no_npc")
						.Hex("ref", a_speaker->GetFormID())
						.Hex("topic", a_topic ? a_topic->GetFormID() : 0)
						.Field("via", a_via)
						.Str());
			}
			return BindResult::noNpc;
		}

		a_out.originalID = VoiceMap::OriginalVoiceTypeID(a_out.npc->GetFormID());
		a_out.current = LiveVoiceType(a_out.npc->voiceType);
		if (!a_out.current) {
			a_out.current = LiveVoiceType(a_out.npc->GetObjectVoiceType());
		}
		a_out.original = a_out.originalID ? RE::TESForm::LookupByID<RE::BGSVoiceType>(a_out.originalID) : nullptr;
		a_out.currentID = a_out.current ? a_out.current->GetFormID() : 0;
		return BindResult::ok;
	}

	// Path that should replace a_voice, when the current VoiceType file is missing.
	[[nodiscard]] std::optional<std::string> ConsiderResponse(
		const SpeakerVoice& a_voice,
		RE::TESObjectREFR*  a_speaker,
		RE::TESTopic*       a_topic,
		RE::TESTopicInfo*   a_info,
		RE::TESQuest*       a_quest,
		const char*         a_voicePath,
		bool                a_hasVoiceSound,
		int                 a_responseIndex,
		std::string_view    a_via)
	{
		const auto foundCurrent = (a_voicePath && *a_voicePath) ? FindVoiceAsset(a_voicePath) : std::nullopt;

		const char* action = "none";
		std::string swappedPath;
		std::string preFuz;
		std::optional<std::string> foundFallback;

		if (!a_voice.originalID) {
			action = "skip_no_original";
		} else if (!a_voice.current) {
			action = "skip_no_current";
		} else if (a_voice.currentID == a_voice.originalID) {
			action = "skip_same_voicetype";
		} else if (a_hasVoiceSound) {
			action = "skip_sndd";
		} else if (!a_voicePath || !*a_voicePath) {
			action = "skip_empty_path";
		} else if (foundCurrent) {
			action = "skip_current_exists";
		} else {
			const char* currentEdid = a_voice.current->GetFormEditorID();
			const char* originalEdid = a_voice.original ? a_voice.original->GetFormEditorID() : nullptr;
			if (!currentEdid || !*currentEdid || !originalEdid || !*originalEdid) {
				action = "skip_missing_edid";
			} else if (auto swapped = ReplaceFolder(a_voicePath, currentEdid, originalEdid)) {
				swappedPath = std::move(*swapped);
				foundFallback = FindVoiceAsset(swappedPath);
				if (foundFallback) {
					action = "fallback";
				} else {
					action = "skip_fallback_missing";
				}
			} else {
				action = "skip_no_folder_match";
				if (IsFuzStubPath(a_voicePath)) {
					auto* voiceInfo = (a_info && a_info->dataInfo) ? a_info->dataInfo : a_info;
					auto* topic = voiceInfo && voiceInfo->parentTopic ? voiceInfo->parentTopic : a_topic;
					auto* quest = a_quest;
					if (!quest && topic) {
						quest = topic->ownerQuest;
					}
					const auto* file = voiceInfo ? voiceInfo->GetFile() : nullptr;
					const std::string plugin = file ? std::string(file->GetFilename()) : std::string{};
					const auto questEdid = TruncEdid(quest ? quest->GetFormEditorID() : nullptr, 10);
					const auto topicEdid = TruncEdid(topic ? topic->GetFormEditorID() : nullptr, 15);
					const auto infoLocal = VoiceFilenameFormID(voiceInfo);
					preFuz = (!plugin.empty() && currentEdid && *currentEdid)
						? BuildVoicePath(plugin, currentEdid, questEdid, topicEdid, infoLocal, a_responseIndex)
						: std::string{};
					if (!plugin.empty() && originalEdid && *originalEdid) {
						swappedPath = BuildVoicePath(
							plugin, originalEdid, questEdid, topicEdid, infoLocal, a_responseIndex);
						if (auto foundRecon = FindVoiceAsset(swappedPath)) {
							foundFallback = std::move(foundRecon);
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
			a_voice.npc,
			a_speaker,
			a_topic,
			a_info,
			a_voice.currentID,
			a_voice.current,
			a_voice.originalID,
			a_voice.original,
			a_voicePath,
			foundCurrent,
			swappedPath,
			foundFallback,
			a_via,
			preFuz);

		const std::string_view decision{ action };
		if ((decision == "fallback" || decision == "fallback_recon") && foundFallback) {
			return foundFallback;
		}
		return std::nullopt;
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

		SpeakerVoice voice;
		if (BindSpeaker(a_speaker, a_item->topic, a_item->info, "dialogue", voice) != BindResult::ok) {
			return;
		}

		bool anyResponse = false;
		int responseIndex = 0;
		for (RE::DialogueResponse* response : a_item->responses) {
			if (!response) {
				continue;
			}
			anyResponse = true;
			++responseIndex;

			if (auto replaced = ConsiderResponse(
					voice,
					a_speaker,
					a_item->topic,
					a_item->info,
					a_item->quest,
					response->voice.c_str(),
					response->voiceSound != nullptr,
					responseIndex,
					"dialogue")) {
				const std::string previous{ response->voice.c_str() ? response->voice.c_str() : "" };
				response->voice = RE::BSFixedString(replaced->c_str());
				if (ShouldLogSpeak("fallback", false)) {
					SKSE::log::debug(
						"{}",
						Log::Block("Speak  dialogue_write")
							.Hex("ref", a_speaker->GetFormID())
							.Hex("topic", a_item->topic ? a_item->topic->GetFormID() : 0, FormEdid(a_item->topic))
							.Hex("info", a_item->info ? a_item->info->GetFormID() : 0)
							.Field("response", "{}", responseIndex)
							.Field("from", previous)
							.Field("to", *replaced)
							.Str());
				}
			}
		}

		if (!anyResponse) {
			LogSpeak(
				"skip_no_responses",
				voice.npc,
				a_speaker,
				a_item->topic,
				a_item->info,
				voice.currentID,
				voice.current,
				voice.originalID,
				voice.original,
				nullptr,
				std::nullopt,
				{},
				std::nullopt,
				"dialogue");
		}
	}

	void ApplyBuffer(
		char*                                a_filePath,
		std::size_t                          a_capacity,
		RE::TESObjectREFR*                   a_speaker,
		RE::TESTopic*                        a_topic,
		RE::TESTopicInfo*                    a_info,
		const RE::TESTopicInfo::TESResponse* a_response)
	{
		if (!a_filePath || a_capacity == 0) {
			if (ShouldLogSpeak("skip_empty_path", false)) {
				SKSE::log::debug(
					"{}",
					Log::Block("Speak  skip_empty_path")
						.Hex("topic", a_topic ? a_topic->GetFormID() : 0, FormEdid(a_topic))
						.Hex("info", a_info ? a_info->GetFormID() : 0)
						.Field("via", "menu")
						.Str());
			}
			return;
		}

		SpeakerVoice voice;
		if (BindSpeaker(a_speaker, a_topic, a_info, "menu", voice) != BindResult::ok) {
			return;
		}

		const int responseIndex = a_response ? static_cast<int>(a_response->responseNumber) : 0;
		const bool hasSound = a_response && a_response->sound != nullptr;
		const std::string previous{ a_filePath };
		auto replaced = ConsiderResponse(
			voice,
			a_speaker,
			a_topic,
			a_info,
			nullptr,
			a_filePath,
			hasSound,
			responseIndex,
			"menu");
		if (!replaced) {
			return;
		}
		if (replaced->size() >= a_capacity) {
			SKSE::log::warn(
				"{}",
				Log::Block("Speak  skip_menu_path_too_long")
					.Hex("topic", a_topic ? a_topic->GetFormID() : 0, FormEdid(a_topic))
					.Hex("info", a_info ? a_info->GetFormID() : 0)
					.Field("from", previous)
					.Field("to", *replaced)
					.Field("capacity", "{}", a_capacity)
					.Str());
			return;
		}
		std::memcpy(a_filePath, replaced->c_str(), replaced->size() + 1);
		if (ShouldLogSpeak("fallback", false)) {
			SKSE::log::debug(
				"{}",
				Log::Block("Speak  menu_write")
					.Hex("ref", a_speaker->GetFormID())
					.Hex("topic", a_topic ? a_topic->GetFormID() : 0, FormEdid(a_topic))
					.Hex("info", a_info ? a_info->GetFormID() : 0)
					.Field("response", "{}", responseIndex)
					.Field("from", previous)
					.Field("to", *replaced)
					.Str());
		}
	}
}

#include "PCH.h"

#include "Log/Format.h"
#include "VoiceMap/VoiceMap.h"

#include <atomic>
#include <memory>
#include <unordered_map>

namespace
{
	struct Pending
	{
		RE::FormID voiceTypeID{ 0 };
		bool       female{ false };
	};

	using PublishedMap = std::unordered_map<RE::FormID, RE::FormID>;

	std::unordered_map<RE::FormID, Pending>        g_pending;
	std::atomic<std::shared_ptr<const PublishedMap>> g_published;

	[[nodiscard]] RE::FormID UnresolvedOrLiveFormID(const RE::TESForm* a_form)
	{
		const auto raw = reinterpret_cast<std::uintptr_t>(a_form);
		if (raw == 0) {
			return 0;
		}
		// TESNPC::Load writes VTCK as a packed FormID in the pointer slot until InitItem.
		if (raw <= 0xFFFFFFFFu) {
			return static_cast<RE::FormID>(raw);
		}
		return a_form->GetFormID();
	}

	struct TESNPCLoadHook
	{
		static bool thunk(RE::TESNPC* a_this, RE::TESFile* a_file)
		{
			const auto id = a_this->GetFormID();
			const bool first = !g_pending.contains(id);
			const bool ok = func(a_this, a_file);
			if (first && ok) {
				g_pending[id] = Pending{
					.voiceTypeID = UnresolvedOrLiveFormID(a_this->voiceType),
					.female = a_this->IsFemale()
				};
			}
			return ok;
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	[[nodiscard]] RE::BGSVoiceType* ResolveOriginalVoice(RE::TESNPC* a_npc, const Pending& a_pending)
	{
		if (a_pending.voiceTypeID) {
			if (auto* vt = RE::TESForm::LookupByID<RE::BGSVoiceType>(a_pending.voiceTypeID)) {
				return vt;
			}
		}

		RE::TESRace* race = a_npc->originalRace ? a_npc->originalRace : a_npc->GetRace();
		if (!race) {
			return nullptr;
		}

		const RE::SEX sex = a_pending.female ? RE::SEX::kFemale : RE::SEX::kMale;
		return race->defaultVoiceTypes[sex];
	}
}

namespace VoiceMap
{
	bool InstallLoadHook()
	{
		REL::Relocation<std::uintptr_t> vtbl{ RE::TESNPC::VTABLE[0] };
		TESNPCLoadHook::func = vtbl.write_vfunc(0x06, TESNPCLoadHook::thunk);
		if (!TESNPCLoadHook::func.address()) {
			SKSE::log::error("TESNPC::Load vtable slot 0x06 was empty; original VoiceType map will not be built");
			return false;
		}
		SKSE::log::info("TESNPC::Load hook installed at vtable {:X}", vtbl.address());
		return true;
	}

	void Publish()
	{
		auto* dh = RE::TESDataHandler::GetSingleton();
		if (!dh) {
			SKSE::log::error("TESDataHandler missing at kDataLoaded");
			return;
		}

		auto built = std::make_shared<PublishedMap>();
		built->reserve(g_pending.size());

		std::size_t skippedNull = 0;
		std::size_t skippedDeleted = 0;
		std::size_t skippedIgnored = 0;
		std::size_t skippedDynamic = 0;
		std::size_t skippedNoLoad = 0;
		std::size_t skippedNoVoice = 0;
		for (auto* npc : dh->GetFormArray<RE::TESNPC>()) {
			if (!npc) {
				++skippedNull;
				continue;
			}
			if (npc->IsDeleted()) {
				++skippedDeleted;
				continue;
			}
			if (npc->IsIgnored()) {
				++skippedIgnored;
				continue;
			}
			if (npc->IsDynamicForm()) {
				++skippedDynamic;
				continue;
			}

			const auto it = g_pending.find(npc->GetFormID());
			if (it == g_pending.end()) {
				++skippedNoLoad;
				continue;
			}

			const auto* vt = ResolveOriginalVoice(npc, it->second);
			if (!vt || !vt->GetFormID()) {
				++skippedNoVoice;
				continue;
			}

			(*built)[npc->GetFormID()] = vt->GetFormID();
		}

		const auto count = built->size();
		g_published.store(std::move(built), std::memory_order_release);
		g_pending.clear();
		g_pending.rehash(0);

		SKSE::log::info(
			"{}",
			Log::Block("Published original VoiceType map")
				.Field("npcs", count)
				.Field("null", skippedNull)
				.Field("deleted", skippedDeleted)
				.Field("ignored", skippedIgnored)
				.Field("dynamic", skippedDynamic)
				.Field("noLoad", skippedNoLoad)
				.Field("noVoice", skippedNoVoice)
				.Str());
	}

	RE::FormID OriginalVoiceTypeID(RE::FormID a_npcID)
	{
		const auto map = g_published.load(std::memory_order_acquire);
		if (!map) {
			return 0;
		}
		const auto it = map->find(a_npcID);
		return it != map->end() ? it->second : 0;
	}
}

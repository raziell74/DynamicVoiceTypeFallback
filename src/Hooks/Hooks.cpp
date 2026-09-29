#include "PCH.h"

#include "Fallback/VoiceFallback.h"
#include "Hooks/Hooks.h"
#include "Log/Format.h"
#include "VoiceMap/VoiceMap.h"

#include <xbyak/xbyak.h>

namespace
{
	void OnSKSEMessage(SKSE::MessagingInterface::Message* a_msg)
	{
		if (!a_msg) {
			return;
		}
		if (a_msg->type == SKSE::MessagingInterface::kDataLoaded) {
			VoiceMap::Publish();
		}
	}

	[[nodiscard]] bool AlreadyHooked(const std::uint8_t* a_p)
	{
		return a_p[0] == 0xE8 || a_p[0] == 0xE9 || a_p[0] == 0xEB || a_p[0] == 0xCC ||
		       (a_p[0] == 0xFF && a_p[1] == 0x25);
	}

	[[nodiscard]] constexpr std::size_t ConsumePrologueInsn(const std::uint8_t* a_p)
	{
		const auto b0 = a_p[0];
		const auto b1 = a_p[1];
		const auto b2 = a_p[2];
		const auto b3 = a_p[3];

		if (b0 == 0xF3 && b1 == 0x0F && b2 == 0x1E && b3 == 0xFA) {
			return 4;
		}
		if (b0 >= 0x50 && b0 <= 0x57) {
			return 1;
		}
		if (b0 >= 0x40 && b0 <= 0x4F && b1 >= 0x50 && b1 <= 0x57) {
			return 2;
		}
		if (b0 == 0x48 && b1 == 0x83 && b2 == 0xEC) {
			return 4;
		}
		if (b0 == 0x48 && b1 == 0x81 && b2 == 0xEC) {
			return 7;
		}
		if ((b0 == 0x48 || b0 == 0x4C) && b1 == 0x89 && (b2 & 0xC7) == 0x44 && b3 == 0x24) {
			return 5;
		}
		if ((b0 == 0x48 || b0 == 0x4C) && b1 == 0x89 && (b2 & 0xC7) == 0x84 && b3 == 0x24) {
			return 8;
		}
		if (b0 == 0x44 && b1 == 0x89 && (b2 & 0xC7) == 0x44 && b3 == 0x24) {
			return 5;
		}
		// mov [reg+disp8], r64 (no SIB): 48 89 48 08 = mov [rax+8], rcx after mov rax, rsp
		if ((b0 == 0x48 || b0 == 0x4C) && b1 == 0x89 && (b2 & 0xC0) == 0x40 && (b2 & 0x07) != 0x04) {
			return 4;
		}
		if (b0 == 0x48 && b1 == 0x8B && b2 == 0xC4) {
			return 3;
		}
		if (b0 == 0x4C && b1 == 0x8B && b2 == 0xDC) {
			return 3;
		}
		if (b0 == 0x48 && b1 == 0x8B && b2 == 0xEC) {
			return 3;
		}
		return 0;
	}

	[[nodiscard]] constexpr std::size_t MeasureStolenBytes(const std::uint8_t* a_p, std::size_t a_min)
	{
		std::size_t stolen = 0;
		while (stolen < a_min) {
			const auto len = ConsumePrologueInsn(a_p + stolen);
			if (len == 0 || stolen + len > 32) {
				return 0;
			}
			stolen += len;
		}
		return stolen;
	}

	[[nodiscard]] std::size_t MeasureStolenBytes(std::uintptr_t a_src, std::size_t a_min)
	{
		return MeasureStolenBytes(reinterpret_cast<const std::uint8_t*>(a_src), a_min);
	}

	// AE 1.6.1170 / 1.7.99 DialogueItem::Ctor (SKSE log 2026-09-11)
	constexpr std::uint8_t kLoggedCtorPrologue[]{ 0x48, 0x8B, 0xC4, 0x48, 0x89, 0x48, 0x08, 0x55 };
	static_assert(ConsumePrologueInsn(kLoggedCtorPrologue) == 3);
	static_assert(ConsumePrologueInsn(kLoggedCtorPrologue + 3) == 4);
	static_assert(MeasureStolenBytes(kLoggedCtorPrologue, 5) == 7);

	struct StolenCave : Xbyak::CodeGenerator
	{
		StolenCave(const std::uint8_t* a_src, std::size_t a_len, std::uintptr_t a_retn)
		{
			for (std::size_t i = 0; i < a_len; ++i) {
				db(a_src[i]);
			}
			jmp(ptr[rip]);
			dq(a_retn);
		}
	};

	struct DialogueItemCtorHook
	{
		static RE::DialogueItem* thunk(
			RE::DialogueItem* a_this,
			RE::TESQuest* a_quest,
			RE::TESTopic* a_topic,
			RE::TESTopicInfo* a_topicInfo,
			RE::TESObjectREFR* a_speaker)
		{
			auto* item = func(a_this, a_quest, a_topic, a_topicInfo, a_speaker);
			VoiceFallback::Apply(item, a_speaker);
			return item;
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	bool InstallDialogueItemCtorHook()
	{
		REL::Relocation<std::uintptr_t> target{ RELOCATION_ID(34413, 35220) };
		const auto src = target.address();
		SKSE::log::info("DialogueItem::Ctor relocated to {:X}", src);
		if (!src) {
			SKSE::log::error("DialogueItem::Ctor relocated to 0");
			return false;
		}

		const auto* bytes = reinterpret_cast<const std::uint8_t*>(src);
		if (AlreadyHooked(bytes)) {
			SKSE::log::error(
				"{}",
				Log::Block("DialogueItem::Ctor entry is already patched; running unhooked")
					.Addr("address", src)
					.Field("bytes", "{:02X} {:02X}", bytes[0], bytes[1])
					.Str());
			return false;
		}

		constexpr std::size_t kPatch = 5;
		const auto stolen = MeasureStolenBytes(src, kPatch);
		if (stolen < kPatch) {
			SKSE::log::error(
				"{}",
				Log::Block("DialogueItem::Ctor prologue is not a recognized MSVC sequence")
					.Addr("address", src)
					.Field(
						"bytes",
						"{:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X}",
						bytes[0],
						bytes[1],
						bytes[2],
						bytes[3],
						bytes[4],
						bytes[5],
						bytes[6],
						bytes[7])
					.Str());
			return false;
		}

		StolenCave cave(bytes, stolen, src + stolen);
		cave.ready();

		auto& trampoline = SKSE::GetTrampoline();
		const auto original = reinterpret_cast<std::uintptr_t>(trampoline.allocate(cave));
		if (!original) {
			SKSE::log::error("Failed to allocate DialogueItem::Ctor trampoline");
			return false;
		}
		DialogueItemCtorHook::func = original;

		trampoline.write_branch<5>(src, DialogueItemCtorHook::thunk);
		if (stolen > kPatch) {
			REL::safe_fill(src + kPatch, REL::NOP, stolen - kPatch);
		}

		SKSE::log::info(
			"{}",
			Log::Block("DialogueItem::Ctor hook installed")
				.Addr("address", src)
				.Field("stolen", "{} bytes", stolen)
				.Addr("trampoline", original)
				.Str());
		return true;
	}

	// Dialogue Menu responses are built here, not by DialogueItem::Ctor.
	// SE 34429 / AE 35249. ConstructResponse is the call at +0xDE (SE and AE 1.6).
	thread_local RE::TESObjectREFR* g_menuSpeaker{ nullptr };

	struct MenuSpeakerGuard
	{
		RE::TESObjectREFR* previous;

		explicit MenuSpeakerGuard(RE::TESObjectREFR* a_speaker) :
			previous(g_menuSpeaker)
		{
			g_menuSpeaker = a_speaker;
		}

		~MenuSpeakerGuard() { g_menuSpeaker = previous; }
	};

	struct ConstructResponseHook
	{
		static bool thunk(
			RE::TESTopicInfo::TESResponse* a_response,
			char*                          a_filePath,
			RE::BGSVoiceType*              a_voiceType,
			RE::TESTopic*                  a_topic,
			RE::TESTopicInfo*              a_topicInfo)
		{
			const auto ok = func(a_response, a_filePath, a_voiceType, a_topic, a_topicInfo);
			if (ok && a_filePath) {
				constexpr std::size_t kPathCapacity = 0x104;
				VoiceFallback::ApplyBuffer(
					a_filePath,
					kPathCapacity,
					g_menuSpeaker,
					a_topic,
					a_topicInfo,
					a_response);
			}
			return ok;
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct PopulateTopicInfoHook
	{
		static std::int64_t thunk(
			std::int64_t                       a_unk,
			RE::TESTopic*                      a_topic,
			RE::TESTopicInfo*                  a_topicInfo,
			RE::Character*                     a_speaker,
			RE::TESTopicInfo::TESResponse*     a_response)
		{
			MenuSpeakerGuard guard(a_speaker);
			return func(a_unk, a_topic, a_topicInfo, a_speaker, a_response);
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	bool InstallPopulateTopicInfoHook()
	{
		REL::Relocation<std::uintptr_t> target{ RELOCATION_ID(34429, 35249) };
		const auto src = target.address();
		SKSE::log::info("PopulateTopicInfo relocated to {:X}", src);
		if (!src) {
			SKSE::log::error("PopulateTopicInfo relocated to 0");
			return false;
		}

		const auto* bytes = reinterpret_cast<const std::uint8_t*>(src);
		if (AlreadyHooked(bytes)) {
			SKSE::log::error(
				"{}",
				Log::Block("PopulateTopicInfo entry is already patched; Dialogue Menu fallback left off")
					.Addr("address", src)
					.Field("bytes", "{:02X} {:02X}", bytes[0], bytes[1])
					.Str());
			return false;
		}

		const auto callOffset = REL::Relocate(0xDE, 0xDE);
		const auto callSite = src + callOffset;
		const auto callByte = *reinterpret_cast<const std::uint8_t*>(callSite);
		if (callByte != 0xE8) {
			SKSE::log::error(
				"{}",
				Log::Block("PopulateTopicInfo ConstructResponse call site is not a call; Dialogue Menu fallback left off")
					.Addr("address", callSite)
					.Field("offset", "{:X}", callOffset)
					.Field("byte", "{:02X}", callByte)
					.Str());
			return false;
		}

		constexpr std::size_t kPatch = 5;
		const auto stolen = MeasureStolenBytes(src, kPatch);
		if (stolen < kPatch) {
			SKSE::log::error(
				"{}",
				Log::Block("PopulateTopicInfo prologue is not a recognized MSVC sequence")
					.Addr("address", src)
					.Field(
						"bytes",
						"{:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X}",
						bytes[0],
						bytes[1],
						bytes[2],
						bytes[3],
						bytes[4],
						bytes[5],
						bytes[6],
						bytes[7])
					.Str());
			return false;
		}

		StolenCave cave(bytes, stolen, src + stolen);
		cave.ready();

		auto& trampoline = SKSE::GetTrampoline();
		const auto original = reinterpret_cast<std::uintptr_t>(trampoline.allocate(cave));
		if (!original) {
			SKSE::log::error("Failed to allocate PopulateTopicInfo trampoline");
			return false;
		}
		PopulateTopicInfoHook::func = original;

		trampoline.write_branch<5>(src, PopulateTopicInfoHook::thunk);
		if (stolen > kPatch) {
			REL::safe_fill(src + kPatch, REL::NOP, stolen - kPatch);
		}

		ConstructResponseHook::func = trampoline.write_call<5>(callSite, ConstructResponseHook::thunk);

		SKSE::log::info(
			"{}",
			Log::Block("PopulateTopicInfo hook installed")
				.Addr("address", src)
				.Field("stolen", "{} bytes", stolen)
				.Addr("trampoline", original)
				.Addr("constructResponse", callSite)
				.Str());
		return true;
	}
}

namespace Hooks
{
	bool Register()
	{
		const auto messaging = SKSE::GetMessagingInterface();
		if (!messaging || !messaging->RegisterListener(OnSKSEMessage)) {
			SKSE::log::error("Failed to register SKSE messaging listener");
			return false;
		}

		if (REL::Module::IsVR()) {
			SKSE::log::warn("VR is not supported; VoiceType fallback hooks not installed");
			return true;
		}

		if (!VoiceMap::InstallLoadHook()) {
			SKSE::log::error("TESNPC::Load hook failed; original VoiceType map will not be built");
		}

		if (!InstallDialogueItemCtorHook()) {
			SKSE::log::error("DialogueItem::Ctor hook not installed; plugin will run unhooked");
		}

		if (!InstallPopulateTopicInfoHook()) {
			SKSE::log::error("PopulateTopicInfo hook not installed; Dialogue Menu voice fallback will not run");
		}

		return true;
	}
}

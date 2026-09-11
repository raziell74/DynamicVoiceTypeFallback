#include "PCH.h"

#include "Fallback/VoiceFallback.h"
#include "Hooks/Hooks.h"
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

	[[nodiscard]] std::size_t ConsumePrologueInsn(const std::uint8_t* a_p)
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

	[[nodiscard]] std::size_t MeasureStolenBytes(std::uintptr_t a_src, std::size_t a_min)
	{
		std::size_t stolen = 0;
		while (stolen < a_min) {
			const auto len = ConsumePrologueInsn(reinterpret_cast<const std::uint8_t*>(a_src + stolen));
			if (len == 0 || stolen + len > 32) {
				return 0;
			}
			stolen += len;
		}
		return stolen;
	}

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
				"DialogueItem::Ctor entry at {:X} is already patched ({:02X} {:02X}); running unhooked",
				src,
				bytes[0],
				bytes[1]);
			return false;
		}

		constexpr std::size_t kPatch = 5;
		const auto stolen = MeasureStolenBytes(src, kPatch);
		if (stolen < kPatch) {
			SKSE::log::error(
				"DialogueItem::Ctor prologue at {:X} is not a recognized MSVC sequence "
				"({:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X}); hook not installed",
				src,
				bytes[0],
				bytes[1],
				bytes[2],
				bytes[3],
				bytes[4],
				bytes[5],
				bytes[6],
				bytes[7]);
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
			"DialogueItem::Ctor hook installed at {:X} (stolen {} bytes, trampoline {:X})",
			src,
			stolen,
			original);
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

		return true;
	}
}

#pragma once
#include "ledger/Economy.hpp"

namespace lgp
{
	// Original price data for an item we re-priced while a barter menu is open.
	struct SavedPrice
	{
		enum class Kind : std::uint8_t { Value, Alchemy, Ingredient } kind{ Kind::Value };
		std::int32_t value{ 0 };
		std::uint32_t flags{ 0 };
	};

	struct State
	{
		std::recursive_mutex lock;
		ledger::Economy econ;
		std::vector<std::string> pendingNotices;
		std::unordered_map<RE::FormID, SavedPrice> saved;      // items currently re-priced
		std::unordered_map<RE::FormID, std::int32_t> baseGold;  // merchant chest -> normal gold (saved)
		std::unordered_map<RE::FormID, std::uint32_t> stockDay; // chest -> last day stock shifted
		std::unordered_map<std::uint32_t, std::uint32_t> lastBattle;  // hold -> day
		std::unordered_map<std::uint32_t, std::uint32_t> lastDragon;  // hold -> day
		bool barterOpen{ false };
		bool dialogueOpen{ false };
		bool applied{ false };  // prices/stock currently adjusted for a merchant
		ledger::Hold barterHold{ ledger::Hold::Unknown };
		bool menuOpen{ false };
	};

	State& S();

	namespace game
	{
		void ResolveForms();
		std::uint32_t Today();
		ledger::Hold HoldOf(RE::TESObjectREFR* ref);
		bool InSettlement(RE::TESObjectREFR* ref);
		std::optional<ledger::Good> Categorize(RE::TESBoundObject* obj);
		int PlayerGold();
		void TakeGold(int amount);
		void GiveGold(int amount);

		// Called when Dialogue/Barter menus open or close. Adjustments go in while
		// talking to a merchant (so the barter list is built with them) and come
		// out when both menus are closed.
		void OnMenu(std::string_view menu, bool opening);
		void RestorePrices();

		void OnCombat(RE::Actor* actor, RE::Actor* target);
		void OnDeath(RE::Actor* dying, RE::Actor* killer);
		void OnContainerChanged(const RE::TESContainerChangedEvent& e);

		void OnBloodfeud(SKSE::MessagingInterface::Message* msg);
		void Tick();
	}
}

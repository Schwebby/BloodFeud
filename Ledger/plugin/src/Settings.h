#pragma once
// Free of CommonLib so the INI parser can be tested anywhere.
#include "ledger/Economy.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace lgp
{
	struct FormRef
	{
		std::string plugin;
		std::uint32_t localId{ 0 };
	};

	struct Config
	{
		// [General]
		std::uint32_t menuKey = 66;  // F8
		bool notifications = true;

		// [Prices]
		bool scalePrices = true;
		float priceStrength = 1.0f;  // 0 = vanilla prices, 1 = full effect
		bool scaleMerchantGold = true;
		bool shiftStock = true;

		// [Events]
		bool civilWar = true;
		bool dragons = true;
		std::optional<FormRef> imperialFaction{ FormRef{ "Skyrim.esm", 0x01C9FC } };  // CWImperialFactionNPC
		std::optional<FormRef> stormcloakFaction{ FormRef{ "Skyrim.esm", 0x01C9FD } };  // CWSonsFactionNPC
		std::vector<std::string> outlawNames{ "Bandit", "Forsworn", "Highwayman", "Marauder", "Necromancer", "Vampire" };

		ledger::Settings core;

		void Load(const std::filesystem::path& path);
	};

	Config& Cfg();
}

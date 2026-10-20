#pragma once
// Deliberately free of CommonLib so the INI parser can be tested anywhere.
#include "bloodfeud/Types.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace bfp
{
	struct FormRef
	{
		std::string plugin;
		std::uint32_t localId{ 0 };
	};

	struct Config
	{
		// [General]
		std::uint32_t menuKey = 65;  // F7 (DirectInput scan code)
		bool showRumors = true;
		bool renameEnemies = true;
		bool buffGrunts = true;
		bool interrogation = true;

		// [Ambush]
		float spawnDistance = 1800.0f;
		float despawnDistance = 9000.0f;

		// [Detection]
		std::vector<std::string> banditNames{ "Bandit", "Highwayman", "Marauder", "Outlaw", "Thug" };
		std::vector<std::string> forswornNames{ "Forsworn", "Briarheart", "Ravager" };
		std::vector<std::string> silverHandNames{ "Silver Hand" };
		std::vector<std::string> thalmorNames{ "Thalmor", "Justiciar" };
		std::vector<std::string> vampireNames{ "Vampire" };
		std::vector<std::string> necromancerNames{ "Necromancer", "Conjurer", "Warlock" };
		std::vector<std::string> bossNames{ "Chief", "Boss", "Briarheart", "Master", "Leader" };

		// [Factions]
		std::optional<FormRef> banditFaction;
		std::optional<FormRef> forswornFaction;
		std::optional<FormRef> silverHandFaction;
		std::optional<FormRef> thalmorFaction;
		std::optional<FormRef> vampireFaction;
		std::optional<FormRef> necromancerFaction;

		// core tuning
		bf::Settings core;

		void Load(const std::filesystem::path& path);
	};

	Config& Cfg();
}

#pragma once
#include "bloodfeud/Rng.hpp"
#include "bloodfeud/Types.hpp"

#include <string>
#include <string_view>

namespace bf::text
{
	// Identity generation -------------------------------------------------
	void GenerateIdentity(Rng& rng, Npc& npc, GameGroup group);
	std::string RandomFirstName(Rng& rng, Culture c, bool female);
	std::string RandomSurname(Rng& rng, Culture c, bool female, std::string_view father);
	std::string RandomEpithet(Rng& rng);
	// An epithet earned from what happened to them ("the Burned", "Twice-Dead")
	std::string EarnedEpithet(Rng& rng, const Npc& npc);

	// Mapping helpers ---------------------------------------------------------
	Culture CultureFromRaceName(std::string_view raceName, GameGroup group);
	Hold HoldFromLocationName(std::string_view name);  // "Whiterun Hold", "The Rift"...
	std::string_view HoldName(Hold h);
	std::string_view FactionName(FactionId f);
	std::string_view RankName(Rank r, FactionId f);
	std::string_view TemperName(Temperament t);
	std::string_view CultureName(Culture c);
	std::string TraitList(std::uint32_t traits);
	std::string EdgeList(std::uint32_t edges, bool strengths);
	std::string InjuryList(std::uint32_t injuries);

	// Lines -------------------------------------------------------------------
	std::string Taunt(Rng& rng, const Npc& npc, const std::string& kinName);
	std::string YieldLine(Rng& rng, const Npc& npc);
	std::string LifeEvent(Rng& rng, Npc& npc, std::string& outEpithetChange);

	// Simple format: replaces {0}, {1}, ... with args.
	std::string Fmt(std::string_view pattern, std::initializer_list<std::string_view> args);
}

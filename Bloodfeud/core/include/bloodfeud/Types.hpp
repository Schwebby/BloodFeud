#pragma once
// Bloodfeud core types. This header has no game dependencies so the whole
// simulation can be compiled and unit-tested on any platform.

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace bf
{
	using NpcId = std::uint32_t;  // 0 == none
	inline constexpr NpcId kNoNpc = 0;

	enum class Culture : std::uint8_t
	{
		Nord,
		Imperial,
		Breton,
		Redguard,
		Reachman,
		Altmer,
		Bosmer,
		Dunmer,
		Khajiit,
		Argonian,
		Orc,
		Count
	};

	enum class Temperament : std::uint8_t
	{
		Hothead,    // quick to anger, never yields
		Coward,     // yields and talks, flees early
		Cold,       // calculating, holds grudges quietly
		Proud,      // humiliation is worse than death
		Cunning,    // lies under interrogation, sets traps
		Zealot,     // will not bargain, will not talk
		Honorable,  // keeps their word, can be reconciled
		Unhinged,   // unpredictable
		Count
	};

	// Personal quirks. Some are purely flavour, some change stats in-game.
	enum Trait : std::uint32_t
	{
		kTraitNone = 0,
		kDrunkard = 1u << 0,
		kSober = 1u << 1,
		kPerceptive = 1u << 2,
		kIntrovert = 1u << 3,
		kBraggart = 1u << 4,
		kDogLover = 1u << 5,
		kPious = 1u << 6,
		kGreedy = 1u << 7,
		kVengeful = 1u << 8,
		kLoyal = 1u << 9,
		kTraitorous = 1u << 10,
		kSadist = 1u << 11,
	};

	// Combat strengths/weaknesses gained over a career (Nemesis-style).
	enum Edge : std::uint32_t
	{
		kEdgeNone = 0,
		// strengths
		kIronHide = 1u << 0,      // +armor
		kFireproof = 1u << 1,     // +fire resist
		kFrostborn = 1u << 2,     // +frost resist
		kStormward = 1u << 3,     // +shock resist
		kSpellbreaker = 1u << 4,  // +magic resist
		kBerserker = 1u << 5,     // +damage
		kSwift = 1u << 6,         // +speed
		kDuelist = 1u << 7,       // +one-handed / block
		kMarksman = 1u << 8,      // +archery
		kHardy = 1u << 9,         // +health
		// weaknesses
		kFearsFire = 1u << 16,    // -fire resist
		kBrittle = 1u << 17,      // -frost resist
		kConductive = 1u << 18,   // -shock resist
		kGlassJaw = 1u << 19,     // -health
		kSlow = 1u << 20,         // -speed
		kWeakArm = 1u << 21,      // -damage
		kWeaknessMask = 0xFFFF0000u,
		kStrengthMask = 0x0000FFFFu,
	};

	// Lasting wounds from surviving the player.
	enum Injury : std::uint32_t
	{
		kInjuryNone = 0,
		kScarred = 1u << 0,      // flavour
		kBurnScars = 1u << 1,    // from fire damage: grows Fireproof or FearsFire
		kFrostbitten = 1u << 2,  // from frost
		kLimp = 1u << 3,         // -speed
		kOneEye = 1u << 4,       // -archery
		kBrokenJaw = 1u << 5,    // flavour (taunts change)
		kMaimedHand = 1u << 6,   // -one-handed
	};

	enum class Rank : std::uint8_t
	{
		Grunt,
		Lieutenant,
		Captain,
		Warchief,
		Count
	};

	enum class Status : std::uint8_t
	{
		Active,      // alive, roaming
		Dead,
		Reconciled,  // feud settled by parley
		Retired,     // left the life (spared, honourable)
	};

	enum class Hold : std::uint8_t
	{
		Haafingar,
		Hjaalmarch,
		ThePale,
		Winterhold,
		TheReach,
		Whiterun,
		Eastmarch,
		Falkreath,
		TheRift,
		Count,
		Unknown = 0xFF
	};
	inline constexpr std::size_t kHoldCount = static_cast<std::size_t>(Hold::Count);

	enum class FactionId : std::uint8_t
	{
		Forsworn,
		SilverHand,
		Thalmor,
		Volkihar,
		Necromancers,
		Bloodhorn,  // bandit clan, north/west
		AshenHand,  // bandit clan, east
		Greymane,   // bandit clan, south/central
		Count,
		None = 0xFF
	};
	inline constexpr std::size_t kFactionCount = static_cast<std::size_t>(FactionId::Count);

	// Which in-game group an NPC was detected as. The plugin layer figures this
	// out; the core decides which Bloodfeud faction it maps to.
	enum class GameGroup : std::uint8_t
	{
		Bandit,
		Forsworn,
		SilverHand,
		Thalmor,
		Vampire,
		Necromancer,
		Other,
	};

	enum class MemoryKind : std::uint8_t
	{
		Met,
		WoundedBy,       // player hurt them and they lived
		FledFrom,        // escaped a fight with the player
		LeftForDead,     // "killed" but cheated death
		KinSlain,        // player killed a relative
		LeaderSlain,     // player killed their captain/warchief
		Spared,          // player spared them after they yielded
		Humiliated,      // yielded and was interrogated
		Betrayed,        // lied to the player under interrogation
		AmbushedPlayer,  // came hunting the player
		Parley,          // received a letter of parley
		Promoted,
	};

	struct Memory
	{
		std::uint32_t day{ 0 };
		MemoryKind kind{ MemoryKind::Met };
		NpcId other{ kNoNpc };
		std::string place;
	};

	struct Npc
	{
		NpcId id{ kNoNpc };

		// game linkage (opaque to the core; raw form IDs owned by the plugin)
		std::uint32_t refId{ 0 };    // current in-world reference, 0 if none
		std::uint32_t baseId{ 0 };   // spawnable base form
		std::uint32_t sourceRef{ 0 };  // original placed ref we first met

		// identity
		std::string first;
		std::string surname;
		std::string epithet;  // "the Cruel" - earned or generated
		Culture culture{ Culture::Nord };
		bool female{ false };
		Temperament temper{ Temperament::Hothead };
		std::uint32_t traits{ 0 };
		std::string trade;
		std::string faith;
		std::string backstory;

		// standing
		FactionId faction{ FactionId::None };
		Rank rank{ Rank::Grunt };
		Hold home{ Hold::Unknown };
		Status status{ Status::Active };

		// feelings toward the player, 0..100
		std::int16_t hatred{ 0 };
		std::int16_t fear{ 0 };
		std::int16_t respect{ 0 };

		// career
		std::uint32_t edges{ 0 };
		std::uint32_t injuries{ 0 };
		std::uint16_t level{ 1 };
		std::uint16_t encounters{ 0 };
		std::uint16_t cheatedDeath{ 0 };
		bool nemesis{ false };
		bool edgesKnown{ false };  // revealed through interrogation
		bool secretlyAlive{ false };  // cheated death; announced when they return
		std::uint32_t lastSeenDay{ 0 };
		std::uint32_t lastHuntDay{ 0 };
		NpcId kinOf{ kNoNpc };  // swore vengeance for this relative

		std::vector<Memory> memories;

		[[nodiscard]] std::string FullName() const;
		[[nodiscard]] std::string ShortName() const;
		[[nodiscard]] bool Alive() const { return status == Status::Active; }
	};

	struct Faction
	{
		FactionId id{ FactionId::None };
		std::string name;
		std::int32_t strength{ 500 };  // fighting power 0..1000
		std::int32_t wealth{ 300 };
		std::array<std::int16_t, kHoldCount> influence{};      // 0..100 per hold
		std::array<std::int16_t, kFactionCount> relations{};   // -100..100
		std::int16_t playerStanding{ 0 };                       // -100..100
		NpcId leader{ kNoNpc };
		std::vector<NpcId> captains;
		std::vector<NpcId> lieutenants;
		std::uint32_t killsByPlayer{ 0 };
		bool destroyed{ false };
	};

	enum class ChronicleKind : std::uint8_t
	{
		Personal,  // something between the player and an NPC
		Faction,   // wars, succession, territory
		Life,      // NPC life events (drink, dogs, feuds)
		Rumor,
	};

	struct ChronicleEntry
	{
		std::uint32_t day{ 0 };
		ChronicleKind kind{ ChronicleKind::Personal };
		std::string text;
	};

	// Generic stats the plugin maps to Skyrim actor values.
	enum class Stat : std::uint8_t
	{
		Health,
		Stamina,
		Magicka,
		DamageResist,
		ResistFire,
		ResistFrost,
		ResistShock,
		ResistMagic,
		SpeedMult,
		AttackDamageMult,
		OneHanded,
		TwoHanded,
		Archery,
		Block,
	};

	struct StatMod
	{
		Stat stat;
		float delta;
	};

	// A "lead" from interrogation: go to this hold and the target will be there
	// (or it is a trap).
	struct Lead
	{
		NpcId target{ kNoNpc };
		NpcId informant{ kNoNpc };
		Hold hold{ Hold::Unknown };
		bool trap{ false };
		std::uint32_t expiresDay{ 0 };
	};

	// Things other mods (e.g. the Ledger economy) may want to react to.
	struct WorldEvent
	{
		enum class Kind : std::uint8_t
		{
			OfficerSlain,   // the player killed a captain or warchief
			HoldSeized,     // a faction took control of a hold's wilds
			Skirmish,       // factions fought in a hold
			Ambush,         // the player was ambushed in a hold
			FactionBroken,  // a faction collapsed
		};
		Kind kind{ Kind::Skirmish };
		Hold hold{ Hold::Unknown };
		FactionId faction{ FactionId::None };
		float magnitude{ 1.0f };
	};

	struct Settings
	{
		float cheatDeathChance = 0.25f;     // named enemy "dies" but returns
		float kinVengeanceChance = 0.35f;   // killing a named enemy spawns an avenger
		float yieldChanceMult = 1.0f;
		int nemesisHatred = 60;             // hatred needed to become a nemesis
		int maxNemeses = 10;                // active nemeses at once
		float ambushChancePerHour = 0.06f;  // per eligible nemesis, outdoors only
		float ambushCooldownDays = 1.0f;    // minimum time between ambushes
		int maxEscorts = 3;
		bool factionWar = true;
		bool lifeEvents = true;
		int chronicleLimit = 400;
	};
}

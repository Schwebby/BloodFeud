#pragma once
#include "bloodfeud/Rng.hpp"
#include "bloodfeud/Types.hpp"

#include <functional>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace bf
{
	enum class DamageType : std::uint8_t
	{
		Physical,
		Arrow,
		Fire,
		Frost,
		Shock,
		Count
	};

	// What the plugin knows about an actor it wants to register.
	struct ActorInfo
	{
		std::uint32_t refId{ 0 };
		std::uint32_t baseId{ 0 };  // spawnable base form (0 = can't respawn)
		GameGroup group{ GameGroup::Other };
		std::string raceName;
		bool female{ false };
		std::uint16_t level{ 1 };
		Hold hold{ Hold::Unknown };
		std::string place;  // location name, e.g. "Embershard Mine"
		bool boss{ false };  // "Bandit Chief", "Forsworn Briarheart", ...
	};

	struct AmbushPlan
	{
		NpcId leader{ kNoNpc };
		NpcId informant{ kNoNpc };  // set when a lead was a trap
		int escorts{ 0 };
		bool trap{ false };
		bool fromLead{ false };
		std::string taunt;
	};

	struct Intel
	{
		NpcId informant{ kNoNpc };
		NpcId target{ kNoNpc };
		bool truthful{ false };
		Hold hold{ Hold::Unknown };
		std::string text;  // what the informant says
	};

	enum class DeathResult : std::uint8_t
	{
		NotTracked,
		Died,
		CheatedDeath,
	};

	class World
	{
	public:
		explicit World(Settings settings = {}, std::uint64_t seed = 0xB100DFEEDull);

		void Reset(std::uint64_t seed = 0xB100DFEEDull);
		Settings& GetSettings() { return _settings; }

		// ---- identity -----------------------------------------------------
		[[nodiscard]] NpcId FindByRef(std::uint32_t refId) const;
		[[nodiscard]] Npc* Get(NpcId id);
		[[nodiscard]] const Npc* Get(NpcId id) const;
		// Gives the actor a persistent identity (or binds it to a waiting officer).
		NpcId Register(const ActorInfo& info, std::uint32_t day);
		// Plugin tells us the in-world reference is gone (unloaded / deleted).
		void Unbind(NpcId id);

		// ---- combat -----------------------------------------------------------
		void OnEngaged(NpcId id, std::uint32_t day, const std::string& place);
		void OnWounded(NpcId id, DamageType type, float damage);
		// Combat ended and the NPC is alive (escaped, or the player left).
		void OnSurvived(NpcId id, std::uint32_t day, const std::string& place);
		DeathResult OnKilledByPlayer(NpcId id, std::uint32_t day, const std::string& place);
		void OnKilledByOther(NpcId id, std::uint32_t day);

		// ---- yield / interrogation ---------------------------------------------
		[[nodiscard]] bool RollYield(NpcId id, float healthFraction);
		Intel Interrogate(NpcId informant, std::uint32_t day);
		void Spare(NpcId id, std::uint32_t day, const std::string& place);
		void Execute(NpcId id, std::uint32_t day, const std::string& place);

		// ---- parley (the Skyrim stand-in for enRAGE's telegrams) ----------------
		[[nodiscard]] int ParleyCost(NpcId id) const;
		// Returns true if the feud is settled. Gold is consumed either way.
		bool Parley(NpcId id, int gold, std::uint32_t day);

		// ---- time ---------------------------------------------------------------
		// Call as game hours pass. Returns an ambush to stage, if any.
		std::optional<AmbushPlan> TickHours(float daysPassed, float hoursElapsed, std::uint16_t playerLevel, Hold current, bool outdoors, bool safeToSpawn);
		void OnAmbushSpawned(NpcId id, std::uint32_t refId, std::uint32_t day);
		// Daily faction simulation and life events. Safe to call repeatedly with the same day.
		void TickDay(std::uint32_t day);

		// ---- stats for the plugin ------------------------------------------------
		[[nodiscard]] std::vector<StatMod> StatModsFor(const Npc& npc, std::uint16_t playerLevel, std::uint16_t actorLevel) const;
		// Make sure an officer we generated off-screen has something to spawn as.
		bool EnsureBase(Npc& npc);

		// ---- reporting -------------------------------------------------------------
		std::vector<std::string> DrainNotices();
		std::vector<WorldEvent> DrainEvents();
		// 0..1: how dangerous the roads of a hold are, from outlaw influence and strength.
		[[nodiscard]] float RoadDanger(Hold h) const;
		[[nodiscard]] const std::vector<ChronicleEntry>& Chronicle() const { return _chronicle; }
		[[nodiscard]] std::vector<NpcId> Nemeses() const;
		[[nodiscard]] std::vector<NpcId> KnownEnemies() const;  // met, alive, sorted by hatred
		[[nodiscard]] const Faction& GetFaction(FactionId f) const { return _factions[static_cast<std::size_t>(f)]; }
		[[nodiscard]] Faction& GetFaction(FactionId f) { return _factions[static_cast<std::size_t>(f)]; }
		[[nodiscard]] const std::vector<Lead>& Leads() const { return _leads; }
		[[nodiscard]] std::string Dossier(NpcId id) const;
		[[nodiscard]] std::string FactionReport(FactionId f) const;
		[[nodiscard]] std::size_t NpcCount() const { return _npcs.size(); }
		[[nodiscard]] FactionId ControllerOf(Hold h, bool banditsOnly) const;

		// ---- persistence -----------------------------------------------------------
		[[nodiscard]] std::vector<std::uint8_t> Save() const;
		bool Load(std::span<const std::uint8_t> data);
		// Re-map stored form IDs after a load-order change. Return 0 to drop.
		void RemapForms(const std::function<std::uint32_t(std::uint32_t)>& resolveBase, const std::function<std::uint32_t(std::uint32_t)>& resolveRef);

		static constexpr std::uint32_t kSaveVersion = 1;

	private:
		Npc& NewNpc();
		NpcId CreateNamed(FactionId f, Culture c, bool female, Rank r, Hold home, std::uint32_t baseId, GameGroup group);
		FactionId FactionFor(GameGroup g, Hold h) const;
		void Remember(Npc& npc, MemoryKind k, std::uint32_t day, const std::string& place, NpcId other = kNoNpc);
		void Log(std::uint32_t day, ChronicleKind kind, std::string text, bool notify = false);
		void Notify(std::string text);
		void Emit(WorldEvent::Kind k, Hold h, FactionId f, float magnitude = 1.0f);
		void CheckNemesis(Npc& npc, std::uint32_t day);
		void Promote(Npc& npc, std::uint32_t day, bool survivedPlayer);
		void ApplyFightInjuries(Npc& npc);
		void HandleDeath(Npc& npc, std::uint32_t day, const std::string& place, bool byPlayer);
		void SpawnAvenger(const Npc& dead, std::uint32_t day);
		void FillRanks(Faction& f, std::uint32_t day);
		void Succession(Faction& f, std::uint32_t day);
		void RemoveFromRanks(Faction& f, NpcId id);
		void SimulateFactions(std::uint32_t day);
		void LifeEvents(std::uint32_t day);
		void InitFactions();
		std::string KinName(const Npc& npc) const;

		Settings _settings;
		Rng _rng;
		NpcId _nextId{ 1 };
		std::vector<Npc> _npcs;  // index = id - 1
		std::unordered_map<std::uint32_t, NpcId> _byRef;
		std::array<Faction, kFactionCount> _factions{};
		std::array<std::vector<std::uint32_t>, kFactionCount> _sampleBases{};
		std::vector<ChronicleEntry> _chronicle;
		std::vector<Lead> _leads;
		std::vector<std::string> _notices;
		std::vector<WorldEvent> _events;  // not saved; drained every tick
		std::uint32_t _lastDay{ 0 };
		float _lastAmbushDay{ -100.0f };

		// per-fight scratch (not saved)
		struct FightScratch
		{
			std::array<float, static_cast<std::size_t>(DamageType::Count)> dmg{};
			int hits{ 0 };
			bool yielded{ false };
			bool yieldRolled{ false };
		};
		std::unordered_map<NpcId, FightScratch> _fight;
	};
}

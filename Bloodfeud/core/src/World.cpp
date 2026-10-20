#include "bloodfeud/World.hpp"

#include "bloodfeud/Text.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>

namespace bf
{
	namespace
	{
		constexpr std::size_t kMaxMemories = 24;
		constexpr std::size_t kMaxSampleBases = 12;

		template <class T>
		T Clamp(T v, T lo, T hi) { return std::min(hi, std::max(lo, v)); }

		void AddFeeling(std::int16_t& f, int delta) { f = static_cast<std::int16_t>(Clamp(static_cast<int>(f) + delta, 0, 100)); }

		int RankValue(Rank r) { return static_cast<int>(r); }

		// -------- tiny binary serializer -------------------------------------
		class Writer
		{
		public:
			std::vector<std::uint8_t> buf;
			template <class T>
			void Pod(const T& v)
			{
				const auto* p = reinterpret_cast<const std::uint8_t*>(&v);
				buf.insert(buf.end(), p, p + sizeof(T));
			}
			void Str(const std::string& s)
			{
				Pod(static_cast<std::uint32_t>(s.size()));
				buf.insert(buf.end(), s.begin(), s.end());
			}
		};

		class Reader
		{
		public:
			explicit Reader(std::span<const std::uint8_t> d) : _d(d) {}
			template <class T>
			bool Pod(T& v)
			{
				if (_pos + sizeof(T) > _d.size()) return Fail();
				std::memcpy(&v, _d.data() + _pos, sizeof(T));
				_pos += sizeof(T);
				return true;
			}
			bool Str(std::string& s)
			{
				std::uint32_t n{};
				if (!Pod(n) || n > (1u << 20) || _pos + n > _d.size()) return Fail();
				s.assign(reinterpret_cast<const char*>(_d.data() + _pos), n);
				_pos += n;
				return true;
			}
			bool Count(std::uint32_t& n, std::uint32_t max)
			{
				return Pod(n) && (n <= max || Fail());
			}
			[[nodiscard]] bool Ok() const { return _ok; }

		private:
			bool Fail()
			{
				_ok = false;
				return false;
			}
			std::span<const std::uint8_t> _d;
			std::size_t _pos{ 0 };
			bool _ok{ true };
		};

		Culture CultureForFaction(Rng& rng, FactionId f)
		{
			switch (f) {
			case FactionId::Forsworn:
				return Culture::Reachman;
			case FactionId::Thalmor:
				return rng.Chance(0.85f) ? Culture::Altmer : Culture::Bosmer;
			case FactionId::SilverHand:
				return rng.Chance(0.7f) ? Culture::Nord : Culture::Imperial;
			case FactionId::Necromancers:
				{
					static constexpr std::array<Culture, 4> c{ Culture::Breton, Culture::Imperial, Culture::Dunmer, Culture::Altmer };
					return c[rng.Below(c.size())];
				}
			default:
				{
					static constexpr std::array<Culture, 12> c{ Culture::Nord, Culture::Nord, Culture::Nord, Culture::Nord, Culture::Imperial, Culture::Breton, Culture::Redguard, Culture::Dunmer, Culture::Orc, Culture::Khajiit, Culture::Argonian, Culture::Bosmer };
					return c[rng.Below(c.size())];
				}
			}
		}

		GameGroup GroupForFaction(FactionId f)
		{
			switch (f) {
			case FactionId::Forsworn: return GameGroup::Forsworn;
			case FactionId::SilverHand: return GameGroup::SilverHand;
			case FactionId::Thalmor: return GameGroup::Thalmor;
			case FactionId::Volkihar: return GameGroup::Vampire;
			case FactionId::Necromancers: return GameGroup::Necromancer;
			case FactionId::None: return GameGroup::Other;
			default: return GameGroup::Bandit;
			}
		}

		bool IsBanditClan(FactionId f) { return f == FactionId::Bloodhorn || f == FactionId::AshenHand || f == FactionId::Greymane; }
	}

	// =======================================================================
	World::World(Settings settings, std::uint64_t seed) : _settings(settings), _rng(seed)
	{
		InitFactions();
	}

	void World::Reset(std::uint64_t seed)
	{
		const Settings keep = _settings;
		*this = World(keep, seed);
	}

	void World::InitFactions()
	{
		using H = Hold;
		auto set = [&](FactionId id, std::initializer_list<std::pair<H, int>> infl) {
			auto& f = _factions[static_cast<std::size_t>(id)];
			f.id = id;
			f.name = std::string(text::FactionName(id));
			f.influence.fill(0);
			f.relations.fill(-10);
			f.relations[static_cast<std::size_t>(id)] = 100;
			for (auto& [h, v] : infl) f.influence[static_cast<std::size_t>(h)] = static_cast<std::int16_t>(v);
			f.strength = 450 + _rng.Range(0, 150);
			f.wealth = 200 + _rng.Range(0, 200);
		};
		set(FactionId::Forsworn, { { H::TheReach, 80 }, { H::Haafingar, 10 }, { H::Falkreath, 10 } });
		set(FactionId::SilverHand, { { H::Whiterun, 30 }, { H::Falkreath, 25 }, { H::TheReach, 15 }, { H::Hjaalmarch, 20 } });
		set(FactionId::Thalmor, { { H::Haafingar, 40 }, { H::TheReach, 15 }, { H::Whiterun, 10 }, { H::Eastmarch, 10 }, { H::TheRift, 10 }, { H::ThePale, 10 } });
		set(FactionId::Volkihar, { { H::Haafingar, 20 }, { H::Hjaalmarch, 25 }, { H::ThePale, 20 }, { H::Winterhold, 15 } });
		set(FactionId::Necromancers, { { H::Hjaalmarch, 25 }, { H::ThePale, 25 }, { H::Winterhold, 20 }, { H::Falkreath, 25 }, { H::TheRift, 10 } });
		set(FactionId::Bloodhorn, { { H::Haafingar, 45 }, { H::Hjaalmarch, 40 }, { H::ThePale, 40 }, { H::TheReach, 20 } });
		set(FactionId::AshenHand, { { H::Winterhold, 45 }, { H::Eastmarch, 50 }, { H::TheRift, 50 }, { H::ThePale, 15 } });
		set(FactionId::Greymane, { { H::Whiterun, 55 }, { H::Falkreath, 50 }, { H::TheRift, 15 }, { H::TheReach, 15 } });

		auto rel = [&](FactionId a, FactionId b, int v) {
			_factions[static_cast<std::size_t>(a)].relations[static_cast<std::size_t>(b)] = static_cast<std::int16_t>(v);
			_factions[static_cast<std::size_t>(b)].relations[static_cast<std::size_t>(a)] = static_cast<std::int16_t>(v);
		};
		rel(FactionId::Bloodhorn, FactionId::AshenHand, -45);
		rel(FactionId::Bloodhorn, FactionId::Greymane, -40);
		rel(FactionId::AshenHand, FactionId::Greymane, -45);
		rel(FactionId::Forsworn, FactionId::Thalmor, -80);
		rel(FactionId::Forsworn, FactionId::Bloodhorn, -50);
		rel(FactionId::Forsworn, FactionId::SilverHand, -30);
		rel(FactionId::SilverHand, FactionId::Volkihar, -60);
		rel(FactionId::Volkihar, FactionId::Necromancers, 30);
		rel(FactionId::Thalmor, FactionId::Greymane, -25);
		rel(FactionId::Thalmor, FactionId::Necromancers, -40);

		for (auto& f : _factions) {
			FillRanks(f, 0);
		}
		_chronicle.clear();  // founding officers aren't news
	}

	// =======================================================================
	// identity

	Npc& World::NewNpc()
	{
		Npc& n = _npcs.emplace_back();
		n.id = _nextId++;
		return n;
	}

	Npc* World::Get(NpcId id) { return id && id <= _npcs.size() ? &_npcs[id - 1] : nullptr; }
	const Npc* World::Get(NpcId id) const { return id && id <= _npcs.size() ? &_npcs[id - 1] : nullptr; }

	NpcId World::FindByRef(std::uint32_t refId) const
	{
		if (!refId) return kNoNpc;
		auto it = _byRef.find(refId);
		return it == _byRef.end() ? kNoNpc : it->second;
	}

	FactionId World::ControllerOf(Hold h, bool banditsOnly) const
	{
		if (h == Hold::Unknown) return FactionId::None;
		FactionId best = FactionId::None;
		int bestV = 0;
		for (const auto& f : _factions) {
			if (f.destroyed || (banditsOnly && !IsBanditClan(f.id))) continue;
			const int v = f.influence[static_cast<std::size_t>(h)];
			if (v > bestV) {
				bestV = v;
				best = f.id;
			}
		}
		return best;
	}

	FactionId World::FactionFor(GameGroup g, Hold h) const
	{
		switch (g) {
		case GameGroup::Forsworn: return FactionId::Forsworn;
		case GameGroup::SilverHand: return FactionId::SilverHand;
		case GameGroup::Thalmor: return FactionId::Thalmor;
		case GameGroup::Vampire: return FactionId::Volkihar;
		case GameGroup::Necromancer: return FactionId::Necromancers;
		case GameGroup::Bandit:
			{
				const FactionId c = ControllerOf(h, true);
				return c == FactionId::None ? FactionId::Greymane : c;
			}
		default: return FactionId::None;
		}
	}

	NpcId World::CreateNamed(FactionId f, Culture c, bool female, Rank r, Hold home, std::uint32_t baseId, GameGroup group)
	{
		Npc& n = NewNpc();
		n.culture = c;
		n.female = female;
		n.faction = f;
		n.rank = r;
		n.home = home;
		n.baseId = baseId;
		n.level = static_cast<std::uint16_t>(6 + RankValue(r) * 8 + _rng.Range(0, 6));
		text::GenerateIdentity(_rng, n, group);
		return n.id;
	}

	NpcId World::Register(const ActorInfo& info, std::uint32_t day)
	{
		if (const NpcId existing = FindByRef(info.refId)) {
			return existing;
		}
		const FactionId fid = FactionFor(info.group, info.hold);
		const Culture culture = text::CultureFromRaceName(info.raceName, info.group);

		if (fid != FactionId::None && info.baseId) {
			auto& samples = _sampleBases[static_cast<std::size_t>(fid)];
			if (std::ranges::find(samples, info.baseId) == samples.end()) {
				if (samples.size() >= kMaxSampleBases) samples.erase(samples.begin());
				samples.push_back(info.baseId);
			}
		}

		// Dungeon bosses become the faction officers we've been simulating.
		if (info.boss && fid != FactionId::None) {
			auto& f = GetFaction(fid);
			std::vector<NpcId> pool;
			for (NpcId id : f.captains) pool.push_back(id);
			if (f.leader) pool.push_back(f.leader);
			for (NpcId id : f.lieutenants) pool.push_back(id);
			NpcId pick = kNoNpc;
			int bestScore = -1;
			for (NpcId id : pool) {
				Npc* o = Get(id);
				if (!o || !o->Alive() || o->refId || o->encounters > 0 || o->female != info.female) continue;
				int score = 1;
				if (o->home == info.hold) score += 4;
				if (o->culture == culture) score += 2;
				if (o->rank == Rank::Captain) score += 2;
				if (o->rank == Rank::Warchief) score += (o->home == info.hold ? 1 : -3);
				if (score > bestScore) {
					bestScore = score;
					pick = id;
				}
			}
			if (pick && bestScore >= 3) {
				Npc& o = *Get(pick);
				o.refId = info.refId;
				o.sourceRef = info.refId;
				if (info.baseId) o.baseId = info.baseId;
				o.culture = culture;
				o.level = std::max<std::uint16_t>(o.level, info.level);
				if (info.hold != Hold::Unknown) o.home = info.hold;
				_byRef[info.refId] = pick;
				return pick;
			}
		}

		const Rank rank = info.boss ? Rank::Lieutenant : Rank::Grunt;
		const NpcId id = CreateNamed(fid, culture, info.female, rank, info.hold, info.baseId, info.group);
		Npc& n = *Get(id);
		n.refId = info.refId;
		n.sourceRef = info.refId;
		n.level = std::max<std::uint16_t>(1, info.level);
		if (fid != FactionId::None && rank == Rank::Lieutenant) {
			GetFaction(fid).lieutenants.push_back(id);
		}
		_byRef[info.refId] = id;
		(void)day;
		return id;
	}

	void World::Unbind(NpcId id)
	{
		if (Npc* n = Get(id)) {
			if (n->refId) _byRef.erase(n->refId);
			n->refId = 0;
		}
		_fight.erase(id);
	}

	bool World::EnsureBase(Npc& npc)
	{
		if (npc.baseId) return true;
		if (npc.faction == FactionId::None) return false;
		const auto& samples = _sampleBases[static_cast<std::size_t>(npc.faction)];
		if (samples.empty()) return false;
		npc.baseId = _rng.Pick(samples);
		return true;
	}

	// =======================================================================
	// helpers

	void World::Remember(Npc& npc, MemoryKind k, std::uint32_t day, const std::string& place, NpcId other)
	{
		npc.memories.push_back({ day, k, other, place });
		if (npc.memories.size() > kMaxMemories) {
			npc.memories.erase(npc.memories.begin());
		}
	}

	void World::Log(std::uint32_t day, ChronicleKind kind, std::string textLine, bool notify)
	{
		if (notify) Notify(textLine);
		_chronicle.push_back({ day, kind, std::move(textLine) });
		const auto limit = static_cast<std::size_t>(std::max(50, _settings.chronicleLimit));
		if (_chronicle.size() > limit) {
			_chronicle.erase(_chronicle.begin(), _chronicle.begin() + static_cast<std::ptrdiff_t>(_chronicle.size() - limit));
		}
	}

	void World::Notify(std::string textLine)
	{
		if (_notices.size() < 32) _notices.push_back(std::move(textLine));
	}

	void World::Emit(WorldEvent::Kind k, Hold h, FactionId f, float magnitude)
	{
		if (_events.size() < 256) _events.push_back({ k, h, f, magnitude });
	}

	std::vector<WorldEvent> World::DrainEvents()
	{
		std::vector<WorldEvent> out;
		out.swap(_events);
		return out;
	}

	float World::RoadDanger(Hold h) const
	{
		if (h == Hold::Unknown) return 0.0f;
		float sum = 0.0f;
		for (const auto& f : _factions) {
			if (f.destroyed) continue;
			sum += f.influence[static_cast<std::size_t>(h)] * (static_cast<float>(f.strength) / 1000.0f);
		}
		return Clamp(sum / 200.0f, 0.0f, 1.0f);
	}

	std::vector<std::string> World::DrainNotices()
	{
		std::vector<std::string> out;
		out.swap(_notices);
		return out;
	}

	std::string World::KinName(const Npc& npc) const
	{
		if (const Npc* k = Get(npc.kinOf)) return k->ShortName();
		return "my kin";
	}

	void World::CheckNemesis(Npc& npc, std::uint32_t day)
	{
		if (npc.nemesis || !npc.Alive()) return;
		if (npc.hatred >= _settings.nemesisHatred) {
			std::size_t active = 0;
			for (const auto& o : _npcs) active += (o.nemesis && o.Alive()) ? 1 : 0;
			if (static_cast<int>(active) >= _settings.maxNemeses) return;  // waits in line
			npc.nemesis = true;
			npc.lastHuntDay = day;  // give the player a breather
			const std::string_view why = npc.kinOf ? "for blood spilled" : "against you";
			Log(day, ChronicleKind::Personal, text::Fmt("{0} has sworn a blood-oath {1}.", { npc.FullName(), why }), true);
		}
	}

	void World::Promote(Npc& npc, std::uint32_t day, bool survivedPlayer)
	{
		npc.level = static_cast<std::uint16_t>(std::min(81, npc.level + _rng.Range(2, 4)));
		if (survivedPlayer) {
			static constexpr std::array<std::uint32_t, 10> strengths{ kIronHide, kFireproof, kFrostborn, kStormward, kSpellbreaker, kBerserker, kSwift, kDuelist, kMarksman, kHardy };
			if (_rng.Chance(0.6f)) npc.edges |= strengths[_rng.Below(strengths.size())];
		}
		// Rank up into an open slot.
		if (npc.faction == FactionId::None || npc.rank >= Rank::Captain) return;
		auto& f = GetFaction(npc.faction);
		const bool nemesisBoost = npc.nemesis && _rng.Chance(0.5f);
		if (npc.rank == Rank::Grunt && (f.lieutenants.size() < 4 || nemesisBoost)) {
			npc.rank = Rank::Lieutenant;
			f.lieutenants.push_back(npc.id);
			Remember(npc, MemoryKind::Promoted, day, {});
			Log(day, ChronicleKind::Faction, text::Fmt("{0} was made a {1} of {2}.", { npc.ShortName(), text::RankName(npc.rank, npc.faction), f.name }), npc.encounters > 0);
		} else if (npc.rank == Rank::Lieutenant && (f.captains.size() < 3 || nemesisBoost)) {
			RemoveFromRanks(f, npc.id);
			npc.rank = Rank::Captain;
			f.captains.push_back(npc.id);
			Remember(npc, MemoryKind::Promoted, day, {});
			Log(day, ChronicleKind::Faction, text::Fmt("{0} rose to {1} of {2}.", { npc.ShortName(), text::RankName(npc.rank, npc.faction), f.name }), npc.encounters > 0);
		}
	}

	void World::ApplyFightInjuries(Npc& npc)
	{
		auto it = _fight.find(npc.id);
		if (it == _fight.end()) return;
		const auto& d = it->second.dmg;
		const auto top = static_cast<DamageType>(std::distance(d.begin(), std::ranges::max_element(d)));
		switch (top) {
		case DamageType::Fire:
			npc.injuries |= kBurnScars;
			npc.edges |= _rng.Chance(0.5f) ? kFireproof : kFearsFire;
			break;
		case DamageType::Frost:
			npc.injuries |= kFrostbitten;
			npc.edges |= _rng.Chance(0.5f) ? kFrostborn : kBrittle;
			break;
		case DamageType::Shock:
			npc.injuries |= kScarred;
			npc.edges |= _rng.Chance(0.5f) ? kStormward : kConductive;
			break;
		case DamageType::Arrow:
			npc.injuries |= _rng.Chance(0.35f) ? kOneEye : kScarred;
			break;
		default:
			{
				static constexpr std::array<std::uint32_t, 4> p{ kLimp, kMaimedHand, kBrokenJaw, kScarred };
				npc.injuries |= p[_rng.Below(p.size())];
				break;
			}
		}
		// Edges can't be both strength and weakness of the same element.
		if ((npc.edges & kFireproof) && (npc.edges & kFearsFire)) npc.edges &= ~kFearsFire;
		if ((npc.edges & kFrostborn) && (npc.edges & kBrittle)) npc.edges &= ~kBrittle;
		if ((npc.edges & kStormward) && (npc.edges & kConductive)) npc.edges &= ~kConductive;
	}

	// =======================================================================
	// combat

	void World::OnEngaged(NpcId id, std::uint32_t day, const std::string& place)
	{
		Npc* n = Get(id);
		if (!n || !n->Alive()) return;
		if (_fight.contains(id)) return;  // already in this fight
		_fight[id] = {};
		if (n->encounters == 0) Remember(*n, MemoryKind::Met, day, place);
		n->encounters++;
		n->lastSeenDay = day;
	}

	void World::OnWounded(NpcId id, DamageType type, float damage)
	{
		Npc* n = Get(id);
		if (!n || !n->Alive()) return;
		auto& s = _fight[id];
		s.dmg[static_cast<std::size_t>(type)] += std::max(0.0f, damage);
		if (s.hits < 10) AddFeeling(n->hatred, 1);
		s.hits++;
	}

	void World::OnSurvived(NpcId id, std::uint32_t day, const std::string& place)
	{
		Npc* n = Get(id);
		if (!n || !n->Alive()) return;
		auto it = _fight.find(id);
		if (it == _fight.end()) return;
		const auto scratch = it->second;
		if (scratch.yielded) {
			// handled by Spare(); the plugin may still report the fight ending
			_fight.erase(it);
			return;
		}
		if (scratch.hits > 0) {
			const bool fled = scratch.hits >= 3;
			Remember(*n, fled ? MemoryKind::FledFrom : MemoryKind::WoundedBy, day, place);
			AddFeeling(n->hatred, (fled ? 15 : 8) + (n->temper == Temperament::Proud ? 10 : 0) + ((n->traits & kVengeful) ? 8 : 0));
			AddFeeling(n->fear, 5);
			if (fled && _rng.Chance(0.4f)) ApplyFightInjuries(*n);
			if (fled || n->nemesis) Promote(*n, day, true);
			if (n->nemesis) {
				Log(day, ChronicleKind::Personal, text::Fmt("{0} escaped you at {1}, and grows stronger.", { n->ShortName(), place }), true);
			}
			CheckNemesis(*n, day);
		}
		_fight.erase(id);
	}

	DeathResult World::OnKilledByPlayer(NpcId id, std::uint32_t day, const std::string& place)
	{
		Npc* n = Get(id);
		if (!n || !n->Alive()) return DeathResult::NotTracked;

		float chance = _settings.cheatDeathChance;
		if (n->nemesis) chance *= 1.6f;
		if (n->rank >= Rank::Captain) chance *= 1.3f;
		if (n->edges & kHardy) chance *= 1.3f;
		if (n->encounters <= 1 && !n->nemesis) chance *= 0.6f;  // strangers usually just die
		chance *= std::pow(0.6f, static_cast<float>(n->cheatedDeath));
		if (_fight.contains(id) && _fight[id].yielded) chance = 0.0f;

		if (_rng.Chance(chance)) {
			ApplyFightInjuries(*n);
			n->injuries |= kScarred;
			n->cheatedDeath++;
			n->secretlyAlive = true;
			AddFeeling(n->hatred, 40);
			n->hatred = static_cast<std::int16_t>(std::max<int>(n->hatred, _settings.nemesisHatred));
			AddFeeling(n->fear, 10);
			if (n->epithet.empty() || _rng.Chance(0.5f)) n->epithet = text::EarnedEpithet(_rng, *n);
			Remember(*n, MemoryKind::LeftForDead, day, place);
			Promote(*n, day, true);
			if (n->refId) _byRef.erase(n->refId);
			n->refId = 0;
			_fight.erase(id);
			// Silent on purpose: the player finds out when they come back.
			const bool wasNemesis = n->nemesis;
			CheckNemesis(*n, day);
			if (!wasNemesis && n->nemesis) {
				// CheckNemesis logged an oath; hide the name until the return.
				_chronicle.pop_back();
				if (!_notices.empty()) _notices.pop_back();
			}
			return DeathResult::CheatedDeath;
		}
		HandleDeath(*n, day, place, true);
		return DeathResult::Died;
	}

	void World::OnKilledByOther(NpcId id, std::uint32_t day)
	{
		Npc* n = Get(id);
		if (!n || !n->Alive()) return;
		HandleDeath(*n, day, {}, false);
	}

	void World::HandleDeath(Npc& deadRef, std::uint32_t day, const std::string& place, bool byPlayer)
	{
		// Succession below can create NPCs and reallocate _npcs, so work on a
		// snapshot and write status through the id.
		deadRef.status = Status::Dead;
		deadRef.secretlyAlive = false;
		if (deadRef.refId) _byRef.erase(deadRef.refId);
		deadRef.refId = 0;
		_fight.erase(deadRef.id);
		const Npc npc = deadRef;

		const bool notable = npc.nemesis || npc.rank >= Rank::Lieutenant || npc.encounters > 1;
		if (npc.faction != FactionId::None) {
			auto& f = GetFaction(npc.faction);
			const Rank r = npc.rank;
			RemoveFromRanks(f, npc.id);
			if (byPlayer) {
				f.killsByPlayer++;
				const int standingHit = r == Rank::Warchief ? 25 : r == Rank::Captain ? 10 : r == Rank::Lieutenant ? 5 : 2;
				f.playerStanding = static_cast<std::int16_t>(Clamp(f.playerStanding - standingHit, -100, 100));
				f.strength = std::max(0, f.strength - (r == Rank::Warchief ? 60 : r == Rank::Captain ? 25 : r == Rank::Lieutenant ? 12 : 4));
				if (r >= Rank::Captain) {
					Emit(WorldEvent::Kind::OfficerSlain, npc.home, npc.faction, r == Rank::Warchief ? 2.0f : 1.0f);
					for (auto& other : _npcs) {
						if (other.faction == npc.faction && other.Alive() && other.id != npc.id && other.rank >= Rank::Lieutenant) {
							Remember(other, MemoryKind::LeaderSlain, day, place, npc.id);
							AddFeeling(other.hatred, r == Rank::Warchief ? 20 : 10);
							CheckNemesis(other, day);
						}
					}
				}
			}
			if (r == Rank::Warchief) Succession(f, day);
		}
		if (byPlayer && notable) {
			const std::string what = npc.faction == FactionId::None ? std::string{} : text::Fmt(", {0} of {1}", { text::RankName(npc.rank, npc.faction), text::FactionName(npc.faction) });
			Log(day, ChronicleKind::Personal, text::Fmt("You killed {0}{1}{2}.", { npc.FullName(), what, place.empty() ? std::string_view{} : std::string_view(" at " + place) }), npc.nemesis);
		}
		if (byPlayer) {
			float kin = _settings.kinVengeanceChance;
			if (npc.rank >= Rank::Lieutenant) kin *= 1.4f;
			if (npc.nemesis) kin *= 1.3f;
			if (npc.encounters <= 1 && npc.rank == Rank::Grunt) kin *= 0.5f;  // nameless grunts rarely have kin who care
			if (_rng.Chance(kin)) SpawnAvenger(npc, day);
		}
	}

	void World::SpawnAvenger(const Npc& dead, std::uint32_t day)
	{
		const bool female = _rng.Chance(0.4f);
		const Npc deadCopy = dead;  // creating may reallocate _npcs
		const NpcId id = CreateNamed(deadCopy.faction, deadCopy.culture, female, deadCopy.rank == Rank::Grunt ? Rank::Grunt : Rank::Lieutenant, deadCopy.home, deadCopy.baseId, GroupForFaction(deadCopy.faction));
		Npc& n = *Get(id);
		if (!deadCopy.surname.empty()) {
			n.surname = deadCopy.surname;
		} else if (deadCopy.culture == Culture::Nord) {
			n.surname = deadCopy.first + (female ? "-Daughter" : "-Son");
		}
		static constexpr std::array<std::string_view, 3> maleKin{ "brother", "son", "cousin" };
		static constexpr std::array<std::string_view, 3> femaleKin{ "sister", "daughter", "widow" };
		const std::string_view relation = female ? femaleKin[_rng.Below(3)] : maleKin[_rng.Below(3)];
		n.kinOf = deadCopy.id;
		n.backstory = text::Fmt("is the {0} of {1}, whom you killed", { relation, deadCopy.FullName() });
		n.traits |= kVengeful;
		n.hatred = 75;
		n.level = static_cast<std::uint16_t>(std::max<int>(n.level, deadCopy.level));
		if (n.faction != FactionId::None && n.rank == Rank::Lieutenant) GetFaction(n.faction).lieutenants.push_back(n.id);
		Remember(n, MemoryKind::KinSlain, day, deadCopy.memories.empty() ? std::string{} : deadCopy.memories.back().place, deadCopy.id);
		Log(day, ChronicleKind::Personal, text::Fmt("{0}, {1} of {2}, has learned who killed them.", { n.FullName(), relation, deadCopy.ShortName() }), false);
		CheckNemesis(n, day);
	}

	// =======================================================================
	// yield / interrogation

	bool World::RollYield(NpcId id, float healthFraction)
	{
		Npc* n = Get(id);
		if (!n || !n->Alive() || healthFraction > 0.25f) return false;
		auto& s = _fight[id];
		if (s.yieldRolled) return false;
		s.yieldRolled = true;
		static constexpr std::array<float, static_cast<std::size_t>(Temperament::Count)> base{ 0.10f, 0.70f, 0.25f, 0.10f, 0.45f, 0.03f, 0.35f, 0.20f };
		float p = base[static_cast<std::size_t>(n->temper)] * (1.0f + n->fear / 100.0f) * _settings.yieldChanceMult;
		if (n->nemesis) p *= 0.3f;
		if (n->rank == Rank::Warchief) p *= 0.5f;
		if (_rng.Chance(p)) {
			s.yielded = true;
			Notify(text::Fmt("{0}: \"{1}\"", { n->ShortName(), text::YieldLine(_rng, *n) }));
			return true;
		}
		return false;
	}

	Intel World::Interrogate(NpcId informantId, std::uint32_t day)
	{
		Intel intel;
		intel.informant = informantId;
		Npc* inf = Get(informantId);
		if (!inf) return intel;

		// Who could they give up?
		std::vector<NpcId> candidates;
		if (inf->faction != FactionId::None) {
			const auto& f = GetFaction(inf->faction);
			if (f.leader) candidates.push_back(f.leader);
			for (NpcId c : f.captains) candidates.push_back(c);
			if (inf->rank <= Rank::Lieutenant) {
				for (NpcId c : f.lieutenants) candidates.push_back(c);
			}
		}
		for (const auto& o : _npcs) {
			if (o.nemesis && o.Alive() && o.faction == inf->faction) candidates.push_back(o.id);
		}
		std::erase_if(candidates, [&](NpcId c) {
			const Npc* t = Get(c);
			return c == informantId || !t || !t->Alive() || t->refId;
		});
		std::ranges::sort(candidates);
		candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());

		Remember(*inf, MemoryKind::Humiliated, day, {});
		AddFeeling(inf->hatred, inf->temper == Temperament::Proud ? 25 : 10);
		AddFeeling(inf->fear, 20);

		if (candidates.empty()) {
			intel.text = text::Fmt("{0} spits blood. \"I don't know anything worth your time.\"", { inf->ShortName() });
			return intel;
		}
		// Prefer higher-ranked targets.
		std::ranges::sort(candidates, [&](NpcId a, NpcId b) { return Get(a)->rank > Get(b)->rank; });
		const NpcId targetId = candidates[std::min<std::size_t>(candidates.size() - 1, _rng.Below(std::min<std::uint32_t>(3, static_cast<std::uint32_t>(candidates.size()))))];
		Npc& t = *Get(targetId);

		float truth = 0.6f + inf->fear / 200.0f;
		switch (inf->temper) {
		case Temperament::Coward: truth += 0.25f; break;
		case Temperament::Honorable: truth += 0.2f; break;
		case Temperament::Cunning: truth -= 0.35f; break;
		case Temperament::Zealot: truth -= 0.25f; break;
		case Temperament::Proud: truth -= 0.1f; break;
		case Temperament::Unhinged: truth -= 0.15f; break;
		default: break;
		}
		if (inf->traits & kLoyal) truth -= 0.15f;
		if (inf->traits & kTraitorous) truth += 0.2f;
		truth = Clamp(truth, 0.05f, 0.95f);
		intel.truthful = _rng.Chance(truth);
		intel.target = targetId;

		if (!EnsureBase(t)) {
			// Nothing to spawn them as yet; still give the player the name.
			intel.truthful = true;
		}

		Hold hold = t.home;
		if (hold == Hold::Unknown) hold = static_cast<Hold>(_rng.Below(kHoldCount));
		if (!intel.truthful) {
			Hold fake = hold;
			while (fake == hold) fake = static_cast<Hold>(_rng.Below(kHoldCount));
			hold = fake;
			Remember(*inf, MemoryKind::Betrayed, day, {}, targetId);
		}
		intel.hold = hold;

		std::string weakness;
		if (intel.truthful) {
			t.edgesKnown = true;
			const std::string w = text::EdgeList(t.edges, false);
			weakness = w == "none" ? std::string("Doesn't have a weak spot I know of.") : text::Fmt("Weak spot? {0}.", { w });
		} else {
			static constexpr std::array<std::string_view, 4> fakes{ "Fears Fire", "Glass Jaw", "Slow", "Weak to Shock" };
			weakness = text::Fmt("Weak spot? {0}.", { fakes[_rng.Below(fakes.size())] });
		}
		intel.text = text::Fmt("\"{0}, {1} of {2}. Camps in {3} these days. {4}\"", { t.FullName(), text::RankName(t.rank, t.faction), text::FactionName(t.faction), text::HoldName(hold), weakness });

		_leads.push_back({ targetId, informantId, hold, !intel.truthful, day + 7 });
		Log(day, ChronicleKind::Personal, text::Fmt("You interrogated {0}, who named {1}.", { inf->ShortName(), t.ShortName() }));
		return intel;
	}

	void World::Spare(NpcId id, std::uint32_t day, const std::string& place)
	{
		Npc* n = Get(id);
		if (!n || !n->Alive()) return;
		_fight.erase(id);
		float retire = 0.1f;
		if (n->temper == Temperament::Honorable) retire = 0.6f;
		if (n->temper == Temperament::Coward) retire = 0.4f;
		AddFeeling(n->fear, 15);
		switch (n->temper) {
		case Temperament::Proud: AddFeeling(n->hatred, 30); break;
		case Temperament::Hothead: AddFeeling(n->hatred, 15); break;
		case Temperament::Cold: AddFeeling(n->hatred, 10); break;
		case Temperament::Honorable:
			AddFeeling(n->respect, 40);
			AddFeeling(n->hatred, -30);
			break;
		default: AddFeeling(n->respect, 15); break;
		}
		Remember(*n, MemoryKind::Spared, day, place);
		if (_rng.Chance(retire) && !n->nemesis) {
			n->status = Status::Retired;
			if (n->faction != FactionId::None) RemoveFromRanks(GetFaction(n->faction), id);
			AddFeeling(n->respect, 20);
			Log(day, ChronicleKind::Personal, text::Fmt("{0} has left {1}, owing you a life.", { n->ShortName(), text::FactionName(n->faction) }), true);
		} else {
			Log(day, ChronicleKind::Personal, text::Fmt("You spared {0} at {1}.", { n->ShortName(), place }));
			CheckNemesis(*n, day);
		}
	}

	void World::Execute(NpcId id, std::uint32_t day, const std::string& place)
	{
		Npc* n = Get(id);
		if (!n || !n->Alive()) return;
		const FactionId f = n->faction;
		Log(day, ChronicleKind::Personal, text::Fmt("You executed {0} after they yielded.", { n->FullName() }));
		HandleDeath(*n, day, place, true);
		if (f != FactionId::None) {
			auto& fac = GetFaction(f);
			fac.playerStanding = static_cast<std::int16_t>(Clamp(fac.playerStanding - 5, -100, 100));
			for (auto& o : _npcs) {
				if (o.faction == f && o.Alive()) AddFeeling(o.fear, 5);
			}
		}
	}

	// =======================================================================
	// parley

	int World::ParleyCost(NpcId id) const
	{
		const Npc* n = Get(id);
		if (!n) return 0;
		static constexpr std::array<float, 4> rankMult{ 1.0f, 1.5f, 2.5f, 4.0f };
		float cost = (100.0f + n->level * 15.0f + n->hatred * 5.0f) * rankMult[static_cast<std::size_t>(n->rank)];
		if (n->traits & kGreedy) cost *= 0.7f;
		if (n->temper == Temperament::Proud) cost *= 1.5f;
		return static_cast<int>(std::round(cost / 50.0f)) * 50;
	}

	bool World::Parley(NpcId id, int gold, std::uint32_t day)
	{
		Npc* n = Get(id);
		if (!n || !n->Alive()) return false;
		const int cost = ParleyCost(id);
		static constexpr std::array<float, static_cast<std::size_t>(Temperament::Count)> base{ 0.40f, 0.80f, 0.70f, 0.35f, 0.60f, 0.05f, 0.85f, 0.25f };
		float p = base[static_cast<std::size_t>(n->temper)] + n->respect / 200.0f - std::max(0, n->hatred - 60) / 200.0f;
		if (n->traits & kGreedy) p += 0.15f;
		if (n->traits & kVengeful) p -= 0.2f;
		if (n->kinOf) p -= 0.2f;
		if (gold < cost) p *= 0.1f * static_cast<float>(gold) / static_cast<float>(std::max(1, cost));
		p = Clamp(p, 0.0f, 0.95f);
		Remember(*n, MemoryKind::Parley, day, {});
		if (_rng.Chance(p)) {
			n->status = Status::Reconciled;
			n->nemesis = false;
			n->hatred = 0;
			AddFeeling(n->respect, 25);
			if (n->faction != FactionId::None) {
				auto& f = GetFaction(n->faction);
				f.playerStanding = static_cast<std::int16_t>(Clamp(f.playerStanding + 5, -100, 100));
				const bool wasLeader = f.leader == id;
				RemoveFromRanks(f, id);  // takes the gold and leaves the life
				if (wasLeader) Succession(f, day);
				n = Get(id);  // Succession may have reallocated
			}
			Log(day, ChronicleKind::Personal, text::Fmt("{0} accepted your weregild of {1} gold. The feud is ended.", { n->FullName(), std::to_string(gold) }), true);
			return true;
		}
		AddFeeling(n->hatred, 10);
		n->lastHuntDay = 0;  // they're coming, soon
		Log(day, ChronicleKind::Personal, text::Fmt("{0} burned your letter and kept the gold.", { n->FullName() }), true);
		return false;
	}

	// =======================================================================
	// time

	std::optional<AmbushPlan> World::TickHours(float daysPassed, float hoursElapsed, std::uint16_t playerLevel, Hold current, bool outdoors, bool safeToSpawn)
	{
		(void)playerLevel;
		const auto today = static_cast<std::uint32_t>(daysPassed);
		std::erase_if(_leads, [&](const Lead& l) {
			const Npc* t = Get(l.target);
			return l.expiresDay < today || !t || !t->Alive();
		});
		if (!outdoors || !safeToSpawn || hoursElapsed <= 0.0f) return std::nullopt;
		if (daysPassed - _lastAmbushDay < _settings.ambushCooldownDays) return std::nullopt;

		auto finish = [&](Npc& leader, AmbushPlan plan) -> std::optional<AmbushPlan> {
			plan.leader = leader.id;
			const int byRank = std::array<int, 4>{ 0, 1, 2, 3 }[static_cast<std::size_t>(leader.rank)];
			plan.escorts = std::min(_settings.maxEscorts, plan.escorts ? plan.escorts : byRank + (leader.nemesis ? 0 : 1));
			if (plan.taunt.empty()) plan.taunt = text::Fmt("{0}: \"{1}\"", { leader.ShortName(), text::Taunt(_rng, leader, KinName(leader)) });
			leader.lastHuntDay = today;
			_lastAmbushDay = daysPassed;
			Emit(WorldEvent::Kind::Ambush, current, leader.faction);
			if (leader.secretlyAlive) {
				leader.secretlyAlive = false;
				Log(today, ChronicleKind::Personal, text::Fmt("{0} has returned from the grave.", { leader.FullName() }), true);
			}
			return plan;
		};

		// 1) Leads the player is following.
		for (auto it = _leads.begin(); it != _leads.end(); ++it) {
			if (it->hold != current) continue;
			Npc* t = Get(it->target);
			if (!t || !t->Alive() || t->refId || !EnsureBase(*t)) continue;
			AmbushPlan plan;
			plan.fromLead = true;
			plan.trap = it->trap;
			if (it->trap) {
				plan.escorts = _settings.maxEscorts;
				Npc* inf = Get(it->informant);
				if (inf && inf->Alive() && !inf->refId && EnsureBase(*inf)) {
					plan.informant = inf->id;
					plan.taunt = text::Fmt("{0}: \"Did you really think I'd tell you the truth?\"", { inf->ShortName() });
					Remember(*inf, MemoryKind::AmbushedPlayer, today, std::string(text::HoldName(current)));
				}
			} else {
				plan.escorts = 1;
				plan.taunt = text::Fmt("You've found {0}.", { t->FullName() });
			}
			_leads.erase(it);
			return finish(*t, plan);
		}

		// 2) Nemeses on the hunt.
		std::vector<NpcId> order;
		for (const auto& n : _npcs) {
			if (n.nemesis && n.Alive() && !n.refId && (today >= n.lastHuntDay + 1 || n.lastHuntDay == 0)) order.push_back(n.id);
		}
		for (std::size_t i = order.size(); i > 1; --i) std::swap(order[i - 1], order[_rng.Below(static_cast<std::uint32_t>(i))]);
		for (NpcId id : order) {
			Npc& n = *Get(id);
			if (!EnsureBase(n)) continue;
			float p = _settings.ambushChancePerHour * (n.hatred / 100.0f) * (n.home == current ? 1.6f : 0.8f);
			if (n.traits & kVengeful) p *= 1.3f;
			if (n.lastHuntDay == 0) p *= 3.0f;  // insulted by a parley
			const float total = 1.0f - std::pow(1.0f - Clamp(p, 0.0f, 0.95f), hoursElapsed);
			if (_rng.Chance(total)) {
				Remember(n, MemoryKind::AmbushedPlayer, today, std::string(text::HoldName(current)));
				return finish(n, {});
			}
		}

		// 3) Faction hit squads for players who have bled a faction badly.
		for (auto& f : _factions) {
			if (f.destroyed || f.playerStanding > -50) continue;
			const float p = 0.02f * hoursElapsed * (-f.playerStanding / 100.0f) * (f.influence[static_cast<std::size_t>(current == Hold::Unknown ? Hold::Whiterun : current)] > 0 ? 1.5f : 0.5f);
			if (!_rng.Chance(p)) continue;
			std::vector<NpcId> pool = f.captains;
			pool.insert(pool.end(), f.lieutenants.begin(), f.lieutenants.end());
			std::erase_if(pool, [&](NpcId id) { Npc* n = Get(id); return !n || !n->Alive() || n->refId || !EnsureBase(*n); });
			if (pool.empty()) continue;
			Npc& leader = *Get(pool[_rng.Below(static_cast<std::uint32_t>(pool.size()))]);
			AmbushPlan plan;
			plan.escorts = _rng.Range(2, std::max(2, _settings.maxEscorts));
			plan.taunt = text::Fmt("{0}: \"{1} sends its regards.\"", { leader.ShortName(), f.name });
			Log(today, ChronicleKind::Faction, text::Fmt("{0} sent {1} to hunt you down.", { f.name, leader.ShortName() }));
			return finish(leader, plan);
		}
		return std::nullopt;
	}

	void World::OnAmbushSpawned(NpcId id, std::uint32_t refId, std::uint32_t day)
	{
		Npc* n = Get(id);
		if (!n) return;
		if (n->refId) _byRef.erase(n->refId);
		n->refId = refId;
		if (refId) _byRef[refId] = id;
		n->lastSeenDay = day;
	}

	void World::TickDay(std::uint32_t day)
	{
		if (_lastDay == 0) {
			_lastDay = day;
			return;
		}
		if (day <= _lastDay) return;
		const std::uint32_t from = std::max(_lastDay + 1, day > 30 ? day - 30 : 1u);
		for (std::uint32_t d = from; d <= day; ++d) {
			if (_settings.factionWar) SimulateFactions(d);
			LifeEvents(d);
		}
		_lastDay = day;
	}

	// =======================================================================
	// factions

	void World::RemoveFromRanks(Faction& f, NpcId id)
	{
		if (f.leader == id) f.leader = kNoNpc;
		std::erase(f.captains, id);
		std::erase(f.lieutenants, id);
	}

	void World::Succession(Faction& f, std::uint32_t day)
	{
		if (f.leader) return;
		auto score = [&](NpcId id) {
			const Npc* n = Get(id);
			return n ? n->level + n->encounters * 2 + n->cheatedDeath * 5 + (n->nemesis ? 10 : 0) : -1;
		};
		NpcId heir = kNoNpc;
		if (!f.captains.empty()) {
			heir = *std::ranges::max_element(f.captains, {}, score);
		} else if (!f.lieutenants.empty()) {
			heir = *std::ranges::max_element(f.lieutenants, {}, score);
		}
		if (heir) {
			RemoveFromRanks(f, heir);
		} else {
			const Hold home = static_cast<Hold>(std::distance(f.influence.begin(), std::ranges::max_element(f.influence)));
			heir = CreateNamed(f.id, CultureForFaction(_rng, f.id), _rng.Chance(0.3f), Rank::Warchief, home, kNoNpc, GroupForFaction(f.id));
			if (!_sampleBases[static_cast<std::size_t>(f.id)].empty()) EnsureBase(*Get(heir));
		}
		Npc& h = *Get(heir);
		h.rank = Rank::Warchief;
		h.level = static_cast<std::uint16_t>(std::max<int>(h.level, 30));
		f.leader = heir;
		if (day > 0) {
			Log(day, ChronicleKind::Faction, text::Fmt("{0} is the new {1} of {2}.", { h.FullName(), text::RankName(Rank::Warchief, f.id), f.name }), h.encounters > 0 || f.killsByPlayer > 0);
		}
	}

	void World::FillRanks(Faction& f, std::uint32_t day)
	{
		if (f.destroyed) return;
		if (!f.leader) Succession(f, day);
		auto homeHold = [&]() {
			int total = std::accumulate(f.influence.begin(), f.influence.end(), 0);
			if (total <= 0) return static_cast<Hold>(_rng.Below(kHoldCount));
			int roll = _rng.Range(0, total - 1);
			for (std::size_t i = 0; i < kHoldCount; ++i) {
				if (roll < f.influence[i]) return static_cast<Hold>(i);
				roll -= f.influence[i];
			}
			return Hold::Whiterun;
		};
		while (f.captains.size() < 2) {
			if (!f.lieutenants.empty()) {
				const NpcId up = f.lieutenants.front();
				f.lieutenants.erase(f.lieutenants.begin());
				Npc& n = *Get(up);
				n.rank = Rank::Captain;
				n.level = static_cast<std::uint16_t>(n.level + 4);
				f.captains.push_back(up);
				if (day > 0 && n.encounters > 0) {
					Log(day, ChronicleKind::Faction, text::Fmt("{0} was raised to {1} of {2}.", { n.ShortName(), text::RankName(Rank::Captain, f.id), f.name }));
				}
			} else {
				f.captains.push_back(CreateNamed(f.id, CultureForFaction(_rng, f.id), _rng.Chance(0.3f), Rank::Captain, homeHold(), kNoNpc, GroupForFaction(f.id)));
			}
		}
		while (f.lieutenants.size() < 3) {
			f.lieutenants.push_back(CreateNamed(f.id, CultureForFaction(_rng, f.id), _rng.Chance(0.3f), Rank::Lieutenant, homeHold(), kNoNpc, GroupForFaction(f.id)));
		}
	}

	void World::SimulateFactions(std::uint32_t day)
	{
		std::array<FactionId, kHoldCount> before{};
		for (std::size_t h = 0; h < kHoldCount; ++h) before[h] = ControllerOf(static_cast<Hold>(h), false);

		for (auto& f : _factions) {
			if (f.destroyed) {
				if (_rng.Chance(0.03f)) {
					f.destroyed = false;
					f.strength = 200;
					f.leader = kNoNpc;
					FillRanks(f, day);
					Log(day, ChronicleKind::Faction, text::Fmt("{0} has risen again under {1}.", { f.name, Get(f.leader)->FullName() }), true);
				}
				continue;
			}
			const int infl = std::accumulate(f.influence.begin(), f.influence.end(), 0);
			const int upkeep = 4 + static_cast<int>(f.captains.size()) * 2 + static_cast<int>(f.lieutenants.size()) + f.strength / 200;
			f.wealth = Clamp(f.wealth + infl / 10 + 9 - upkeep, 0, 3000);
			f.strength = Clamp(f.strength + (f.wealth > 0 ? 3 + f.wealth / 600 : -4), 0, 1000);
			// Grudges slowly cool: standing drifts back toward 0.
			if (f.playerStanding < 0 && _rng.Chance(0.2f)) f.playerStanding++;
		}

		// Rich factions buy trouble: sellswords sent against a neighbour.
		for (auto& f : _factions) {
			if (f.destroyed || f.wealth < 1200 || !_rng.Chance(0.1f)) continue;
			std::vector<std::size_t> rivals;
			for (const auto& o : _factions) {
				if (o.id == f.id || o.destroyed) continue;
				for (std::size_t h = 0; h < kHoldCount; ++h) {
					if (f.influence[h] >= 10 && o.influence[h] >= 10) {
						rivals.push_back(static_cast<std::size_t>(o.id));
						break;
					}
				}
			}
			if (rivals.empty()) continue;
			const std::size_t t = rivals[_rng.Below(static_cast<std::uint32_t>(rivals.size()))];
			const int v = Clamp(f.relations[t] - _rng.Range(15, 30), -100, 100);
			f.relations[t] = _factions[t].relations[static_cast<std::size_t>(f.id)] = static_cast<std::int16_t>(v);
			f.wealth -= 400;
			f.strength = std::min(1000, f.strength + 60);
			Log(day, ChronicleKind::Faction, text::Fmt("{0} is hiring sellswords to move against {1}.", { f.name, _factions[t].name }));
		}

		// Relations drift a little each day.
		for (std::size_t a = 0; a < kFactionCount; ++a) {
			for (std::size_t b = a + 1; b < kFactionCount; ++b) {
				if (_rng.Chance(0.3f)) {
					int v = _factions[a].relations[b] + _rng.Range(-4, 4);
					if (_rng.Chance(0.15f)) v += v > -10 ? -1 : 1;  // drift back toward wary neutrality
					v = Clamp(v, -100, 100);
					_factions[a].relations[b] = _factions[b].relations[a] = static_cast<std::int16_t>(v);
				}
			}
		}

		// Skirmishes between factions at war that share a hold.
		for (std::size_t a = 0; a < kFactionCount; ++a) {
			for (std::size_t b = a + 1; b < kFactionCount; ++b) {
				auto& A = _factions[a];
				auto& B = _factions[b];
				if (A.destroyed || B.destroyed || A.relations[b] > -35 || !_rng.Chance(0.25f)) continue;
				std::vector<std::size_t> shared;
				for (std::size_t h = 0; h < kHoldCount; ++h) {
					if (A.influence[h] >= 10 && B.influence[h] >= 10) shared.push_back(h);
				}
				if (shared.empty()) continue;
				const std::size_t h = shared[_rng.Below(static_cast<std::uint32_t>(shared.size()))];
				const float pa = A.strength * (A.influence[h] + 10) * (0.5f + _rng.Unit());
				const float pb = B.strength * (B.influence[h] + 10) * (0.5f + _rng.Unit());
				Faction& win = pa >= pb ? A : B;
				Faction& lose = pa >= pb ? B : A;
				const int delta = _rng.Range(5, 15);
				win.influence[h] = static_cast<std::int16_t>(std::min(100, win.influence[h] + delta));
				lose.influence[h] = static_cast<std::int16_t>(std::max(0, lose.influence[h] - delta));
				lose.strength = std::max(0, lose.strength - _rng.Range(10, 40));
				win.strength = std::max(0, win.strength - _rng.Range(5, 15));
				win.wealth += _rng.Range(10, 60);

				std::string line = text::Fmt("In {0}, {1} routed {2}.", { text::HoldName(static_cast<Hold>(h)), win.name, lose.name });
				bool loud = false;
				if (_rng.Chance(0.25f)) {
					const float r = _rng.Unit();
					std::vector<NpcId>* pool = r < 0.7f ? &lose.lieutenants : &lose.captains;
					NpcId victim = kNoNpc;
					if (r >= 0.95f && lose.leader) {
						victim = lose.leader;
					} else if (!pool->empty()) {
						victim = (*pool)[_rng.Below(static_cast<std::uint32_t>(pool->size()))];
					}
					if (Npc* v = Get(victim); v && v->Alive() && !v->refId) {
						line += text::Fmt(" {0}, {1}, was among the dead.", { v->FullName(), text::RankName(v->rank, lose.id) });
						loud = v->encounters > 0 || v->nemesis || v->rank == Rank::Warchief;
						if (v->nemesis) line += " Your nemesis died at someone else's hand.";
						HandleDeath(*v, day, std::string(text::HoldName(static_cast<Hold>(h))), false);
					}
				}
				Emit(WorldEvent::Kind::Skirmish, static_cast<Hold>(h), win.id, static_cast<float>(delta) / 10.0f);
				Log(day, ChronicleKind::Faction, std::move(line), loud);
			}
		}

		// Pacts.
		for (std::size_t a = 0; a < kFactionCount; ++a) {
			for (std::size_t b = a + 1; b < kFactionCount; ++b) {
				if (_factions[a].relations[b] >= 60 && _rng.Chance(0.01f) && !_factions[a].destroyed && !_factions[b].destroyed) {
					Log(day, ChronicleKind::Faction, text::Fmt("{0} and {1} have sworn a pact.", { _factions[a].name, _factions[b].name }));
				}
			}
		}

		for (auto& f : _factions) {
			if (!f.destroyed && f.strength <= 0) {
				f.destroyed = true;
				Emit(WorldEvent::Kind::FactionBroken, Hold::Unknown, f.id);
				Log(day, ChronicleKind::Faction, text::Fmt("{0} has been broken. Its survivors scatter.", { f.name }), true);
				continue;
			}
			// Leaders spend wealth to replace losses.
			if (f.wealth > 150) FillRanks(f, day);
		}

		for (std::size_t h = 0; h < kHoldCount; ++h) {
			const FactionId now = ControllerOf(static_cast<Hold>(h), false);
			const int margin = now == FactionId::None || before[h] == FactionId::None ? 100 :
				GetFaction(now).influence[h] - GetFaction(before[h]).influence[h];
			if (now != before[h] && now != FactionId::None && margin >= 8) {
				Emit(WorldEvent::Kind::HoldSeized, static_cast<Hold>(h), now);
				Log(day, ChronicleKind::Rumor, text::Fmt("{0} now rules the wilds of {1}.", { text::FactionName(now), text::HoldName(static_cast<Hold>(h)) }), true);
			}
		}
	}

	void World::LifeEvents(std::uint32_t day)
	{
		std::vector<NpcId> pool;
		std::vector<NpcId> indebted;
		for (const auto& n : _npcs) {
			if (n.Alive() && (n.encounters > 0 || n.rank >= Rank::Captain)) pool.push_back(n.id);
			if (n.status == Status::Retired && n.respect >= 40) indebted.push_back(n.id);
		}
		if (_settings.lifeEvents && !pool.empty()) {
			const int count = _rng.Chance(0.3f) ? 1 : 0;
			for (int i = 0; i < count; ++i) {
				Npc& n = *Get(pool[_rng.Below(static_cast<std::uint32_t>(pool.size()))]);
				std::string ep;
				Log(day, ChronicleKind::Life, text::LifeEvent(_rng, n, ep));
			}
		}
		// Someone you spared sends word.
		for (NpcId id : indebted) {
			if (!_rng.Chance(0.05f)) continue;
			Npc& friendNpc = *Get(id);
			std::vector<NpcId> targets;
			for (const auto& n : _npcs) {
				if (n.Alive() && !n.refId && (n.nemesis || n.rank >= Rank::Captain) && n.faction == friendNpc.faction) targets.push_back(n.id);
			}
			if (targets.empty()) continue;
			Npc& t = *Get(targets[_rng.Below(static_cast<std::uint32_t>(targets.size()))]);
			if (!EnsureBase(t)) continue;
			t.edgesKnown = true;
			_leads.push_back({ t.id, id, t.home, false, day + 7 });
			Log(day, ChronicleKind::Personal, text::Fmt("A courier brings a note from {0}: \"{1} is in {2}. Weak spot: {3}. We're square.\"", { friendNpc.ShortName(), t.FullName(), text::HoldName(t.home), text::EdgeList(t.edges, false) }), true);
			friendNpc.respect = 0;  // one favour per life spared
			break;
		}
	}

	// =======================================================================
	// stats

	std::vector<StatMod> World::StatModsFor(const Npc& n, std::uint16_t playerLevel, std::uint16_t actorLevel) const
	{
		std::vector<StatMod> m;
		const int r = RankValue(n.rank);
		if (r) {
			m.push_back({ Stat::Health, 25.0f * r });
			m.push_back({ Stat::AttackDamageMult, 0.1f * r });
		}
		if (n.cheatedDeath) m.push_back({ Stat::Health, 30.0f * n.cheatedDeath });
		// Nemeses keep pace with the player.
		const int want = n.nemesis ? std::max<int>(n.level, playerLevel + r) : n.level;
		if (want > actorLevel) m.push_back({ Stat::Health, 8.0f * (want - actorLevel) });

		const auto e = n.edges;
		if (e & kIronHide) m.push_back({ Stat::DamageResist, 100.0f });
		if (e & kFireproof) m.push_back({ Stat::ResistFire, 50.0f });
		if (e & kFrostborn) m.push_back({ Stat::ResistFrost, 50.0f });
		if (e & kStormward) m.push_back({ Stat::ResistShock, 50.0f });
		if (e & kSpellbreaker) m.push_back({ Stat::ResistMagic, 30.0f });
		if (e & kBerserker) m.push_back({ Stat::AttackDamageMult, 0.35f });
		if (e & kSwift) m.push_back({ Stat::SpeedMult, 20.0f });
		if (e & kDuelist) {
			m.push_back({ Stat::OneHanded, 25.0f });
			m.push_back({ Stat::Block, 25.0f });
		}
		if (e & kMarksman) m.push_back({ Stat::Archery, 30.0f });
		if (e & kHardy) m.push_back({ Stat::Health, 75.0f });
		if (e & kFearsFire) m.push_back({ Stat::ResistFire, -50.0f });
		if (e & kBrittle) m.push_back({ Stat::ResistFrost, -50.0f });
		if (e & kConductive) m.push_back({ Stat::ResistShock, -50.0f });
		if (e & kGlassJaw) m.push_back({ Stat::Health, -40.0f });
		if (e & kSlow) m.push_back({ Stat::SpeedMult, -15.0f });
		if (e & kWeakArm) m.push_back({ Stat::AttackDamageMult, -0.25f });

		const auto i = n.injuries;
		if (i & kLimp) m.push_back({ Stat::SpeedMult, -10.0f });
		if (i & kOneEye) m.push_back({ Stat::Archery, -20.0f });
		if (i & kMaimedHand) m.push_back({ Stat::OneHanded, -15.0f });
		return m;
	}

	// =======================================================================
	// reporting

	std::vector<NpcId> World::Nemeses() const
	{
		std::vector<NpcId> out;
		for (const auto& n : _npcs) {
			if (n.nemesis && n.Alive()) out.push_back(n.id);
		}
		std::ranges::sort(out, [&](NpcId a, NpcId b) { return Get(a)->hatred > Get(b)->hatred; });
		return out;
	}

	std::vector<NpcId> World::KnownEnemies() const
	{
		std::vector<NpcId> out;
		for (const auto& n : _npcs) {
			if (n.Alive() && n.encounters > 0 && !n.secretlyAlive) out.push_back(n.id);
		}
		std::ranges::sort(out, [&](NpcId a, NpcId b) {
			const Npc* x = Get(a);
			const Npc* y = Get(b);
			if (x->nemesis != y->nemesis) return x->nemesis;
			return x->hatred > y->hatred;
		});
		return out;
	}

	std::string World::Dossier(NpcId id) const
	{
		const Npc* n = Get(id);
		if (!n) return {};
		std::string s = n->FullName();
		if (n->nemesis) s += "  [NEMESIS]";
		if (n->status == Status::Dead) s += "  [DEAD]";
		if (n->status == Status::Reconciled) s += "  [FEUD ENDED]";
		if (n->status == Status::Retired) s += "  [LEFT THE LIFE]";
		if (n->kinOf) {
			if (const Npc* k = Get(n->kinOf)) s += text::Fmt("\nKin of {0}", { k->FullName() });
		}
		s += "\n";
		if (n->faction != FactionId::None) {
			s += text::Fmt("{0} of {1}\n", { text::RankName(n->rank, n->faction), text::FactionName(n->faction) });
		}
		s += text::Fmt("{0} {1}, {2}. Lvl {3}. Met {4}x.\n", { text::TemperName(n->temper), text::CultureName(n->culture), text::HoldName(n->home), std::to_string(n->level), std::to_string(n->encounters) });
		s += text::Fmt("Once {0}; {1}.\n", { n->trade, n->backstory });
		s += text::Fmt("Prays to {0}. Habits: {1}.\n", { n->faith, text::TraitList(n->traits) });
		if (n->injuries) s += text::Fmt("Wounds: {0}.\n", { text::InjuryList(n->injuries) });
		const bool seenEnough = n->encounters >= 2 || n->edgesKnown;
		s += text::Fmt("Strengths: {0}\n", { seenEnough ? text::EdgeList(n->edges, true) : std::string("unknown") });
		s += text::Fmt("Weaknesses: {0}\n", { n->edgesKnown ? text::EdgeList(n->edges, false) : std::string("unknown (interrogate their allies)") });
		s += text::Fmt("Hatred {0}  Fear {1}  Respect {2}", { std::to_string(n->hatred), std::to_string(n->fear), std::to_string(n->respect) });
		if (n->Alive() && n->hatred > 0) {
			s += text::Fmt("\nWeregild to end the feud: {0} gold", { std::to_string(ParleyCost(id)) });
		}
		return s;
	}

	std::string World::FactionReport(FactionId fid) const
	{
		const auto& f = GetFaction(fid);
		auto nameOf = [&](NpcId id) -> std::string {
			const Npc* n = Get(id);
			if (!n) return "?";
			std::string s = n->ShortName();
			if (n->nemesis) s += " (nemesis)";
			else if (n->encounters == 0) s += " (unmet)";
			return s;
		};
		std::string s = f.name;
		if (f.destroyed) return s + "\nBroken and scattered.";
		s += text::Fmt("\nStrength {0}  Coin {1}  Your standing {2}\n", { std::to_string(f.strength), std::to_string(f.wealth), std::to_string(f.playerStanding) });
		std::string holds;
		for (std::size_t h = 0; h < kHoldCount; ++h) {
			if (ControllerOf(static_cast<Hold>(h), false) == fid) {
				if (!holds.empty()) holds += ", ";
				holds += text::HoldName(static_cast<Hold>(h));
			}
		}
		s += text::Fmt("Rules: {0}\n", { holds.empty() ? std::string("nothing") : holds });
		s += text::Fmt("{0}: {1}\n", { text::RankName(Rank::Warchief, fid), f.leader ? nameOf(f.leader) : std::string("none") });
		for (NpcId c : f.captains) s += text::Fmt("  {0}: {1}\n", { text::RankName(Rank::Captain, fid), nameOf(c) });
		for (NpcId l : f.lieutenants) s += text::Fmt("    {0}: {1}\n", { text::RankName(Rank::Lieutenant, fid), nameOf(l) });
		std::string wars;
		std::string pacts;
		for (const auto& o : _factions) {
			if (o.id == fid || o.destroyed) continue;
			const int r = f.relations[static_cast<std::size_t>(o.id)];
			if (r <= -40) wars += (wars.empty() ? "" : ", ") + o.name;
			if (r >= 50) pacts += (pacts.empty() ? "" : ", ") + o.name;
		}
		if (!wars.empty()) s += "At war with: " + wars + "\n";
		if (!pacts.empty()) s += "Allied with: " + pacts + "\n";
		return s;
	}

	// =======================================================================
	// persistence

	std::vector<std::uint8_t> World::Save() const
	{
		Writer w;
		w.Pod(std::uint32_t{ 0x44554642 });  // 'BFUD'
		w.Pod(kSaveVersion);
		for (int i = 0; i < 4; ++i) w.Pod(_rng.State()[i]);
		w.Pod(_nextId);
		w.Pod(_lastDay);
		w.Pod(_lastAmbushDay);

		w.Pod(static_cast<std::uint32_t>(_npcs.size()));
		for (const auto& n : _npcs) {
			w.Pod(n.id);
			w.Pod(n.refId);
			w.Pod(n.baseId);
			w.Pod(n.sourceRef);
			w.Str(n.first);
			w.Str(n.surname);
			w.Str(n.epithet);
			w.Pod(n.culture);
			w.Pod(static_cast<std::uint8_t>(n.female));
			w.Pod(n.temper);
			w.Pod(n.traits);
			w.Str(n.trade);
			w.Str(n.faith);
			w.Str(n.backstory);
			w.Pod(n.faction);
			w.Pod(n.rank);
			w.Pod(n.home);
			w.Pod(n.status);
			w.Pod(n.hatred);
			w.Pod(n.fear);
			w.Pod(n.respect);
			w.Pod(n.edges);
			w.Pod(n.injuries);
			w.Pod(n.level);
			w.Pod(n.encounters);
			w.Pod(n.cheatedDeath);
			w.Pod(static_cast<std::uint8_t>(n.nemesis | (n.edgesKnown << 1) | (n.secretlyAlive << 2)));
			w.Pod(n.lastSeenDay);
			w.Pod(n.lastHuntDay);
			w.Pod(n.kinOf);
			w.Pod(static_cast<std::uint32_t>(n.memories.size()));
			for (const auto& m : n.memories) {
				w.Pod(m.day);
				w.Pod(m.kind);
				w.Pod(m.other);
				w.Str(m.place);
			}
		}
		for (const auto& f : _factions) {
			w.Pod(f.strength);
			w.Pod(f.wealth);
			w.Pod(f.influence);
			w.Pod(f.relations);
			w.Pod(f.playerStanding);
			w.Pod(f.leader);
			w.Pod(f.killsByPlayer);
			w.Pod(static_cast<std::uint8_t>(f.destroyed));
			w.Pod(static_cast<std::uint32_t>(f.captains.size()));
			for (NpcId c : f.captains) w.Pod(c);
			w.Pod(static_cast<std::uint32_t>(f.lieutenants.size()));
			for (NpcId l : f.lieutenants) w.Pod(l);
		}
		for (const auto& s : _sampleBases) {
			w.Pod(static_cast<std::uint32_t>(s.size()));
			for (auto b : s) w.Pod(b);
		}
		w.Pod(static_cast<std::uint32_t>(_chronicle.size()));
		for (const auto& c : _chronicle) {
			w.Pod(c.day);
			w.Pod(c.kind);
			w.Str(c.text);
		}
		w.Pod(static_cast<std::uint32_t>(_leads.size()));
		for (const auto& l : _leads) {
			w.Pod(l.target);
			w.Pod(l.informant);
			w.Pod(l.hold);
			w.Pod(static_cast<std::uint8_t>(l.trap));
			w.Pod(l.expiresDay);
		}
		return std::move(w.buf);
	}

	bool World::Load(std::span<const std::uint8_t> data)
	{
		Reader r(data);
		std::uint32_t magic{}, version{};
		if (!r.Pod(magic) || magic != 0x44554642 || !r.Pod(version) || version > kSaveVersion) return false;

		World fresh(_settings);
		for (int i = 0; i < 4; ++i) r.Pod(fresh._rng.State()[i]);
		r.Pod(fresh._nextId);
		r.Pod(fresh._lastDay);
		r.Pod(fresh._lastAmbushDay);

		std::uint32_t count{};
		if (!r.Count(count, 1u << 20)) return false;
		fresh._npcs.clear();
		fresh._npcs.reserve(count);
		for (std::uint32_t i = 0; i < count && r.Ok(); ++i) {
			Npc n;
			std::uint8_t female{}, flags{};
			r.Pod(n.id);
			r.Pod(n.refId);
			r.Pod(n.baseId);
			r.Pod(n.sourceRef);
			r.Str(n.first);
			r.Str(n.surname);
			r.Str(n.epithet);
			r.Pod(n.culture);
			r.Pod(female);
			r.Pod(n.temper);
			r.Pod(n.traits);
			r.Str(n.trade);
			r.Str(n.faith);
			r.Str(n.backstory);
			r.Pod(n.faction);
			r.Pod(n.rank);
			r.Pod(n.home);
			r.Pod(n.status);
			r.Pod(n.hatred);
			r.Pod(n.fear);
			r.Pod(n.respect);
			r.Pod(n.edges);
			r.Pod(n.injuries);
			r.Pod(n.level);
			r.Pod(n.encounters);
			r.Pod(n.cheatedDeath);
			r.Pod(flags);
			r.Pod(n.lastSeenDay);
			r.Pod(n.lastHuntDay);
			r.Pod(n.kinOf);
			n.female = female != 0;
			n.nemesis = flags & 1;
			n.edgesKnown = flags & 2;
			n.secretlyAlive = flags & 4;
			std::uint32_t mc{};
			if (!r.Count(mc, 1024)) return false;
			for (std::uint32_t j = 0; j < mc && r.Ok(); ++j) {
				Memory m;
				r.Pod(m.day);
				r.Pod(m.kind);
				r.Pod(m.other);
				r.Str(m.place);
				n.memories.push_back(std::move(m));
			}
			// Enum sanity: corrupted data shouldn't index out of bounds later.
			if (n.culture >= Culture::Count || n.temper >= Temperament::Count || n.rank >= Rank::Count ||
				(n.faction >= FactionId::Count && n.faction != FactionId::None) || n.id != i + 1) {
				return false;
			}
			fresh._npcs.push_back(std::move(n));
		}
		for (auto& f : fresh._factions) {
			std::uint8_t destroyed{};
			r.Pod(f.strength);
			r.Pod(f.wealth);
			r.Pod(f.influence);
			r.Pod(f.relations);
			r.Pod(f.playerStanding);
			r.Pod(f.leader);
			r.Pod(f.killsByPlayer);
			r.Pod(destroyed);
			f.destroyed = destroyed != 0;
			std::uint32_t n{};
			if (!r.Count(n, 4096)) return false;
			f.captains.resize(n);
			for (auto& c : f.captains) r.Pod(c);
			if (!r.Count(n, 4096)) return false;
			f.lieutenants.resize(n);
			for (auto& l : f.lieutenants) r.Pod(l);
		}
		for (auto& s : fresh._sampleBases) {
			std::uint32_t n{};
			if (!r.Count(n, 1024)) return false;
			s.resize(n);
			for (auto& b : s) r.Pod(b);
		}
		if (!r.Count(count, 1u << 16)) return false;
		fresh._chronicle.clear();
		for (std::uint32_t i = 0; i < count && r.Ok(); ++i) {
			ChronicleEntry c;
			r.Pod(c.day);
			r.Pod(c.kind);
			r.Str(c.text);
			fresh._chronicle.push_back(std::move(c));
		}
		if (!r.Count(count, 1u << 16)) return false;
		fresh._leads.clear();
		for (std::uint32_t i = 0; i < count && r.Ok(); ++i) {
			Lead l;
			std::uint8_t trap{};
			r.Pod(l.target);
			r.Pod(l.informant);
			r.Pod(l.hold);
			r.Pod(trap);
			r.Pod(l.expiresDay);
			l.trap = trap != 0;
			fresh._leads.push_back(l);
		}
		if (!r.Ok()) return false;

		// Validate cross references.
		const auto valid = [&](NpcId id) { return id == kNoNpc || id <= fresh._npcs.size(); };
		for (auto& f : fresh._factions) {
			if (!valid(f.leader)) return false;
			for (NpcId c : f.captains) if (!valid(c) || c == kNoNpc) return false;
			for (NpcId l : f.lieutenants) if (!valid(l) || l == kNoNpc) return false;
		}
		for (const auto& l : fresh._leads) {
			if (!valid(l.target) || !valid(l.informant)) return false;
		}
		fresh._nextId = static_cast<NpcId>(fresh._npcs.size() + 1);

		fresh._byRef.clear();
		for (const auto& n : fresh._npcs) {
			if (n.refId) fresh._byRef[n.refId] = n.id;
		}
		*this = std::move(fresh);
		return true;
	}

	void World::RemapForms(const std::function<std::uint32_t(std::uint32_t)>& resolveBase, const std::function<std::uint32_t(std::uint32_t)>& resolveRef)
	{
		_byRef.clear();
		for (auto& n : _npcs) {
			if (n.baseId) n.baseId = resolveBase(n.baseId);
			if (n.refId) n.refId = resolveRef(n.refId);
			if (n.sourceRef) n.sourceRef = resolveRef(n.sourceRef);
			if (n.refId) _byRef[n.refId] = n.id;
		}
		for (auto& s : _sampleBases) {
			for (auto& b : s) b = resolveBase(b);
			std::erase(s, 0u);
		}
	}
}

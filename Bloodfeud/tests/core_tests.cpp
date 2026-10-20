// Bloodfeud core tests. Plain asserts so they build anywhere with no deps.
#include "bloodfeud/Text.hpp"
#include "bloodfeud/World.hpp"

#include <cstdio>
#include <cstdlib>
#include <set>

using namespace bf;

static int g_fail = 0;
#define CHECK(cond)                                                              \
	do {                                                                         \
		if (!(cond)) {                                                           \
			std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);        \
			++g_fail;                                                            \
		}                                                                        \
	} while (0)

static ActorInfo Bandit(std::uint32_t ref, Hold h = Hold::Whiterun, bool boss = false, bool female = false)
{
	ActorInfo a;
	a.refId = ref;
	a.baseId = 0x0001000 + (ref & 0xF);
	a.group = GameGroup::Bandit;
	a.raceName = "Nord";
	a.female = female;
	a.level = 12;
	a.hold = h;
	a.place = "Embershard Mine";
	a.boss = boss;
	return a;
}

// Invariants that must hold after any sequence of events.
static void CheckInvariants(const World& w)
{
	std::set<NpcId> seen;
	for (std::size_t i = 0; i < kFactionCount; ++i) {
		const auto& f = w.GetFaction(static_cast<FactionId>(i));
		auto ok = [&](NpcId id, Rank r) {
			const Npc* n = w.Get(id);
			CHECK(n != nullptr);
			if (!n) return;
			CHECK(n->Alive());
			CHECK(n->faction == f.id);
			CHECK(n->rank == r);
			CHECK(seen.insert(id).second);  // nobody holds two posts
		};
		if (f.leader) ok(f.leader, Rank::Warchief);
		for (NpcId c : f.captains) ok(c, Rank::Captain);
		for (NpcId l : f.lieutenants) ok(l, Rank::Lieutenant);
		for (auto v : f.influence) CHECK(v >= 0 && v <= 100);
		CHECK(f.strength >= 0 && f.strength <= 1000);
	}
	for (std::size_t id = 1; id <= w.NpcCount(); ++id) {
		const Npc* n = w.Get(static_cast<NpcId>(id));
		CHECK(n->hatred >= 0 && n->hatred <= 100);
		CHECK(n->fear >= 0 && n->fear <= 100);
		CHECK(!n->first.empty());
		if (n->refId) CHECK(w.FindByRef(n->refId) == n->id);
		if (!n->Alive()) CHECK(!n->nemesis || n->status == Status::Dead);
	}
	CHECK(static_cast<int>(w.Chronicle().size()) <= std::max(50, 400));
}

static void TestIdentity()
{
	std::puts("identity");
	World w;
	const NpcId a = w.Register(Bandit(0xFF000001), 1);
	CHECK(a != kNoNpc);
	CHECK(w.Register(Bandit(0xFF000001), 2) == a);  // same ref → same person
	const Npc* n = w.Get(a);
	CHECK(n && !n->first.empty() && !n->trade.empty() && !n->backstory.empty());
	CHECK(n->faction == FactionId::Greymane);  // Whiterun is Greymane country
	CHECK(w.Register(Bandit(0xFF000002, Hold::TheRift), 1) != a);
	CHECK(w.Get(w.FindByRef(0xFF000002))->faction == FactionId::AshenHand);

	ActorInfo fw = Bandit(0xFF000003, Hold::TheReach);
	fw.group = GameGroup::Forsworn;
	fw.raceName = "Breton";
	const Npc* f = w.Get(w.Register(fw, 1));
	CHECK(f->faction == FactionId::Forsworn && f->culture == Culture::Reachman);
	CheckInvariants(w);
}

static void TestBossBindsOfficer()
{
	std::puts("boss binds to faction officer");
	World w;
	const auto& g = w.GetFaction(FactionId::Greymane);
	// Find an officer's gender so the binding can match.
	const Npc* cap = w.Get(g.captains.front());
	ActorInfo b = Bandit(0xFF0000AA, cap->home, true, cap->female);
	const NpcId id = w.Register(b, 3);
	const Npc* n = w.Get(id);
	CHECK(n->rank >= Rank::Lieutenant);
	CHECK(n->refId == 0xFF0000AA);
	CheckInvariants(w);
}

static void TestNemesisFromEscapes()
{
	std::puts("escapes build a nemesis");
	World w;
	const NpcId id = w.Register(Bandit(0xFF000010), 1);
	for (int fight = 0; fight < 4 && !w.Get(id)->nemesis; ++fight) {
		w.OnEngaged(id, 1 + fight, "Embershard Mine");
		for (int i = 0; i < 5; ++i) w.OnWounded(id, DamageType::Fire, 20);
		w.OnSurvived(id, 1 + fight, "Embershard Mine");
	}
	const Npc* n = w.Get(id);
	CHECK(n->nemesis);
	CHECK(n->encounters >= 2);
	CHECK(!n->memories.empty());
	CHECK(w.Nemeses().size() == 1);
	bool oath = false;
	for (auto& c : w.Chronicle()) oath |= c.text.find("blood-oath") != std::string::npos;
	CHECK(oath);
	CHECK(!w.DrainNotices().empty());
	CheckInvariants(w);
}

static void TestCheatDeathAndReturn()
{
	std::puts("cheat death, then return for revenge");
	Settings s;
	s.cheatDeathChance = 10.0f;  // guaranteed
	s.ambushChancePerHour = 1.0f;
	World w(s);
	const NpcId id = w.Register(Bandit(0xFF000020), 1);
	w.OnEngaged(id, 1, "Embershard Mine");
	w.OnWounded(id, DamageType::Fire, 50);
	CHECK(w.OnKilledByPlayer(id, 1, "Embershard Mine") == DeathResult::CheatedDeath);
	const Npc* n = w.Get(id);
	CHECK(n->Alive() && n->secretlyAlive && n->cheatedDeath == 1);
	CHECK(n->injuries & kBurnScars);
	CHECK(n->refId == 0 && w.FindByRef(0xFF000020) == kNoNpc);
	CHECK(n->nemesis);  // +40 hatred on top of the fight
	// The oath is hidden until they return.
	for (auto& c : w.Chronicle()) CHECK(c.text.find(n->first) == std::string::npos || c.text.find("blood-oath") == std::string::npos);
	w.DrainNotices();

	std::optional<AmbushPlan> plan;
	for (int h = 0; h < 48 && !plan; ++h) plan = w.TickHours(3.0f + h / 24.0f, 1.0f, 20, Hold::Whiterun, true, true);
	CHECK(plan.has_value());
	if (plan) {
		CHECK(plan->leader == id);
		CHECK(!plan->taunt.empty());
		CHECK(!w.Get(id)->secretlyAlive);
		bool returned = false;
		for (auto& t : w.DrainNotices()) returned |= t.find("returned from the grave") != std::string::npos;
		CHECK(returned);
		w.OnAmbushSpawned(id, 0xFF000099, 4);
		CHECK(w.FindByRef(0xFF000099) == id);
	}
	CheckInvariants(w);
}

static void TestKinVengeance()
{
	std::puts("kin vengeance");
	Settings s;
	s.cheatDeathChance = 0.0f;
	s.kinVengeanceChance = 10.0f;
	World w(s);
	const NpcId id = w.Register(Bandit(0xFF000030), 1);
	const std::string surname = w.Get(id)->surname;
	const std::string first = w.Get(id)->first;
	w.OnEngaged(id, 1, "Halted Stream Camp");
	CHECK(w.OnKilledByPlayer(id, 1, "Halted Stream Camp") == DeathResult::Died);
	{
		// Killing a captain lowers the faction's strength, and so the road danger.
		World w2(s);
		auto& gm = w2.GetFaction(FactionId::Greymane);
		const NpcId cap = gm.captains.front();
		const Hold home = w2.Get(cap)->home;
		const float before = w2.RoadDanger(home);
		w2.OnKilledByPlayer(cap, 1, "Camp");
		CHECK(w2.RoadDanger(home) < before);
		bool slain = false;
		for (auto& e : w2.DrainEvents()) slain |= e.kind == WorldEvent::Kind::OfficerSlain;
		CHECK(slain);
	}
	CHECK(w.Get(id)->status == Status::Dead);
	const auto nem = w.Nemeses();
	CHECK(nem.size() == 1);
	if (!nem.empty()) {
		const Npc* k = w.Get(nem[0]);
		CHECK(k->kinOf == id);
		CHECK(k->surname == surname || k->surname.find(first) == 0);
		CHECK(k->memories.back().kind == MemoryKind::KinSlain);
		CHECK(k->baseId == w.Get(id)->baseId);
	}
	CheckInvariants(w);
}

static void TestInterrogationAndLead()
{
	std::puts("yield, interrogate, follow the lead");
	World w;
	w.GetSettings().yieldChanceMult = 100.0f;
	const NpcId grunt = w.Register(Bandit(0xFF000040), 1);
	w.OnEngaged(grunt, 1, "Bleak Falls Barrow");
	CHECK(!w.RollYield(grunt, 0.9f));  // too healthy
	CHECK(w.RollYield(grunt, 0.1f));
	CHECK(!w.RollYield(grunt, 0.1f));  // only one roll per fight
	const Intel intel = w.Interrogate(grunt, 1);
	CHECK(intel.target != kNoNpc);
	CHECK(!intel.text.empty());
	CHECK(intel.hold != Hold::Unknown);
	CHECK(w.Leads().size() == 1);
	w.Spare(grunt, 1, "Bleak Falls Barrow");
	CHECK(w.Get(grunt)->memories.size() >= 3);

	std::optional<AmbushPlan> plan;
	for (int h = 0; h < 4 && !plan; ++h) plan = w.TickHours(2.0f + h / 24.0f, 1.0f, 15, intel.hold, true, true);
	CHECK(plan.has_value());
	if (plan) {
		CHECK(plan->fromLead);
		CHECK(plan->leader == intel.target);
		CHECK(plan->trap == !intel.truthful);
	}
	CHECK(w.Leads().empty());
	CheckInvariants(w);
}

static void TestParley()
{
	std::puts("parley");
	int settled = 0;
	int burned = 0;
	for (std::uint64_t seed = 1; seed <= 200; ++seed) {
		World w({}, seed);
		const NpcId id = w.Register(Bandit(0xFF000050), 1);
		Npc* n = w.Get(id);
		n->hatred = 70;
		n->nemesis = true;
		const int cost = w.ParleyCost(id);
		CHECK(cost > 0 && cost % 50 == 0);
		if (w.Parley(id, cost, 5)) {
			++settled;
			CHECK(n->status == Status::Reconciled && !n->nemesis && n->hatred == 0);
		} else {
			++burned;
			CHECK(n->hatred == 80);
		}
		CHECK(!w.Parley(kNoNpc, 100, 5));
	}
	std::printf("    settled %d / burned %d\n", settled, burned);
	CHECK(settled > 20 && burned > 20);  // depends on temperament, both outcomes common
}

static void TestFactionYear()
{
	std::puts("a year of faction war");
	World w({}, 7);
	w.TickDay(1);
	for (std::uint32_t d = 2; d <= 365; ++d) {
		w.TickDay(d);
		if (d % 30 == 0) CheckInvariants(w);
	}
	int routs = 0, deaths = 0, rumors = 0, life = 0;
	for (auto& c : w.Chronicle()) {
		routs += c.text.find("routed") != std::string::npos;
		deaths += c.text.find("among the dead") != std::string::npos;
		rumors += c.kind == ChronicleKind::Rumor;
		life += c.kind == ChronicleKind::Life;
	}
	std::printf("    chronicle %zu entries: %d skirmishes, %d officer deaths, %d territory changes, %d life events, %zu npcs\n", w.Chronicle().size(), routs, deaths, rumors, life, w.NpcCount());
	CHECK(routs > 10);
	CHECK(deaths > 0);
	for (std::size_t i = 0; i < kFactionCount; ++i) {
		const auto& f = w.GetFaction(static_cast<FactionId>(i));
		if (!f.destroyed) CHECK(f.leader != kNoNpc);
	}
	std::puts(w.FactionReport(FactionId::Greymane).c_str());
	// Event feed + road danger for other mods.
	const auto events = w.DrainEvents();
	CHECK(!events.empty());
	CHECK(w.DrainEvents().empty());
	for (std::size_t h = 0; h < kHoldCount; ++h) {
		const float d = w.RoadDanger(static_cast<Hold>(h));
		CHECK(d >= 0.0f && d <= 1.0f);
	}
	CHECK(w.RoadDanger(Hold::Unknown) == 0.0f);
	CheckInvariants(w);
}

static void TestSaveLoad()
{
	std::puts("save / load round trip");
	World w({}, 99);
	w.TickDay(1);
	for (std::uint32_t r = 1; r <= 40; ++r) {
		const NpcId id = w.Register(Bandit(0xFF100000 + r, static_cast<Hold>(r % kHoldCount), r % 7 == 0), r);
		w.OnEngaged(id, r, "Somewhere");
		w.OnWounded(id, DamageType::Physical, 30);
		if (r % 3 == 0) w.OnKilledByPlayer(id, r, "Somewhere");
		else w.OnSurvived(id, r, "Somewhere");
		w.TickDay(r + 1);
	}
	auto bytes = w.Save();
	World loaded;
	CHECK(loaded.Load(bytes));
	CHECK(loaded.Save() == bytes);
	CHECK(loaded.NpcCount() == w.NpcCount());
	CHECK(loaded.Chronicle().size() == w.Chronicle().size());
	CheckInvariants(loaded);

	// Determinism: both worlds roll the same future.
	w.TickDay(60);
	loaded.TickDay(60);
	CHECK(w.Save() == loaded.Save());

	// Corrupt and truncated data must be rejected without crashing.
	World bad;
	CHECK(!bad.Load({}));
	auto trunc = bytes;
	trunc.resize(trunc.size() / 2);
	CHECK(!bad.Load(trunc));
	auto junk = bytes;
	for (std::size_t i = 40; i < junk.size(); i += 7) junk[i] ^= 0x5A;
	(void)bad.Load(junk);  // may or may not parse; must not crash
	CheckInvariants(bad);

	// Load-order remap drops unresolvable forms.
	loaded.RemapForms([](std::uint32_t b) { return b == 0x0001003 ? 0u : b + 0x01000000; }, [](std::uint32_t) { return 0u; });
	for (std::size_t i = 1; i <= loaded.NpcCount(); ++i) CHECK(loaded.Get(static_cast<NpcId>(i))->refId == 0);
}

static void TestStats()
{
	std::puts("stat mods");
	World w;
	Npc n;
	n.rank = Rank::Captain;
	n.edges = kFireproof | kGlassJaw;
	n.injuries = kLimp;
	n.nemesis = true;
	n.level = 10;
	const auto mods = w.StatModsFor(n, 30, 20);
	float fire = 0, speed = 0, health = 0;
	for (auto& m : mods) {
		if (m.stat == Stat::ResistFire) fire += m.delta;
		if (m.stat == Stat::SpeedMult) speed += m.delta;
		if (m.stat == Stat::Health) health += m.delta;
	}
	CHECK(fire == 50.0f);
	CHECK(speed == -10.0f);
	CHECK(health > 50.0f);  // rank + level catch-up - glass jaw
}

static void TestText()
{
	std::puts("text helpers");
	CHECK(text::HoldFromLocationName("Whiterun Hold") == Hold::Whiterun);
	CHECK(text::HoldFromLocationName("The Rift") == Hold::TheRift);
	CHECK(text::HoldFromLocationName("Markarth") == Hold::TheReach);
	CHECK(text::HoldFromLocationName("Bleak Falls Barrow") == Hold::Unknown);
	CHECK(text::CultureFromRaceName("DarkElfRace", GameGroup::Bandit) == Culture::Dunmer);
	CHECK(text::CultureFromRaceName("Dark Elf", GameGroup::Bandit) == Culture::Dunmer);
	CHECK(text::CultureFromRaceName("Nord", GameGroup::Forsworn) == Culture::Reachman);
	CHECK(text::Fmt("{0} and {1}", { "a", "b" }) == "a and b");
	CHECK(text::Fmt("{9} stays", { "a" }) == " stays");
	CHECK(text::EdgeList(kFireproof | kSlow, true) == "Fireproof");
	CHECK(text::EdgeList(kFireproof | kSlow, false) == "Slow");
}

static void TestFuzz()
{
	std::puts("fuzz: 30k random events");
	World w({}, 1234);
	Rng r(42);
	std::vector<std::uint32_t> refs;
	std::uint32_t nextRef = 0xFF200000;
	float day = 1.0f;
	w.TickDay(1);
	for (int step = 0; step < 30000; ++step) {
		const auto d = static_cast<std::uint32_t>(day);
		switch (r.Below(12)) {
		case 0:
		case 1:
			{
				ActorInfo a = Bandit(nextRef++, static_cast<Hold>(r.Below(kHoldCount)), r.Chance(0.1f), r.Chance(0.3f));
				a.group = static_cast<GameGroup>(r.Below(7));
				refs.push_back(a.refId);
				w.Register(a, d);
				break;
			}
		case 2:
		case 3:
			if (!refs.empty()) {
				const NpcId id = w.FindByRef(refs[r.Below(static_cast<std::uint32_t>(refs.size()))]);
				w.OnEngaged(id, d, "Place");
				w.OnWounded(id, static_cast<DamageType>(r.Below(5)), 10);
			}
			break;
		case 4:
			if (!refs.empty()) w.OnSurvived(w.FindByRef(refs[r.Below(static_cast<std::uint32_t>(refs.size()))]), d, "Place");
			break;
		case 5:
			if (!refs.empty()) w.OnKilledByPlayer(w.FindByRef(refs[r.Below(static_cast<std::uint32_t>(refs.size()))]), d, "Place");
			break;
		case 6:
			if (!refs.empty()) {
				const NpcId id = w.FindByRef(refs[r.Below(static_cast<std::uint32_t>(refs.size()))]);
				if (w.RollYield(id, 0.1f)) {
					w.Interrogate(id, d);
					if (r.Chance(0.5f)) w.Spare(id, d, "Place");
					else w.Execute(id, d, "Place");
				}
			}
			break;
		case 7:
			{
				auto plan = w.TickHours(day, 1.0f, 20, static_cast<Hold>(r.Below(kHoldCount)), true, true);
				if (plan) {
					const std::uint32_t ref = nextRef++;
					refs.push_back(ref);
					w.OnAmbushSpawned(plan->leader, ref, d);
				}
				break;
			}
		case 8:
			day += 0.25f;
			w.TickDay(static_cast<std::uint32_t>(day));
			break;
		case 9:
			{
				auto n = w.Nemeses();
				if (!n.empty()) w.Parley(n[0], r.Range(0, 3000), d);
				break;
			}
		case 10:
			if (!refs.empty()) w.Unbind(w.FindByRef(refs[r.Below(static_cast<std::uint32_t>(refs.size()))]));
			break;
		default:
			for (auto id : w.KnownEnemies()) (void)w.Dossier(id);
			break;
		}
		if (step % 5000 == 0) {
			CheckInvariants(w);
			World copy;
			CHECK(copy.Load(w.Save()));
		}
	}
	CheckInvariants(w);
	std::printf("    %zu npcs, %zu nemeses, %zu chronicle entries, day %.0f\n", w.NpcCount(), w.Nemeses().size(), w.Chronicle().size(), day);
}

int main()
{
	TestText();
	TestIdentity();
	TestBossBindsOfficer();
	TestNemesisFromEscapes();
	TestCheatDeathAndReturn();
	TestKinVengeance();
	TestInterrogationAndLead();
	TestParley();
	TestStats();
	TestSaveLoad();
	TestFactionYear();
	TestFuzz();
	std::printf(g_fail ? "\n%d FAILURES\n" : "\nall tests passed\n", g_fail);
	return g_fail ? 1 : 0;
}

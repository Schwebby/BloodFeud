// Ledger economy tests: plain asserts, no dependencies.
#include "ledger/Economy.hpp"

#include <cmath>
#include <cstdio>

using namespace ledger;

static int g_fail = 0;
#define CHECK(c)                                                         \
	do {                                                                 \
		if (!(c)) {                                                      \
			std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #c);   \
			++g_fail;                                                    \
		}                                                                \
	} while (0)

static std::uint32_t MonthOf(std::uint32_t day) { return ((day - 1) / 30) % 12; }

static void Run(Economy& e, std::uint32_t from, std::uint32_t to)
{
	for (std::uint32_t d = from; d <= to; ++d) e.TickDay(d, MonthOf(d));
}

static void CheckSane(const Economy& e)
{
	for (std::size_t h = 0; h < kHolds; ++h) {
		const auto& m = e.GetMarket(static_cast<Hold>(h));
		CHECK(m.prosperity >= 0.0f && m.prosperity <= 1.0f);
		CHECK(m.danger >= 0.0f && m.danger <= 1.0f);
		for (std::size_t g = 0; g < kGoods; ++g) {
			const float p = e.PriceMult(static_cast<Hold>(h), static_cast<Good>(g));
			CHECK(std::isfinite(p) && p >= 0.5f && p <= 2.5f);
			CHECK(m.stock[g] >= 0.0f && std::isfinite(m.stock[g]));
		}
		const float gold = e.MerchantGoldMult(static_cast<Hold>(h));
		CHECK(gold >= 0.45f && gold <= 1.6f);
	}
	for (const auto& b : e.Businesses()) {
		CHECK(b.condition >= 0.0f && b.condition <= 1.0f);
		CHECK(b.playerShares <= Economy::kSharesPerBusiness);
	}
}

static void TestQuietYear()
{
	std::puts("a quiet year stays in a sensible range");
	Economy e;
	e.TickDay(1, 0);
	int extremes = 0, total = 0;
	for (std::uint32_t d = 2; d <= 365; ++d) {
		e.TickDay(d, MonthOf(d));
		for (std::size_t h = 0; h < kHolds; ++h) {
			for (std::size_t g = 0; g < kGoods; ++g, ++total) {
				const float p = e.PriceMult(static_cast<Hold>(h), static_cast<Good>(g));
				extremes += (p <= 0.51f || p >= 2.49f);
			}
		}
	}
	std::printf("    %.1f%% of price readings at the limits\n", 100.0 * extremes / total);
	CHECK(extremes < total / 20);
	for (std::size_t h = 0; h < kHolds; ++h) {
		const float g = e.MerchantGoldMult(static_cast<Hold>(h));
		CHECK(g > 0.85f && g < 1.3f);  // ordinary times, ordinary purses
	}
	CheckSane(e);
}

static void TestSeasons()
{
	std::puts("food is dearer in winter than at harvest");
	Economy e;
	e.TickDay(1, 0);
	Run(e, 2, 270);  // end of Frostfall (harvest)
	float harvest = 0.0f;
	for (std::size_t h = 0; h < kHolds; ++h) harvest += e.PriceMult(static_cast<Hold>(h), Good::Food);
	Run(e, 271, 365);  // into deep winter
	float winter = 0.0f;
	for (std::size_t h = 0; h < kHolds; ++h) winter += e.PriceMult(static_cast<Hold>(h), Good::Food);
	std::printf("    avg food: harvest %.2f, winter %.2f\n", harvest / kHolds, winter / kHolds);
	CHECK(winter > harvest * 1.2f);
}

static void TestDangerChokesTrade()
{
	std::puts("lawless roads cut trade and prosperity");
	Economy calm, lawless;
	calm.TickDay(1, 5);
	lawless.TickDay(1, 5);
	for (std::uint32_t d = 2; d <= 60; ++d) {
		for (std::size_t h = 0; h < kHolds; ++h) {
			calm.SetRoadDanger(static_cast<Hold>(h), 0.2f);
			lawless.SetRoadDanger(static_cast<Hold>(h), h == static_cast<std::size_t>(Hold::Whiterun) ? 0.95f : 0.2f);
		}
		calm.TickDay(d, 5);
		lawless.TickDay(d, 5);
	}
	CHECK(lawless.LinkedToBloodfeud());
	const auto& a = calm.GetMarket(Hold::Whiterun);
	const auto& b = lawless.GetMarket(Hold::Whiterun);
	std::printf("    Whiterun prosperity: calm %.2f, lawless %.2f\n", a.prosperity, b.prosperity);
	CHECK(b.prosperity < a.prosperity - 0.15f);
	CHECK(lawless.MerchantGoldMult(Hold::Whiterun) < calm.MerchantGoldMult(Hold::Whiterun));
	// Whiterun imports ore; with the roads shut it gets dearer.
	CHECK(lawless.PriceMult(Hold::Whiterun, Good::Ore) > calm.PriceMult(Hold::Whiterun, Good::Ore));
	float calmProfit = 0, lawProfit = 0;
	for (auto id : calm.BusinessesIn(Hold::Whiterun)) calmProfit += calm.GetBusiness(id)->avgProfit;
	for (auto id : lawless.BusinessesIn(Hold::Whiterun)) lawProfit += lawless.GetBusiness(id)->avgProfit;
	CHECK(lawProfit < calmProfit);
	CheckSane(lawless);
}

static void TestBattleAndDragon()
{
	std::puts("war raises arms prices; dragons empty markets");
	Economy e;
	e.TickDay(1, 5);
	Run(e, 2, 20);
	const float before = e.PriceMult(Hold::Whiterun, Good::Arms);
	for (int i = 0; i < 3; ++i) e.OnBattle(Hold::Whiterun);
	Run(e, 21, 26);
	const float after = e.PriceMult(Hold::Whiterun, Good::Arms);
	std::printf("    Whiterun arms: %.2f -> %.2f\n", before, after);
	CHECK(after > before * 1.3f);
	bool turmoil = false;
	for (auto& n : e.DrainNotices()) turmoil |= n.find("turmoil") != std::string::npos;
	CHECK(turmoil);

	int damaged = 0;
	for (std::uint64_t seed = 1; seed <= 40; ++seed) {
		Economy d({}, seed);
		d.TickDay(1, 5);
		d.OnDragon(Hold::Whiterun, true);
		for (auto id : d.BusinessesIn(Hold::Whiterun)) damaged += d.GetBusiness(id)->condition < 1.0f;
		CHECK(d.GetMarket(Hold::Whiterun).dragon == 1.0f);
	}
	CHECK(damaged > 10);
	// Seeing a dragon in the wilds worries people less.
	Economy w;
	w.OnDragon(Hold::ThePale, false);
	CHECK(w.GetMarket(Hold::ThePale).dragon < 0.5f);
}

static void TestPlayerFloodsMarket()
{
	std::puts("the player can flood a market, and it recovers");
	Economy e;
	e.TickDay(1, 5);
	Run(e, 2, 20);
	const float before = e.PriceMult(Hold::TheRift, Good::Arms);
	e.OnPlayerTrade(Hold::TheRift, Good::Arms, 150, true, 30);
	const float flooded = e.PriceMult(Hold::TheRift, Good::Arms);
	Run(e, 21, 50);
	const float later = e.PriceMult(Hold::TheRift, Good::Arms);
	std::printf("    Rift arms: %.2f -> flooded %.2f -> a month later %.2f\n", before, flooded, later);
	CHECK(flooded < before * 0.8f);
	CHECK(later > flooded);
	// Buying out a market does the reverse.
	e.OnPlayerTrade(Hold::TheRift, Good::Alchemy, 1000, false, 20);
	CHECK(e.PriceMult(Hold::TheRift, Good::Alchemy) > 1.5f);
	e.OnPlayerTrade(Hold::Unknown, Good::Food, 10, true, 1);  // ignored, no crash
	CheckSane(e);
}

static void TestInvesting()
{
	std::puts("investing: buy, dividends, guards, sell");
	Economy e;
	e.TickDay(1, 5);
	Run(e, 2, 14);
	const auto ids = e.BusinessesIn(Hold::Haafingar);
	CHECK(!ids.empty());
	const std::uint16_t id = ids.front();
	const int quote = e.QuoteBuy(id, 60);
	CHECK(quote > 0);
	CHECK(e.Buy(id, 60, quote - 1) == -1);  // can't afford
	CHECK(e.Buy(id, 60, quote) == quote);
	CHECK(e.GetBusiness(id)->playerShares == 60);
	CHECK(e.QuoteBuy(id, 50) == -1);  // only 40 left
	CHECK(e.PortfolioValue() > 0);

	const int guards = Economy::GuardCost(*e.GetBusiness(id));
	CHECK(e.HireGuards(id, guards) == guards);
	CHECK(e.GetBusiness(id)->guardsUntil > e.Day());

	int paid = 0;
	for (std::uint32_t d = 15; d <= 60; ++d) {
		e.TickDay(d, 5);
		paid += e.CollectDividends();
	}
	std::printf("    %s: paid %d gold over 6 weeks on a %d gold stake\n", e.GetBusiness(id)->name.c_str(), paid, quote);
	CHECK(paid > 0);
	CHECK(e.CollectDividends() == 0);
	bool courier = false;
	for (auto& n : e.DrainNotices()) courier |= n.find("dividends") != std::string::npos;
	CHECK(courier);

	const int proceeds = e.Sell(id, 1000);  // clamps to what you own
	CHECK(proceeds > 0);
	CHECK(e.GetBusiness(id)->playerShares == 0);
	CHECK(e.Sell(id, 5) == 0);
	CHECK(e.HireGuards(id, 100000) == -1);  // no longer the owner
	CHECK(e.Buy(999, 1, 100000) == -1);
	CHECK(e.GetBusiness(0) == nullptr);
}

static void TestRuinAndRebuild()
{
	std::puts("businesses can be ruined and rebuilt");
	Settings s;
	s.raidChance = 1.0f;
	Economy e(s, 3);
	e.TickDay(1, 5);
	for (std::uint32_t d = 2; d <= 40; ++d) {
		e.SetRoadDanger(Hold::ThePale, 1.0f);
		e.TickDay(d, 5);
	}
	bool ruined = false;
	for (auto id : e.BusinessesIn(Hold::ThePale)) ruined |= e.GetBusiness(id)->ruined;
	CHECK(ruined);
	for (auto id : e.BusinessesIn(Hold::ThePale)) {
		if (e.GetBusiness(id)->ruined) CHECK(e.QuoteBuy(id, 1) == -1);
	}
	e.GetSettings().raidChance = 0.0f;
	for (std::uint32_t d = 41; d <= 80; ++d) {
		e.SetRoadDanger(Hold::ThePale, 0.1f);
		e.TickDay(d, 5);
	}
	for (auto id : e.BusinessesIn(Hold::ThePale)) CHECK(!e.GetBusiness(id)->ruined);
	CheckSane(e);
}

static void TestBloodfeudEvents()
{
	std::puts("Bloodfeud events move the economy");
	Economy e;
	e.TickDay(1, 5);
	e.SetRoadDanger(Hold::Falkreath, 0.8f);
	e.OnOfficerSlain(Hold::Falkreath, 2.0f);
	CHECK(std::abs(e.GetMarket(Hold::Falkreath).danger - 0.64f) < 0.001f);
	const float u = e.GetMarket(Hold::Falkreath).unrest;
	e.OnHoldSeized(Hold::Falkreath);
	e.OnSkirmish(Hold::Falkreath, 1.0f);
	CHECK(e.GetMarket(Hold::Falkreath).unrest > u);
	e.OnOfficerSlain(Hold::Unknown, 1.0f);  // ignored
	e.OnMerchantKilled(Hold::Falkreath);
	CHECK(e.GetMarket(Hold::Falkreath).merchantLoss > 0.0f);
	// Without Bloodfeud, killing outlaws makes the roads a little safer.
	Economy solo;
	const float d0 = solo.GetMarket(Hold::TheReach).danger;
	for (int i = 0; i < 10; ++i) solo.OnOutlawKilled(Hold::TheReach);
	CHECK(solo.GetMarket(Hold::TheReach).danger < d0);
	// ...but once linked, Bloodfeud is the source of truth.
	const float d1 = e.GetMarket(Hold::TheReach).danger;
	e.OnOutlawKilled(Hold::TheReach);
	CHECK(e.GetMarket(Hold::TheReach).danger == d1);
}

static void TestSaveLoad()
{
	std::puts("save / load");
	Economy e({}, 77);
	e.TickDay(1, 0);
	Run(e, 2, 50);
	e.Buy(e.BusinessesIn(Hold::Whiterun).front(), 10, 100000);
	e.OnBattle(Hold::Eastmarch);
	e.SetRoadDanger(Hold::TheRift, 0.7f);
	const auto bytes = e.Save();
	Economy l;
	CHECK(l.Load(bytes));
	CHECK(l.Save() == bytes);
	CHECK(l.LinkedToBloodfeud());
	Run(e, 51, 90);
	Run(l, 51, 90);
	CHECK(e.Save() == l.Save());  // deterministic after reload

	Economy bad;
	CHECK(!bad.Load({}));
	auto t = bytes;
	t.resize(t.size() / 3);
	CHECK(!bad.Load(t));
	auto junk = bytes;
	for (std::size_t i = 30; i < junk.size(); i += 5) junk[i] ^= 0xA5;
	(void)bad.Load(junk);
	CheckSane(bad);
}

static void TestReports()
{
	std::puts("reports");
	Economy e;
	e.TickDay(1, 0);
	Run(e, 2, 30);
	const auto r = e.MarketReport(Hold::Whiterun);
	CHECK(r.find("Whiterun Hold") != std::string::npos);
	CHECK(r.find("Food") != std::string::npos && r.find("Luxuries") != std::string::npos);
	std::puts(r.c_str());
	const auto b = e.BusinessReport(1);
	CHECK(b.find("Share price") != std::string::npos);
	CHECK(e.MarketReport(Hold::Unknown) == "No market here.");
	CHECK(HoldFromLocationName("Riften") == Hold::TheRift);
	CHECK(HoldFromLocationName("The Pale") == Hold::ThePale);
	CHECK(HoldFromLocationName("Whiterun Hold") == Hold::Whiterun);
	CHECK(HoldFromLocationName("Bleak Falls Barrow") == Hold::Unknown);
	CHECK(SeasonFromMonth(0) == Season::Winter && SeasonFromMonth(8) == Season::Harvest && SeasonFromMonth(6) == Season::Summer);
}

static void TestFuzz()
{
	std::puts("fuzz: 20k random events");
	Economy e({}, 9);
	Rng r(5);
	std::uint32_t day = 1;
	e.TickDay(day, 0);
	for (int i = 0; i < 20000; ++i) {
		const auto h = static_cast<Hold>(r.Below(kHolds + 1) == kHolds ? 0xFF : r.Below(kHolds));
		const auto g = static_cast<Good>(r.Below(kGoods));
		switch (r.Below(11)) {
		case 0: e.SetRoadDanger(h, r.Unit() * 1.5f - 0.2f); break;
		case 1: e.OnBattle(h); break;
		case 2: e.OnDragon(h, r.Chance(0.5f)); break;
		case 3: e.OnMerchantKilled(h); break;
		case 4: e.OnPlayerTrade(h, g, r.Range(-5, 200), r.Chance(0.5f), r.Range(0, 500)); break;
		case 5: e.Buy(static_cast<std::uint16_t>(r.Below(25)), r.Range(-3, 60), r.Range(0, 20000)); break;
		case 6: e.Sell(static_cast<std::uint16_t>(r.Below(25)), r.Range(-3, 60)); break;
		case 7: e.HireGuards(static_cast<std::uint16_t>(r.Below(25)), 5000); break;
		case 8: ++day; e.TickDay(day, MonthOf(day)); break;
		case 9: e.OnOfficerSlain(h, r.Unit() * 3); e.OnHoldSeized(h); e.OnSkirmish(h, 2); break;
		default: (void)e.CollectDividends(); (void)e.MarketReport(h); break;
		}
		if (i % 4000 == 0) {
			CheckSane(e);
			Economy c;
			CHECK(c.Load(e.Save()));
		}
	}
	CheckSane(e);
	std::printf("    day %u, %zu news entries\n", day, e.News().size());
}

int main()
{
	TestQuietYear();
	TestSeasons();
	TestDangerChokesTrade();
	TestBattleAndDragon();
	TestPlayerFloodsMarket();
	TestInvesting();
	TestRuinAndRebuild();
	TestBloodfeudEvents();
	TestSaveLoad();
	TestReports();
	TestFuzz();
	std::printf(g_fail ? "\n%d FAILURES\n" : "\nall tests passed\n", g_fail);
	return g_fail ? 1 : 0;
}

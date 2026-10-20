#include "ledger/Economy.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

namespace ledger
{
	namespace
	{
		template <class T>
		T Clamp(T v, T lo, T hi) { return std::min(hi, std::max(lo, v)); }

		constexpr std::size_t I(Hold h) { return static_cast<std::size_t>(h); }
		constexpr std::size_t I(Good g) { return static_cast<std::size_t>(g); }

		//                               Food Drink Ore Arms Alch Lumber Pelts Lux
		constexpr float kProd[kHolds][kGoods]{
			{ 22, 6, 0, 8, 4, 4, 4, 12 },     // Haafingar (Solitude: port, imports)
			{ 10, 2, 2, 2, 10, 18, 6, 1 },    // Hjaalmarch (Morthal: timber, marsh herbs)
			{ 12, 2, 24, 4, 3, 4, 6, 3 },     // The Pale (Dawnstar mines)
			{ 6, 1, 6, 1, 8, 2, 6, 3 },       // Winterhold (poor; the College)
			{ 16, 4, 22, 11, 6, 2, 4, 14 },   // The Reach (Markarth silver)
			{ 52, 15, 4, 13, 6, 6, 8, 4 },    // Whiterun (farms, Skyforge)
			{ 26, 8, 12, 9, 5, 6, 8, 6 },     // Eastmarch (Windhelm docks)
			{ 14, 3, 2, 3, 8, 22, 12, 2 },    // Falkreath (lumber, hunting)
			{ 30, 19, 4, 6, 10, 8, 8, 8 },    // The Rift (mead, fish)
		};
		constexpr float kCons[kHolds][kGoods]{
			{ 32, 10, 6, 12, 6, 8, 5, 12 },
			{ 10, 3, 2, 2, 4, 6, 3, 1 },
			{ 12, 3, 6, 3, 3, 6, 4, 2 },
			{ 9, 3, 3, 2, 6, 5, 4, 2 },
			{ 24, 7, 8, 10, 6, 6, 5, 10 },
			{ 34, 10, 8, 12, 8, 10, 7, 6 },
			{ 28, 9, 8, 10, 6, 8, 7, 7 },
			{ 12, 4, 3, 3, 5, 8, 6, 2 },
			{ 28, 10, 6, 8, 9, 8, 7, 9 },
		};
		// Baseline road danger when Bloodfeud isn't feeding us.
		constexpr float kBaseDanger[kHolds]{ 0.25f, 0.3f, 0.3f, 0.35f, 0.45f, 0.25f, 0.3f, 0.35f, 0.3f };

		struct Route
		{
			Hold a, b;
			float capacity;
			bool sea;
		};
		constexpr Route kRoutes[]{
			{ Hold::Haafingar, Hold::Hjaalmarch, 1.0f, false },
			{ Hold::Haafingar, Hold::TheReach, 0.8f, false },
			{ Hold::Hjaalmarch, Hold::ThePale, 0.7f, false },
			{ Hold::Hjaalmarch, Hold::Whiterun, 0.9f, false },
			{ Hold::Hjaalmarch, Hold::TheReach, 0.6f, false },
			{ Hold::ThePale, Hold::Winterhold, 0.6f, false },
			{ Hold::ThePale, Hold::Whiterun, 0.9f, false },
			{ Hold::ThePale, Hold::Eastmarch, 0.7f, false },
			{ Hold::Winterhold, Hold::Eastmarch, 0.8f, false },
			{ Hold::TheReach, Hold::Whiterun, 1.0f, false },
			{ Hold::TheReach, Hold::Falkreath, 0.6f, false },
			{ Hold::Whiterun, Hold::Falkreath, 1.0f, false },
			{ Hold::Whiterun, Hold::Eastmarch, 1.0f, false },
			{ Hold::Whiterun, Hold::TheRift, 0.9f, false },
			{ Hold::Eastmarch, Hold::TheRift, 1.0f, false },
			{ Hold::Falkreath, Hold::TheRift, 0.7f, false },
			{ Hold::Haafingar, Hold::Eastmarch, 0.5f, true },  // East Empire Company ships
			{ Hold::Haafingar, Hold::ThePale, 0.3f, true },
		};

		float SeasonProd(Good g, Season s)
		{
			if (g == Good::Food) return s == Season::Harvest ? 1.4f : s == Season::Winter ? 0.6f : s == Season::Summer ? 1.1f : 0.9f;
			if (g == Good::Pelts) return s == Season::Winter ? 1.2f : 1.0f;
			return 1.0f;
		}
		float SeasonCons(Good g, Season s)
		{
			if (s != Season::Winter) return 1.0f;
			if (g == Good::Lumber) return 1.5f;
			if (g == Good::Pelts) return 1.4f;
			if (g == Good::Drink) return 1.2f;
			return 1.0f;
		}

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
				if (_pos + sizeof(T) > _d.size()) return _ok = false;
				std::memcpy(&v, _d.data() + _pos, sizeof(T));
				_pos += sizeof(T);
				return true;
			}
			bool Str(std::string& s)
			{
				std::uint32_t n{};
				if (!Pod(n) || n > 4096 || _pos + n > _d.size()) return _ok = false;
				s.assign(reinterpret_cast<const char*>(_d.data() + _pos), n);
				_pos += n;
				return true;
			}
			[[nodiscard]] bool Ok() const { return _ok; }

		private:
			std::span<const std::uint8_t> _d;
			std::size_t _pos{ 0 };
			bool _ok{ true };
		};

		std::string Pct(float mult)
		{
			const int p = static_cast<int>(std::lround((mult - 1.0f) * 100.0f));
			return (p > 0 ? "+" : "") + std::to_string(p) + "%";
		}

		std::string Lower(std::string_view s)
		{
			std::string o(s);
			for (auto& c : o) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			return o;
		}
	}

	// =======================================================================
	// helpers

	Season SeasonFromMonth(std::uint32_t m)
	{
		switch (m % 12) {
		case 2: case 3: case 4: return Season::Spring;      // First Seed, Rain's Hand, Second Seed
		case 5: case 6: return Season::Summer;              // Midyear, Sun's Height
		case 7: case 8: case 9: return Season::Harvest;     // Last Seed, Hearthfire, Frostfall
		default: return Season::Winter;                     // Sun's Dusk .. Sun's Dawn
		}
	}

	std::string_view HoldName(Hold h)
	{
		static constexpr std::array<std::string_view, kHolds> n{ "Haafingar", "Hjaalmarch", "The Pale", "Winterhold", "The Reach", "Whiterun Hold", "Eastmarch", "Falkreath Hold", "The Rift" };
		return h == Hold::Unknown ? std::string_view{ "the wilds" } : n[I(h)];
	}

	std::string_view GoodName(Good g)
	{
		static constexpr std::array<std::string_view, kGoods> n{ "Food", "Drink", "Ore", "Arms", "Alchemy", "Lumber", "Pelts", "Luxuries" };
		return n[I(g)];
	}

	Hold HoldFromLocationName(std::string_view name)
	{
		std::string s = Lower(name);
		std::erase(s, ' ');
		auto has = [&](const char* w) { return s.find(w) != std::string::npos; };
		if (has("haafingar") || has("solitude")) return Hold::Haafingar;
		if (has("hjaalmarch") || has("morthal")) return Hold::Hjaalmarch;
		if (has("thepale") || has("palehold") || has("dawnstar")) return Hold::ThePale;
		if (has("winterhold")) return Hold::Winterhold;
		if (has("thereach") || has("reachhold") || has("markarth")) return Hold::TheReach;
		if (has("whiterun")) return Hold::Whiterun;
		if (has("eastmarch") || has("windhelm")) return Hold::Eastmarch;
		if (has("falkreath")) return Hold::Falkreath;
		if (has("therift") || has("rifthold") || has("riften")) return Hold::TheRift;
		return Hold::Unknown;
	}

	float Economy::BasePrice(Good g)
	{
		static constexpr std::array<float, kGoods> p{ 4, 10, 12, 45, 25, 3, 15, 70 };
		return p[I(g)];
	}

	float Economy::Target(Hold h, Good g) { return std::max(10.0f, kCons[I(h)][I(g)] * 7.0f); }

	// =======================================================================

	Economy::Economy(Settings s, std::uint64_t seed) : _settings(s), _rng(seed)
	{
		for (std::size_t h = 0; h < kHolds; ++h) {
			auto& m = _markets[h];
			m.baseDanger = m.danger = kBaseDanger[h];
			for (std::size_t g = 0; g < kGoods; ++g) {
				m.stock[g] = Target(static_cast<Hold>(h), static_cast<Good>(g)) * (0.9f + 0.2f * _rng.Unit());
				m.stockWeekAgo[g] = m.stock[g];
				m.priceWeekAgo[g] = 1.0f;
			}
		}
		InitBusinesses();
		ComputeNorm();
	}

	// Scale each good's regional production so Skyrim as a whole roughly feeds
	// itself at normal road danger; regional surpluses and shortages remain and
	// are what trade (and the player) move around.
	void Economy::ComputeNorm()
	{
		for (std::size_t g = 0; g < kGoods; ++g) {
			float prod = 0.0f;
			float cons = 0.0f;
			for (std::size_t h = 0; h < kHolds; ++h) {
				const float dis = 1.0f - 0.6f * kBaseDanger[h];
				prod += kProd[h][g] * dis;
				cons += kCons[h][g];
			}
			float biz = 0.0f;
			for (const auto& b : _businesses) {
				if (I(b.good) == g) biz += b.capacity * (1.0f - 0.5f * kBaseDanger[I(b.hold)]);
			}
			_norm[g] = prod > 0.0f ? std::max(0.0f, cons - biz) / prod : 1.0f;
		}
	}

	void Economy::InitBusinesses()
	{
		struct Def
		{
			const char* name;
			Hold hold;
			Good good;
			float cap;
		};
		static constexpr Def defs[]{
			{ "Honningbrew Meadery", Hold::Whiterun, Good::Drink, 6 },
			{ "Pelagia Farm", Hold::Whiterun, Good::Food, 10 },
			{ "Riverwood Mill", Hold::Whiterun, Good::Lumber, 7 },
			{ "Warmaiden's", Hold::Whiterun, Good::Arms, 2 },
			{ "Black-Briar Meadery", Hold::TheRift, Good::Drink, 8 },
			{ "Riften Fishery", Hold::TheRift, Good::Food, 7 },
			{ "Elgrim's Elixirs", Hold::TheRift, Good::Alchemy, 2 },
			{ "Quicksilver Mine", Hold::ThePale, Good::Ore, 6 },
			{ "Iron-Breaker Mine", Hold::ThePale, Good::Ore, 6 },
			{ "Cidhna Mine", Hold::TheReach, Good::Ore, 8 },
			{ "Arnleif and Sons Trading Company", Hold::TheReach, Good::Luxury, 2 },
			{ "Whistling Mine", Hold::Winterhold, Good::Ore, 4 },
			{ "Morthal Lumber Mill", Hold::Hjaalmarch, Good::Lumber, 6 },
			{ "Half-Moon Mill", Hold::Falkreath, Good::Lumber, 6 },
			{ "Corpselight Farm", Hold::Falkreath, Good::Food, 5 },
			{ "Grave Concoctions", Hold::Falkreath, Good::Alchemy, 2 },
			{ "Hollyfrost Farm", Hold::Eastmarch, Good::Food, 6 },
			{ "Windhelm Docks", Hold::Eastmarch, Good::Luxury, 2 },
			{ "East Empire Company Warehouse", Hold::Haafingar, Good::Luxury, 3 },
			{ "Solitude Sawmill", Hold::Haafingar, Good::Lumber, 5 },
		};
		_businesses.clear();
		std::uint16_t id = 1;
		for (const auto& d : defs) {
			Business b;
			b.id = id++;
			b.name = d.name;
			b.hold = d.hold;
			b.good = d.good;
			b.capacity = d.cap;
			b.avgProfit = d.cap * BasePrice(d.good) * 0.3f;
			_businesses.push_back(std::move(b));
		}
	}

	// =======================================================================
	// time

	void Economy::TickDay(std::uint32_t day, std::uint32_t month)
	{
		_month = month % 12;
		if (_day == 0) {
			_day = day;
			return;
		}
		if (day <= _day) return;
		const std::uint32_t from = std::max(_day + 1, day > 30 ? day - 30 : 1u);
		for (std::uint32_t d = from; d <= day; ++d) SimulateDay(d, SeasonFromMonth(_month));
		_day = day;
	}

	void Economy::SimulateDay(std::uint32_t day, Season season)
	{
		for (auto& m : _markets) m.tradeToday = 0.0f;
		Produce(season);
		Trade();
		RunBusinesses(day, season);
		UpdateProsperity();

		for (auto& m : _markets) {
			m.unrest *= 0.92f;
			m.dragon *= 0.8f;
			m.merchantLoss *= 0.97f;
			m.playerVolume *= 0.9f;
			if (!_linked) m.danger += (m.baseDanger - m.danger) * 0.03f;
		}

		if (day % 7 == 0) {
			for (std::size_t h = 0; h < kHolds; ++h) {
				auto& m = _markets[h];
				m.stockWeekAgo = m.stock;
				for (std::size_t g = 0; g < kGoods; ++g) m.priceWeekAgo[g] = PriceMult(static_cast<Hold>(h), static_cast<Good>(g));
			}
			if (_pendingDividends >= 1.0f) {
				const int paid = static_cast<int>(_pendingDividends);
				_payable += static_cast<float>(paid);
				_pendingDividends -= static_cast<float>(paid);
				Log(day, "A courier brings " + std::to_string(paid) + " gold in dividends from your holdings.", true);
			}
		}
		Announce(day);
	}

	void Economy::Produce(Season season)
	{
		for (std::size_t h = 0; h < kHolds; ++h) {
			auto& m = _markets[h];
			const float disruption = (1.0f - 0.6f * m.danger) * (1.0f - 0.5f * m.unrest) * (1.0f - 0.4f * m.dragon);
			for (std::size_t g = 0; g < kGoods; ++g) {
				const auto good = static_cast<Good>(g);
				float cons = kCons[h][g] * SeasonCons(good, season);
				if (good == Good::Arms) cons *= 1.0f + 1.2f * m.unrest;
				if (good == Good::Food) cons *= 1.0f + 0.2f * m.unrest;
				if (good == Good::Alchemy) cons *= 1.0f + 0.5f * m.unrest + 0.3f * m.dragon;
				// Prices feed back: dear goods get produced more and used less.
				const float p = PriceMult(static_cast<Hold>(h), good);
				cons *= Clamp(std::pow(p, -0.5f), 0.6f, 1.3f);
				const float prod = kProd[h][g] * _norm[g] * SeasonProd(good, season) * disruption * Clamp(std::pow(p, 0.5f), 0.7f, 1.4f) * (0.9f + 0.2f * _rng.Unit());
				const float target = Target(static_cast<Hold>(h), good);
				float st = m.stock[g] + prod - cons;
				st += (target - st) * 0.03f;  // small local traders smooth things out
				m.stock[g] = Clamp(st, 0.0f, target * 4.0f);
			}
		}
	}

	void Economy::Trade()
	{
		for (const auto& r : kRoutes) {
			auto& A = M(r.a);
			auto& B = M(r.b);
			float flow = 0.15f * r.capacity;
			if (!r.sea) flow *= 1.0f - std::max(A.danger, B.danger) * 0.8f;
			flow *= 1.0f - 0.5f * std::max(A.dragon, B.dragon);
			flow *= 1.0f - 0.5f * std::max(A.unrest, B.unrest);
			flow *= 1.0f - 0.3f * 0.5f * (A.merchantLoss + B.merchantLoss);
			if (flow <= 0.0f) continue;
			for (std::size_t g = 0; g < kGoods; ++g) {
				const auto good = static_cast<Good>(g);
				const float ta = Target(r.a, good);
				const float tb = Target(r.b, good);
				const float ra = A.stock[g] / ta;
				const float rb = B.stock[g] / tb;
				if (std::abs(ra - rb) < 0.05f) continue;
				const bool aToB = ra > rb;
				auto& from = aToB ? A : B;
				auto& to = aToB ? B : A;
				float amount = flow * std::abs(ra - rb) * 0.5f * std::min(ta, tb);
				amount = std::min(amount, from.stock[g] * 0.5f);
				from.stock[g] -= amount;
				to.stock[g] += amount;
				const float value = amount * BasePrice(good);
				A.tradeToday += value;
				B.tradeToday += value;
			}
		}
	}

	void Economy::RunBusinesses(std::uint32_t day, Season season)
	{
		for (auto& b : _businesses) {
			auto& m = M(b.hold);
			if (b.ruined) {
				if (day - b.ruinedSince >= 14 && m.unrest < 0.5f && m.danger < 0.7f) {
					b.ruined = false;
					b.condition = 0.25f;
					Log(day, b.name + " has been rebuilt and is working again.", b.playerShares > 0);
				}
				b.lastProfit = 0.0f;
				b.avgProfit *= 0.95f;
				continue;
			}
			const float output = b.capacity * b.condition * (1.0f - 0.5f * m.danger) * (1.0f - 0.4f * m.unrest) * SeasonProd(b.good, season);
			m.stock[I(b.good)] += output;
			const float revenue = output * BasePrice(b.good) * PriceMult(b.hold, b.good);
			const float costs = b.capacity * BasePrice(b.good) * 0.55f;
			b.lastProfit = revenue - costs;
			b.avgProfit = 0.9f * b.avgProfit + 0.1f * b.lastProfit;
			if (b.lastProfit > 0.0f && b.playerShares > 0) {
				const float payout = b.playerShares > kSharesPerBusiness / 2 ? 0.6f : _settings.dividendShare;
				_pendingDividends += b.lastProfit * payout * (static_cast<float>(b.playerShares) / kSharesPerBusiness);
			}

			// Raids by whoever rules the roads.
			const float raid = _settings.raidChance * m.danger * (day < b.guardsUntil ? 0.25f : 1.0f);
			if (_rng.Chance(raid)) {
				const float hit = 0.15f + 0.2f * _rng.Unit();
				b.condition -= hit;
				if (b.condition <= 0.02f) {
					b.condition = 0.0f;
					b.ruined = true;
					b.ruinedSince = day;
					Log(day, "Outlaws burned " + b.name + " in " + std::string(HoldName(b.hold)) + ".", b.playerShares > 0);
				} else {
					Log(day, "Outlaws raided " + b.name + " in " + std::string(HoldName(b.hold)) + ".", b.playerShares > 0);
				}
			} else if (m.unrest < 0.5f) {
				b.condition = std::min(1.0f, b.condition + 0.02f);
			}
		}
	}

	void Economy::UpdateProsperity()
	{
		for (std::size_t h = 0; h < kHolds; ++h) {
			auto& m = _markets[h];
			float expected = 0.0f;
			float scarcity = 0.0f;
			for (std::size_t g = 0; g < kGoods; ++g) {
				expected += Target(static_cast<Hold>(h), static_cast<Good>(g)) * BasePrice(static_cast<Good>(g));
				scarcity += std::max(0.0f, PriceMult(static_cast<Hold>(h), static_cast<Good>(g)) - 1.3f);
			}
			const float tradeRatio = m.tradeToday / std::max(1.0f, expected * 0.02f);
			float target = 0.8f - 0.45f * m.danger - 0.35f * m.unrest - 0.3f * m.dragon - 0.3f * m.merchantLoss;
			target += 0.15f * Clamp(tradeRatio - 0.5f, -0.5f, 0.5f);
			target -= 0.1f * scarcity / static_cast<float>(kGoods);
			target += 0.05f * Clamp(m.playerVolume / 2000.0f, 0.0f, 1.0f);
			m.prosperity = 0.85f * m.prosperity + 0.15f * Clamp(target, 0.05f, 1.0f);
		}
	}

	void Economy::Announce(std::uint32_t day)
	{
		int budget = 3;
		for (std::size_t h = 0; h < kHolds; ++h) {
			for (std::size_t g = 0; g < kGoods; ++g) {
				const float p = PriceMult(static_cast<Hold>(h), static_cast<Good>(g));
				const std::int8_t band = p >= 2.0f ? 2 : p >= 1.45f ? 1 : p <= 0.7f ? -1 : 0;
				auto& old = _markets[h].band[g];
				if (band == old) continue;
				const bool worse = (band > 0 && band > old) || (band < 0 && band < old);
				old = band;
				if (!worse || budget <= 0) continue;
				--budget;
				const std::string what(GoodName(static_cast<Good>(g)));
				const std::string where(HoldName(static_cast<Hold>(h)));
				if (band == 2) Log(day, "Supplies of " + Lower(what) + " are desperately short in " + where + " (" + Pct(p) + ").", true);
				else if (band == 1) Log(day, "Supplies of " + Lower(what) + " are running short in " + where + " (" + Pct(p) + ").", true);
				else Log(day, "There's a glut of " + Lower(what) + " in " + where + " (" + Pct(p) + ").", true);
			}
		}
	}

	void Economy::Log(std::uint32_t day, std::string text, bool notify)
	{
		if (notify && _notices.size() < 32) _notices.push_back(text);
		_news.push_back({ day, std::move(text) });
		const auto limit = static_cast<std::size_t>(std::max(50, _settings.newsLimit));
		if (_news.size() > limit) _news.erase(_news.begin(), _news.begin() + static_cast<std::ptrdiff_t>(_news.size() - limit));
	}

	std::vector<std::string> Economy::DrainNotices()
	{
		std::vector<std::string> out;
		out.swap(_notices);
		return out;
	}

	// =======================================================================
	// events

	void Economy::SetRoadDanger(Hold h, float danger)
	{
		if (h == Hold::Unknown || I(h) >= kHolds) return;
		_linked = true;
		M(h).danger = Clamp(danger, 0.0f, 1.0f);
	}

	void Economy::OnOfficerSlain(Hold h, float magnitude)
	{
		if (h == Hold::Unknown || I(h) >= kHolds) return;
		auto& m = M(h);
		m.danger = Clamp(m.danger - 0.08f * magnitude, 0.0f, 1.0f);
		Log(_day, "With an outlaw captain dead, caravans are braving the roads of " + std::string(HoldName(h)) + " again.", false);
	}

	void Economy::OnHoldSeized(Hold h)
	{
		if (h == Hold::Unknown || I(h) >= kHolds) return;
		M(h).unrest = Clamp(M(h).unrest + 0.1f, 0.0f, 1.0f);
		Log(_day, "Merchants in " + std::string(HoldName(h)) + " say a new gang controls the roads, and charge accordingly.", false);
	}

	void Economy::OnSkirmish(Hold h, float magnitude)
	{
		if (h == Hold::Unknown || I(h) >= kHolds) return;
		M(h).unrest = Clamp(M(h).unrest + 0.03f * magnitude, 0.0f, 1.0f);
	}

	void Economy::OnBattle(Hold h)
	{
		if (h == Hold::Unknown || I(h) >= kHolds) return;
		auto& m = M(h);
		const bool news = m.unrest < 0.3f;
		m.unrest = Clamp(m.unrest + 0.35f, 0.0f, 1.0f);
		if (news) Log(_day, "Fighting between the Legion and the Stormcloaks has thrown " + std::string(HoldName(h)) + " into turmoil. Arms are in demand.", true);
	}

	void Economy::OnDragon(Hold h, bool inSettlement)
	{
		if (h == Hold::Unknown || I(h) >= kHolds) return;
		auto& m = M(h);
		if (!inSettlement) {
			m.dragon = std::max(m.dragon, 0.35f);
			return;
		}
		const bool news = m.dragon < 0.6f;
		m.dragon = 1.0f;
		for (auto& b : _businesses) {
			if (b.hold != h || b.ruined || !_rng.Chance(0.35f)) continue;
			b.condition -= 0.25f + 0.35f * _rng.Unit();
			if (b.condition <= 0.02f) {
				b.condition = 0.0f;
				b.ruined = true;
				b.ruinedSince = _day;
				Log(_day, "Dragonfire destroyed " + b.name + ".", b.playerShares > 0);
			} else {
				Log(_day, "A dragon damaged " + b.name + ".", b.playerShares > 0);
			}
		}
		if (news) Log(_day, "A dragon attack has emptied the markets of " + std::string(HoldName(h)) + ".", true);
	}

	void Economy::OnMerchantKilled(Hold h)
	{
		if (h == Hold::Unknown || I(h) >= kHolds) return;
		M(h).merchantLoss = Clamp(M(h).merchantLoss + 0.2f, 0.0f, 1.0f);
		Log(_day, "A merchant's death has left " + std::string(HoldName(h)) + " short of coin and goods.", false);
	}

	void Economy::OnOutlawKilled(Hold h)
	{
		if (_linked || h == Hold::Unknown || I(h) >= kHolds) return;
		auto& m = M(h);
		m.danger = std::max(0.05f, m.danger - 0.01f);
	}

	void Economy::OnPlayerTrade(Hold h, Good g, int units, bool playerSold, int unitValue)
	{
		if (h == Hold::Unknown || I(h) >= kHolds || units <= 0) return;
		auto& m = M(h);
		auto& s = m.stock[I(g)];
		s = Clamp(s + (playerSold ? 1.0f : -1.0f) * static_cast<float>(units), 0.0f, Target(h, g) * 4.0f);
		m.playerVolume += static_cast<float>(units * std::max(0, unitValue));
	}

	// =======================================================================
	// prices

	float Economy::PriceMult(Hold h, Good g) const
	{
		if (h == Hold::Unknown || I(h) >= kHolds) return 1.0f;
		const auto& m = M(h);
		const float target = Target(h, g);
		float p = std::pow(target / std::max(m.stock[I(g)], target * 0.05f), 0.7f);
		p = 1.0f + (p - 1.0f) * _settings.priceSwing;
		if (g == Good::Arms) p *= 1.0f + 0.4f * m.unrest;
		p *= 1.0f + 0.15f * m.dragon;
		return Clamp(p, 0.5f, 2.5f);
	}

	float Economy::MerchantGoldMult(Hold h) const
	{
		if (h == Hold::Unknown || I(h) >= kHolds) return 1.0f;
		return Clamp(1.0f + (M(h).prosperity - 0.65f) * 1.6f, 0.45f, 1.6f);  // ~1.0 in ordinary times
	}

	float Economy::StockLevel(Hold h, Good g) const
	{
		if (h == Hold::Unknown || I(h) >= kHolds) return 0.0f;
		const float r = M(h).stock[I(g)] / Target(h, g);
		return Clamp(r - 1.0f, -1.0f, 1.0f);
	}

	std::string_view Economy::ProsperityWord(Hold h) const
	{
		if (h == Hold::Unknown || I(h) >= kHolds) return "unknown";
		const float p = M(h).prosperity;
		return p >= 0.78f ? "Thriving" : p >= 0.55f ? "Steady" : p >= 0.35f ? "Struggling" : "Dire";
	}

	// =======================================================================
	// investment

	std::vector<std::uint16_t> Economy::BusinessesIn(Hold h) const
	{
		std::vector<std::uint16_t> out;
		for (const auto& b : _businesses) {
			if (b.hold == h) out.push_back(b.id);
		}
		return out;
	}

	const Business* Economy::GetBusiness(std::uint16_t id) const
	{
		return id && id <= _businesses.size() ? &_businesses[id - 1] : nullptr;
	}

	int Economy::SharePrice(std::uint16_t id) const
	{
		const Business* b = GetBusiness(id);
		if (!b) return 0;
		const float base = b->capacity * BasePrice(b->good);
		float valuation = std::max(base * 8.0f, std::max(0.0f, b->avgProfit) * 60.0f + base * 10.0f * b->condition);
		if (b->ruined) valuation *= 0.15f;
		return std::max(1, static_cast<int>(std::lround(valuation / kSharesPerBusiness)));
	}

	int Economy::QuoteBuy(std::uint16_t id, int shares) const
	{
		const Business* b = GetBusiness(id);
		if (!b || b->ruined || shares <= 0 || b->playerShares + shares > kSharesPerBusiness) return -1;
		return static_cast<int>(std::ceil(static_cast<float>(SharePrice(id) * shares) * (1.0f + _settings.shareFee)));
	}

	int Economy::Buy(std::uint16_t id, int shares, int playerGold)
	{
		const int cost = QuoteBuy(id, shares);
		if (cost < 0 || cost > playerGold) return -1;
		auto& b = _businesses[id - 1];
		const bool wasMajority = b.playerShares > kSharesPerBusiness / 2;
		b.playerShares = static_cast<std::uint16_t>(b.playerShares + shares);
		Log(_day, "You bought " + std::to_string(shares) + " shares of " + b.name + " for " + std::to_string(cost) + " gold.", false);
		if (!wasMajority && b.playerShares > kSharesPerBusiness / 2) {
			Log(_day, "You now hold a controlling stake in " + b.name + ". Its dividends are yours to set, and you can hire it guards.", true);
		}
		return cost;
	}

	int Economy::Sell(std::uint16_t id, int shares)
	{
		if (!GetBusiness(id) || shares <= 0) return 0;
		auto& b = _businesses[id - 1];
		shares = std::min<int>(shares, b.playerShares);
		if (shares <= 0) return 0;
		const int proceeds = static_cast<int>(static_cast<float>(SharePrice(id) * shares) * (1.0f - _settings.shareFee * 0.5f));
		b.playerShares = static_cast<std::uint16_t>(b.playerShares - shares);
		Log(_day, "You sold " + std::to_string(shares) + " shares of " + b.name + " for " + std::to_string(proceeds) + " gold.", false);
		return proceeds;
	}

	int Economy::GuardCost(const Business& b)
	{
		return std::max(150, static_cast<int>(std::lround(b.capacity * BasePrice(b.good) * 6.0f)));
	}

	int Economy::HireGuards(std::uint16_t id, int playerGold)
	{
		if (!GetBusiness(id)) return -1;
		auto& b = _businesses[id - 1];
		const int cost = GuardCost(b);
		if (b.playerShares <= kSharesPerBusiness / 2 || b.ruined || cost > playerGold) return -1;
		b.guardsUntil = std::max(b.guardsUntil, _day) + 30;
		Log(_day, "You hired guards for " + b.name + " until day " + std::to_string(b.guardsUntil) + ".", false);
		return cost;
	}

	int Economy::CollectDividends()
	{
		const int out = static_cast<int>(_payable);
		_payable = 0.0f;
		return out;
	}

	int Economy::PortfolioValue() const
	{
		int total = 0;
		for (const auto& b : _businesses) total += b.playerShares * SharePrice(b.id);
		return total;
	}

	// =======================================================================
	// reports

	std::string Economy::MarketReport(Hold h) const
	{
		if (h == Hold::Unknown || I(h) >= kHolds) return "No market here.";
		const auto& m = M(h);
		auto level = [](float v) -> std::string { return v >= 0.66f ? "high" : v >= 0.33f ? "moderate" : v >= 0.08f ? "low" : "none"; };
		std::string s = std::string(HoldName(h)) + " - " + std::string(ProsperityWord(h));
		s += "\nRoad danger " + level(m.danger) + ", war " + level(m.unrest) + ", dragons " + level(m.dragon);
		s += "\nMerchants' purses " + Pct(MerchantGoldMult(h)) + "\n";
		for (std::size_t g = 0; g < kGoods; ++g) {
			const float p = PriceMult(h, static_cast<Good>(g));
			const float was = m.priceWeekAgo[g];
			const char* trend = p > was * 1.05f ? " rising" : p < was * 0.95f ? " falling" : "";
			s += "\n" + std::string(GoodName(static_cast<Good>(g))) + ": " + Pct(p) + trend;
		}
		return s;
	}

	std::string Economy::BusinessReport(std::uint16_t id) const
	{
		const Business* b = GetBusiness(id);
		if (!b) return {};
		std::string s = b->name + "\n" + std::string(HoldName(b->hold)) + " - " + std::string(GoodName(b->good));
		s += b->ruined ? "\nRUINED - rebuilding" : "\nCondition " + std::to_string(static_cast<int>(b->condition * 100)) + "%";
		s += "\nProfit about " + std::to_string(static_cast<int>(b->avgProfit)) + " gold a day";
		s += "\nShare price " + std::to_string(SharePrice(id)) + " gold";
		s += "\nYou own " + std::to_string(b->playerShares) + " of " + std::to_string(kSharesPerBusiness) + " shares";
		if (b->playerShares > kSharesPerBusiness / 2) s += " (controlling stake)";
		if (b->guardsUntil > _day) s += "\nGuarded until day " + std::to_string(b->guardsUntil);
		return s;
	}

	// =======================================================================
	// persistence

	std::vector<std::uint8_t> Economy::Save() const
	{
		Writer w;
		w.Pod(std::uint32_t{ 0x4C444752 });  // 'LDGR'
		w.Pod(kSaveVersion);
		for (int i = 0; i < 4; ++i) w.Pod(_rng.State()[i]);
		w.Pod(_day);
		w.Pod(_month);
		w.Pod(static_cast<std::uint8_t>(_linked));
		w.Pod(_pendingDividends);
		w.Pod(_payable);
		for (const auto& m : _markets) {
			w.Pod(m.stock);
			w.Pod(m.stockWeekAgo);
			w.Pod(m.priceWeekAgo);
			w.Pod(m.band);
			w.Pod(m.danger);
			w.Pod(m.baseDanger);
			w.Pod(m.unrest);
			w.Pod(m.dragon);
			w.Pod(m.merchantLoss);
			w.Pod(m.prosperity);
			w.Pod(m.playerVolume);
		}
		w.Pod(static_cast<std::uint32_t>(_businesses.size()));
		for (const auto& b : _businesses) {
			w.Pod(b.condition);
			w.Pod(b.avgProfit);
			w.Pod(b.playerShares);
			w.Pod(b.guardsUntil);
			w.Pod(b.ruinedSince);
			w.Pod(static_cast<std::uint8_t>(b.ruined));
		}
		w.Pod(static_cast<std::uint32_t>(_news.size()));
		for (const auto& n : _news) {
			w.Pod(n.day);
			w.Str(n.text);
		}
		return std::move(w.buf);
	}

	bool Economy::Load(std::span<const std::uint8_t> data)
	{
		Reader r(data);
		std::uint32_t magic{}, version{};
		if (!r.Pod(magic) || magic != 0x4C444752 || !r.Pod(version) || version > kSaveVersion) return false;
		Economy e(_settings);
		std::uint8_t linked{};
		for (int i = 0; i < 4; ++i) r.Pod(e._rng.State()[i]);
		r.Pod(e._day);
		r.Pod(e._month);
		r.Pod(linked);
		r.Pod(e._pendingDividends);
		r.Pod(e._payable);
		e._linked = linked != 0;
		for (auto& m : e._markets) {
			r.Pod(m.stock);
			r.Pod(m.stockWeekAgo);
			r.Pod(m.priceWeekAgo);
			r.Pod(m.band);
			r.Pod(m.danger);
			r.Pod(m.baseDanger);
			r.Pod(m.unrest);
			r.Pod(m.dragon);
			r.Pod(m.merchantLoss);
			r.Pod(m.prosperity);
			r.Pod(m.playerVolume);
			for (auto v : m.stock) {
				if (!std::isfinite(v)) return false;
			}
			if (!std::isfinite(m.danger) || !std::isfinite(m.prosperity)) return false;
		}
		std::uint32_t n{};
		if (!r.Pod(n)) return false;
		// Businesses are defined in code; saved state is matched by position, so
		// adding businesses in a later version keeps old saves working.
		for (std::uint32_t i = 0; i < n && r.Ok(); ++i) {
			Business tmp;
			std::uint8_t ruined{};
			r.Pod(tmp.condition);
			r.Pod(tmp.avgProfit);
			r.Pod(tmp.playerShares);
			r.Pod(tmp.guardsUntil);
			r.Pod(tmp.ruinedSince);
			r.Pod(ruined);
			if (i < e._businesses.size()) {
				auto& b = e._businesses[i];
				b.condition = Clamp(tmp.condition, 0.0f, 1.0f);
				b.avgProfit = std::isfinite(tmp.avgProfit) ? tmp.avgProfit : 0.0f;
				b.playerShares = std::min<std::uint16_t>(tmp.playerShares, kSharesPerBusiness);
				b.guardsUntil = tmp.guardsUntil;
				b.ruinedSince = tmp.ruinedSince;
				b.ruined = ruined != 0;
			}
		}
		if (!r.Pod(n) || n > 100000) return false;
		e._news.clear();
		for (std::uint32_t i = 0; i < n && r.Ok(); ++i) {
			NewsEntry ne;
			r.Pod(ne.day);
			r.Str(ne.text);
			e._news.push_back(std::move(ne));
		}
		if (!r.Ok() || !std::isfinite(e._pendingDividends) || !std::isfinite(e._payable)) return false;
		*this = std::move(e);
		return true;
	}
}

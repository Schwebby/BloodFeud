#pragma once
// Ledger: a living economy for Skyrim. Game-independent so it can be tested
// and tuned anywhere; the SKSE plugin feeds it events and applies its prices.

#include "ledger/Rng.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ledger
{
	// Same numbering as Bloodfeud's API.
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
	inline constexpr std::size_t kHolds = static_cast<std::size_t>(Hold::Count);

	enum class Good : std::uint8_t
	{
		Food,
		Drink,
		Ore,      // ore and ingots
		Arms,     // weapons, armour, ammunition
		Alchemy,  // potions, poisons, ingredients
		Lumber,
		Pelts,    // hides and animal parts
		Luxury,   // gems, jewellery, soul gems, imports
		Count
	};
	inline constexpr std::size_t kGoods = static_cast<std::size_t>(Good::Count);

	enum class Season : std::uint8_t
	{
		Spring,
		Summer,
		Harvest,
		Winter
	};
	Season SeasonFromMonth(std::uint32_t month);  // 0 = Morning Star ... 11 = Evening Star

	struct Market
	{
		std::array<float, kGoods> stock{};
		std::array<float, kGoods> stockWeekAgo{};  // for trend arrows
		std::array<float, kGoods> priceWeekAgo{};
		std::array<std::int8_t, kGoods> band{};    // last announced price band
		float danger{ 0.3f };        // road danger 0..1 (from Bloodfeud or estimated)
		float baseDanger{ 0.3f };    // where danger drifts back to without Bloodfeud
		float unrest{ 0.0f };        // civil war fighting 0..1
		float dragon{ 0.0f };        // dragon fear 0..1
		float merchantLoss{ 0.0f };  // dead merchants 0..1
		float prosperity{ 0.6f };    // 0..1
		float tradeToday{ 0.0f };    // value moved through this hold today
		float playerVolume{ 0.0f };  // value the player traded here (recent)
	};

	struct Business
	{
		std::uint16_t id{ 0 };
		std::string name;
		Hold hold{ Hold::Unknown };
		Good good{ Good::Food };
		float capacity{ 5.0f };    // units per day at full condition
		float condition{ 1.0f };   // 0 ruined .. 1 sound
		float avgProfit{ 0.0f };   // per day, smoothed
		float lastProfit{ 0.0f };
		std::uint16_t playerShares{ 0 };  // of 100
		std::uint32_t guardsUntil{ 0 };   // day; hired guards reduce raids
		std::uint32_t ruinedSince{ 0 };
		bool ruined{ false };
	};

	struct NewsEntry
	{
		std::uint32_t day{ 0 };
		std::string text;
	};

	struct Settings
	{
		float priceSwing = 1.0f;       // scales how far prices move from normal
		float raidChance = 0.03f;      // per day at full danger
		float dividendShare = 0.4f;    // of profit paid out (0.6 with a majority stake)
		float shareFee = 0.05f;        // broker's cut on buying
		int newsLimit = 300;
	};

	class Economy
	{
	public:
		static constexpr std::uint16_t kSharesPerBusiness = 100;
		static constexpr std::uint32_t kSaveVersion = 1;

		explicit Economy(Settings s = {}, std::uint64_t seed = 0x1ED6E4ull);

		Settings& GetSettings() { return _settings; }

		// ---- time ---------------------------------------------------------
		// Advance to `day` (simulates each missed day, max 30). month: 0..11.
		void TickDay(std::uint32_t day, std::uint32_t month);
		[[nodiscard]] std::uint32_t Day() const { return _day; }

		// ---- world events ---------------------------------------------------
		// From Bloodfeud (overrides the estimate). Marks the economy as linked.
		void SetRoadDanger(Hold h, float danger);
		void OnOfficerSlain(Hold h, float magnitude);
		void OnHoldSeized(Hold h);
		void OnSkirmish(Hold h, float magnitude);
		// Vanilla events.
		void OnBattle(Hold h);                    // Imperials vs Stormcloaks
		void OnDragon(Hold h, bool inSettlement);
		void OnMerchantKilled(Hold h);
		void OnOutlawKilled(Hold h);              // only used without Bloodfeud
		void OnPlayerTrade(Hold h, Good g, int units, bool playerSold, int unitValue);
		[[nodiscard]] bool LinkedToBloodfeud() const { return _linked; }

		// ---- prices for the plugin ---------------------------------------------
		// Multiplier on an item's normal value in this hold (0.5 .. 2.5).
		[[nodiscard]] float PriceMult(Hold h, Good g) const;
		// Multiplier on merchants' gold (about 0.45 .. 1.6).
		[[nodiscard]] float MerchantGoldMult(Hold h) const;
		// -1 (scarce) .. +1 (glut): merchants carry less/more of it.
		[[nodiscard]] float StockLevel(Hold h, Good g) const;

		// ---- investment ----------------------------------------------------------
		[[nodiscard]] const std::vector<Business>& Businesses() const { return _businesses; }
		[[nodiscard]] std::vector<std::uint16_t> BusinessesIn(Hold h) const;
		[[nodiscard]] const Business* GetBusiness(std::uint16_t id) const;
		[[nodiscard]] int SharePrice(std::uint16_t id) const;
		// Total cost including the broker's fee, or -1 if not possible.
		[[nodiscard]] int QuoteBuy(std::uint16_t id, int shares) const;
		// Returns gold spent, or -1 (not enough shares / ruined / not enough gold).
		int Buy(std::uint16_t id, int shares, int playerGold);
		// Returns gold received.
		int Sell(std::uint16_t id, int shares);
		[[nodiscard]] static int GuardCost(const Business& b);
		// Majority owners can hire guards for 30 days. Returns gold spent or -1.
		int HireGuards(std::uint16_t id, int playerGold);
		// Gold owed to the player (paid weekly by courier). Resets to 0.
		int CollectDividends();
		[[nodiscard]] int PendingDividends() const { return static_cast<int>(_pendingDividends); }
		[[nodiscard]] int PortfolioValue() const;

		// ---- reporting -------------------------------------------------------------
		std::vector<std::string> DrainNotices();
		[[nodiscard]] const std::vector<NewsEntry>& News() const { return _news; }
		[[nodiscard]] std::string MarketReport(Hold h) const;
		[[nodiscard]] std::string BusinessReport(std::uint16_t id) const;
		[[nodiscard]] std::string_view ProsperityWord(Hold h) const;
		[[nodiscard]] const Market& GetMarket(Hold h) const { return _markets[static_cast<std::size_t>(h)]; }

		// ---- persistence ---------------------------------------------------------
		[[nodiscard]] std::vector<std::uint8_t> Save() const;
		bool Load(std::span<const std::uint8_t> data);

		// Static data, exposed for tests and reports.
		static float BasePrice(Good g);
		static float Target(Hold h, Good g);  // a week of consumption

	private:
		void SimulateDay(std::uint32_t day, Season season);
		void Produce(Season season);
		void Trade();
		void RunBusinesses(std::uint32_t day, Season season);
		void UpdateProsperity();
		void Announce(std::uint32_t day);
		void Log(std::uint32_t day, std::string text, bool notify);
		Market& M(Hold h) { return _markets[static_cast<std::size_t>(h)]; }
		const Market& M(Hold h) const { return _markets[static_cast<std::size_t>(h)]; }
		void InitBusinesses();

		Settings _settings;
		Rng _rng;
		std::array<Market, kHolds> _markets{};
		std::vector<Business> _businesses;
		std::vector<NewsEntry> _news;
		std::vector<std::string> _notices;
		float _pendingDividends{ 0.0f };  // accrued this week
		float _payable{ 0.0f };           // delivered, waiting to be handed over
		std::uint32_t _day{ 0 };
		std::uint32_t _month{ 0 };
		bool _linked{ false };
		std::array<float, kGoods> _norm{};  // balances production against consumption (derived, not saved)
		void ComputeNorm();
	};

	// Helpers shared with the plugin.
	std::string_view HoldName(Hold h);
	std::string_view GoodName(Good g);
	Hold HoldFromLocationName(std::string_view name);
}

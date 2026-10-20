#include "UI.h"

#include "Game.h"
#include "Settings.h"

namespace lgp::ui
{
	namespace
	{
		using ledger::Hold;

		class Callback : public RE::IMessageBoxCallback
		{
		public:
			explicit Callback(std::function<void(int)> fn) : _fn(std::move(fn)) {}
			~Callback() override = default;

			void Run(Message a_msg) override
			{
				// The engine reports buttons starting at 4 (community-documented).
				const int index = static_cast<int>(a_msg) - 4;
				auto fn = _fn;
				SKSE::GetTaskInterface()->AddTask([fn, index]() {
					S().menuOpen = false;
					if (fn) fn(index);
				});
			}

		private:
			std::function<void(int)> _fn;
		};

		std::string MonthName()
		{
			auto* cal = RE::Calendar::GetSingleton();
			return cal ? cal->GetMonthName() : std::string{};
		}

		void Back() { ShowMain(); }

		void ShowBusiness(std::uint16_t id, bool fromHoldings);

		void ShowHoldList(const std::string& title, const std::function<void(Hold)>& onPick)
		{
			std::vector<std::string> buttons;
			for (std::size_t h = 0; h < ledger::kHolds; ++h) buttons.emplace_back(ledger::HoldName(static_cast<Hold>(h)));
			buttons.emplace_back("Back");
			Show(title, buttons, [onPick](int pick) {
				if (pick >= 0 && static_cast<std::size_t>(pick) < ledger::kHolds) onPick(static_cast<Hold>(pick));
				else Back();
			});
		}

		void ShowMarket(Hold h, std::function<void()> back)
		{
			Show(S().econ.MarketReport(h) + "\n\nPrices are relative to normal.", { "Back" }, [back](int) { back(); });
		}

		void ShowAllMarkets()
		{
			ShowHoldList("Which market?", [](Hold h) { ShowMarket(h, ShowAllMarkets); });
		}

		void ShowBusinessList(const std::string& title, const std::vector<std::uint16_t>& ids, bool fromHoldings)
		{
			auto& e = S().econ;
			if (ids.empty()) {
				Show(title + "\n\n" + (fromHoldings ? "You own nothing yet." : "No one here is selling shares."), { "Back" }, [](int) { Back(); });
				return;
			}
			std::vector<std::string> buttons;
			for (auto id : ids) {
				const auto* b = e.GetBusiness(id);
				buttons.push_back(b->name + (b->ruined ? " (ruined)" : "") + " - " + std::to_string(e.SharePrice(id)) + "g/share");
			}
			buttons.emplace_back("Back");
			Show(title, buttons, [ids, fromHoldings](int pick) {
				if (pick >= 0 && static_cast<std::size_t>(pick) < ids.size()) ShowBusiness(ids[static_cast<std::size_t>(pick)], fromHoldings);
				else Back();
			});
		}

		std::vector<std::uint16_t> Owned()
		{
			std::vector<std::uint16_t> out;
			for (const auto& b : S().econ.Businesses()) {
				if (b.playerShares > 0) out.push_back(b.id);
			}
			return out;
		}

		void Result(const std::string& text, std::uint16_t id, bool fromHoldings)
		{
			Show(text, { "Back" }, [id, fromHoldings](int) { ShowBusiness(id, fromHoldings); });
		}

		void ShowBusiness(std::uint16_t id, bool fromHoldings)
		{
			auto& e = S().econ;
			const auto* b = e.GetBusiness(id);
			if (!b) return Back();

			struct Action
			{
				std::string label;
				std::function<void()> run;
			};
			std::vector<Action> actions;
			for (int n : { 5, 10, 25 }) {
				const int cost = e.QuoteBuy(id, n);
				if (cost < 0) continue;
				actions.push_back({ "Buy " + std::to_string(n) + " (" + std::to_string(cost) + "g)", [id, n, fromHoldings]() {
					std::scoped_lock l(S().lock);
					const int spent = S().econ.Buy(id, n, game::PlayerGold());
					if (spent < 0) return Result("You can't afford that.", id, fromHoldings);
					game::TakeGold(spent);
					Result("The steward records your purchase.", id, fromHoldings);
				} });
			}
			if (b->playerShares > 0) {
				actions.push_back({ "Sell 5", [id, fromHoldings]() {
					std::scoped_lock l(S().lock);
					game::GiveGold(S().econ.Sell(id, 5));
					Result("Sold.", id, fromHoldings);
				} });
				actions.push_back({ "Sell all", [id, fromHoldings]() {
					std::scoped_lock l(S().lock);
					game::GiveGold(S().econ.Sell(id, ledger::Economy::kSharesPerBusiness));
					Result("You've sold your stake.", id, fromHoldings);
				} });
			}
			if (b->playerShares > ledger::Economy::kSharesPerBusiness / 2 && !b->ruined) {
				const int cost = ledger::Economy::GuardCost(*b);
				actions.push_back({ "Hire guards, 30 days (" + std::to_string(cost) + "g)", [id, fromHoldings]() {
					std::scoped_lock l(S().lock);
					const int spent = S().econ.HireGuards(id, game::PlayerGold());
					if (spent < 0) return Result("You can't afford guards.", id, fromHoldings);
					game::TakeGold(spent);
					Result("Sellswords will watch over it for a month.", id, fromHoldings);
				} });
			}

			std::vector<std::string> buttons;
			for (const auto& a : actions) buttons.push_back(a.label);
			buttons.emplace_back("Back");
			Show(e.BusinessReport(id) + "\n\nYour purse: " + std::to_string(game::PlayerGold()) + " gold", buttons, [actions, fromHoldings](int pick) {
				if (pick >= 0 && static_cast<std::size_t>(pick) < actions.size()) {
					actions[static_cast<std::size_t>(pick)].run();
				} else if (fromHoldings) {
					ShowBusinessList("Your holdings", Owned(), true);
				} else {
					Back();
				}
			});
		}

		void ShowNews(std::size_t page)
		{
			const auto& n = S().econ.News();
			if (n.empty()) {
				Show("No news yet.", { "Back" }, [](int) { Back(); });
				return;
			}
			constexpr std::size_t per = 8;
			const std::size_t pages = (n.size() + per - 1) / per;
			page = std::min(page, pages - 1);
			std::string body = "Market news (" + std::to_string(page + 1) + "/" + std::to_string(pages) + ")\n";
			for (std::size_t i = 0; i < per; ++i) {
				const std::size_t idx = page * per + i;
				if (idx >= n.size()) break;
				const auto& e = n[n.size() - 1 - idx];
				body += "\nDay " + std::to_string(e.day) + ": " + e.text;
			}
			std::vector<std::string> buttons;
			const bool older = page + 1 < pages;
			const bool newer = page > 0;
			if (older) buttons.emplace_back("Older");
			if (newer) buttons.emplace_back("Newer");
			buttons.emplace_back("Back");
			Show(body, buttons, [=](int pick) {
				int i = 0;
				if (older && pick == i++) return ShowNews(page + 1);
				if (newer && pick == i++) return ShowNews(page - 1);
				Back();
			});
		}
	}

	void Show(const std::string& body, const std::vector<std::string>& buttons, std::function<void(int)> onPick)
	{
		auto* factoryManager = RE::MessageDataFactoryManager::GetSingleton();
		auto* strings = RE::InterfaceStrings::GetSingleton();
		if (!factoryManager || !strings) return;
		auto* factory = factoryManager->GetCreator<RE::MessageBoxData>(strings->messageBoxData);
		auto* box = factory ? factory->Create() : nullptr;
		if (!box) return;
		box->bodyText = body.c_str();
		for (const auto& b : buttons) box->buttonText.push_back(b.c_str());
		box->callback = RE::make_smart<Callback>(std::move(onPick));
		box->unk38 = 10;  // values used by community SKSE message-box helpers
		box->unk48 = 4;
		S().menuOpen = true;
		box->QueueMessage();
	}

	void ShowMain()
	{
		auto& s = S();
		std::scoped_lock l(s.lock);
		auto& e = s.econ;
		auto* player = RE::PlayerCharacter::GetSingleton();
		const Hold here = game::HoldOf(player);
		std::string body = "LEDGER - " + MonthName() + ", day " + std::to_string(game::Today());
		if (here != Hold::Unknown) body += "\nYou are in " + std::string(ledger::HoldName(here)) + ": " + std::string(e.ProsperityWord(here)) + ".";
		body += "\nYour holdings are worth " + std::to_string(e.PortfolioValue()) + " gold; about " + std::to_string(e.PendingDividends()) + " gold in dividends due this week.";
		if (e.LinkedToBloodfeud()) body += "\nRoad reports come from Bloodfeud.";
		if (!e.News().empty()) body += "\n\nLatest: " + e.News().back().text;

		Show(body, { "Local market", "All markets", "Invest here", "My holdings", "News", "Close" }, [here](int pick) {
			auto* p = RE::PlayerCharacter::GetSingleton();
			switch (pick) {
			case 0:
				if (here == Hold::Unknown) return Show("You're far from any market.", { "Back" }, [](int) { Back(); });
				return ShowMarket(here, Back);
			case 1:
				return ShowAllMarkets();
			case 2:
				if (here == Hold::Unknown || !game::InSettlement(p)) {
					return Show("Shares are bought through a hold's stewards. Visit a town or city.", { "Back" }, [](int) { Back(); });
				}
				return ShowBusinessList("Ventures in " + std::string(ledger::HoldName(here)), S().econ.BusinessesIn(here), false);
			case 3:
				return ShowBusinessList("Your holdings", Owned(), true);
			case 4:
				return ShowNews(0);
			default:
				return;
			}
		});
	}
}

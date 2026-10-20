#include "UI.h"

#include "Game.h"
#include "bloodfeud/Text.hpp"

namespace bfp::ui
{
	namespace
	{
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
				// Hop to the main thread before touching the world.
				SKSE::GetTaskInterface()->AddTask([fn, index]() {
					S().menuOpen = false;
					if (fn) fn(index);
				});
			}

		private:
			std::function<void(int)> _fn;
		};

		constexpr std::size_t kPageSize = 7;

		int Gold()
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto counts = player->GetInventoryCounts([](RE::TESBoundObject& obj) { return obj.IsGold(); });
			int total = 0;
			for (const auto& [obj, n] : counts) total += n;
			return total;
		}

		void ShowDossier(bf::NpcId id, std::function<void()> back);

		// A paged list of people; picking one opens their dossier.
		void ShowPeople(const std::string& title, std::vector<bf::NpcId> ids, std::size_t page, std::function<void()> back)
		{
			auto& w = S().world;
			if (ids.empty()) {
				Show(title + "\n\nNo one yet.", { "Back" }, [back](int) { back(); });
				return;
			}
			const std::size_t pages = (ids.size() + kPageSize - 1) / kPageSize;
			page = std::min(page, pages - 1);
			std::vector<std::string> buttons;
			std::vector<bf::NpcId> shown;
			for (std::size_t i = page * kPageSize; i < std::min(ids.size(), (page + 1) * kPageSize); ++i) {
				const bf::Npc* n = w.Get(ids[i]);
				if (!n) continue;
				buttons.push_back(n->ShortName() + (n->nemesis ? " *" : ""));
				shown.push_back(ids[i]);
			}
			const bool more = page + 1 < pages;
			if (more) buttons.emplace_back("More...");
			buttons.emplace_back("Back");
			const std::string body = title + (pages > 1 ? bf::text::Fmt("  ({0}/{1})", { std::to_string(page + 1), std::to_string(pages) }) : std::string{}) + "\n* = nemesis";
			Show(body, buttons, [=](int pick) {
				if (pick >= 0 && static_cast<std::size_t>(pick) < shown.size()) {
					const bf::NpcId id = shown[static_cast<std::size_t>(pick)];
					ShowDossier(id, [=]() { ShowPeople(title, ids, page, back); });
				} else if (more && static_cast<std::size_t>(pick) == shown.size()) {
					ShowPeople(title, ids, page + 1, back);
				} else {
					back();
				}
			});
		}

		void ShowDossier(bf::NpcId id, std::function<void()> back)
		{
			auto& w = S().world;
			const bf::Npc* n = w.Get(id);
			if (!n) {
				back();
				return;
			}
			std::vector<std::string> buttons;
			const bool canParley = n->Alive() && n->hatred > 0;
			const int cost = w.ParleyCost(id);
			if (canParley) buttons.push_back(bf::text::Fmt("Send weregild ({0} gold)", { std::to_string(cost) }));
			buttons.emplace_back("Back");
			Show(w.Dossier(id), buttons, [=](int pick) {
				if (!(canParley && pick == 0)) {
					back();
					return;
				}
				std::scoped_lock l(S().lock);
				if (Gold() < cost) {
					Show(bf::text::Fmt("You need {0} gold for a weregild worth sending.", { std::to_string(cost) }), { "Back" }, [=](int) { ShowDossier(id, back); });
					return;
				}
				auto* player = RE::PlayerCharacter::GetSingleton();
				if (auto* gold = RE::TESForm::LookupByID<RE::TESBoundObject>(0xF)) {
					player->RemoveItem(gold, cost, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
				}
				const bool ok = S().world.Parley(id, cost, game::Today());
				const auto& chron = S().world.Chronicle();
				const std::string result = chron.empty() ? std::string(ok ? "The feud is ended." : "Your letter was refused.") : chron.back().text;
				Show("A courier carries your letter and the gold.\n\n" + result, { "Back" }, [back](int) { back(); });
			});
		}

		void ShowFactions()
		{
			std::vector<std::string> buttons;
			for (std::size_t i = 0; i < bf::kFactionCount; ++i) {
				std::string name(bf::text::FactionName(static_cast<bf::FactionId>(i)));
				if (!name.empty()) name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
				buttons.push_back(name);
			}
			buttons.emplace_back("Back");
			Show("The powers of Skyrim's wilds", buttons, [](int pick) {
				if (pick >= 0 && static_cast<std::size_t>(pick) < bf::kFactionCount) {
					const auto f = static_cast<bf::FactionId>(pick);
					auto& w = S().world;
					const auto& fac = w.GetFaction(f);
					std::vector<bf::NpcId> people;
					if (fac.leader) people.push_back(fac.leader);
					people.insert(people.end(), fac.captains.begin(), fac.captains.end());
					people.insert(people.end(), fac.lieutenants.begin(), fac.lieutenants.end());
					Show(w.FactionReport(f), { "Hierarchy", "Back" }, [f, people](int p) {
						if (p == 0) {
							ShowPeople(std::string(bf::text::FactionName(f)), people, 0, []() { ShowFactions(); });
						} else {
							ShowFactions();
						}
					});
				} else {
					ShowMain();
				}
			});
		}

		void ShowChronicle(std::size_t page)
		{
			const auto& c = S().world.Chronicle();
			if (c.empty()) {
				Show("The chronicle is empty. Go make some enemies.", { "Back" }, [](int) { ShowMain(); });
				return;
			}
			const std::size_t perPage = 8;
			const std::size_t pages = (c.size() + perPage - 1) / perPage;
			page = std::min(page, pages - 1);
			std::string body = bf::text::Fmt("Chronicle ({0}/{1})\n", { std::to_string(page + 1), std::to_string(pages) });
			// Newest first.
			for (std::size_t i = 0; i < perPage; ++i) {
				const std::size_t idx = page * perPage + i;
				if (idx >= c.size()) break;
				const auto& e = c[c.size() - 1 - idx];
				body += bf::text::Fmt("\nDay {0}: {1}", { std::to_string(e.day), e.text });
			}
			std::vector<std::string> buttons;
			const bool older = page + 1 < pages;
			const bool newer = page > 0;
			if (older) buttons.emplace_back("Older");
			if (newer) buttons.emplace_back("Newer");
			buttons.emplace_back("Back");
			Show(body, buttons, [=](int pick) {
				int i = 0;
				if (older && pick == i++) return ShowChronicle(page + 1);
				if (newer && pick == i++) return ShowChronicle(page - 1);
				ShowMain();
			});
		}

		void ShowLeads()
		{
			auto& w = S().world;
			std::string body = "Leads you are following\n";
			if (w.Leads().empty()) body += "\nNone. Make someone talk.";
			for (const auto& l : w.Leads()) {
				const bf::Npc* t = w.Get(l.target);
				if (!t) continue;
				body += bf::text::Fmt("\n{0} - in {1} until day {2}", { t->FullName(), bf::text::HoldName(l.hold), std::to_string(l.expiresDay) });
			}
			body += "\n\nTravel the wilds of that hold to find them.";
			Show(body, { "Back" }, [](int) { ShowMain(); });
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
		auto& w = s.world;
		std::string body = bf::text::Fmt("BLOODFEUD - Day {0}\nNemeses: {1}   Known enemies: {2}   Leads: {3}",
			{ std::to_string(game::Today()), std::to_string(w.Nemeses().size()), std::to_string(w.KnownEnemies().size()), std::to_string(w.Leads().size()) });
		for (auto it = w.Chronicle().rbegin(); it != w.Chronicle().rend(); ++it) {
			if (it->kind == bf::ChronicleKind::Faction || it->kind == bf::ChronicleKind::Rumor) {
				body += "\n\nLatest word: " + it->text;
				break;
			}
		}
		Show(body, { "Nemeses", "Known enemies", "Factions", "Chronicle", "Leads", "Close" }, [](int pick) {
			auto& w = S().world;
			switch (pick) {
			case 0: ShowPeople("Those sworn against you", w.Nemeses(), 0, []() { ShowMain(); }); break;
			case 1: ShowPeople("Everyone who remembers you", w.KnownEnemies(), 0, []() { ShowMain(); }); break;
			case 2: ShowFactions(); break;
			case 3: ShowChronicle(0); break;
			case 4: ShowLeads(); break;
			default: break;
			}
		});
	}

	void ShowYield(RE::FormID ref, bf::NpcId id)
	{
		auto& s = S();
		std::scoped_lock l(s.lock);
		const bf::Npc* n = s.world.Get(id);
		if (!n) return;
		std::string body = bf::text::Fmt("{0} yields.\n", { n->FullName() });
		if (n->faction != bf::FactionId::None) body += bf::text::Fmt("{0} of {1}\n", { bf::text::RankName(n->rank, n->faction), bf::text::FactionName(n->faction) });
		body += bf::text::Fmt("{0}. {1}.", { bf::text::TemperName(n->temper), bf::text::TraitList(n->traits) });

		auto spare = [ref, id]() {
			std::scoped_lock l2(S().lock);
			S().world.Spare(id, game::Today(), game::PlaceName(RE::PlayerCharacter::GetSingleton()));
			if (auto* a = RE::TESForm::LookupByID<RE::Actor>(ref)) {
				a->AsActorValueOwner()->SetActorValue(RE::ActorValue::kConfidence, 0.0f);  // run
				a->EvaluatePackage(true, true);
			}
		};
		auto execute = [ref, id]() {
			std::scoped_lock l2(S().lock);
			S().world.Execute(id, game::Today(), game::PlaceName(RE::PlayerCharacter::GetSingleton()));
			if (auto* a = RE::TESForm::LookupByID<RE::Actor>(ref); a && !a->IsDead()) {
				a->KillImpl(RE::PlayerCharacter::GetSingleton(), 10000.0f, true, true);
			}
		};

		Show(body, { "Interrogate", "Spare", "Execute" }, [=](int pick) {
			if (pick == 0) {
				std::string intelText;
				{
					std::scoped_lock l2(S().lock);
					intelText = S().world.Interrogate(id, game::Today()).text;
				}
				Show(intelText + "\n\n(Check Leads in the Bloodfeud menu.)", { "Spare", "Execute" }, [=](int p) { p == 1 ? execute() : spare(); });
			} else if (pick == 2) {
				execute();
			} else {
				spare();
			}
		});
	}
}

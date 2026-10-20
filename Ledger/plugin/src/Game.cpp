#include "Game.h"

#include "BloodfeudAPI.h"
#include "Settings.h"

#include <cmath>
#include <random>

namespace lgp
{
	State& S()
	{
		static State s;
		return s;
	}

	namespace game
	{
		namespace
		{
			RE::TESFaction* g_imperial{ nullptr };
			RE::TESFaction* g_stormcloak{ nullptr };
			std::mt19937 g_rng{ 0x1ED6E4u };
			std::uint32_t g_lastDay{ 0 };

			RE::TESFaction* Resolve(const std::optional<FormRef>& ref, const char* label)
			{
				if (!ref) return nullptr;
				auto* dh = RE::TESDataHandler::GetSingleton();
				auto* form = dh ? dh->LookupForm(ref->localId, ref->plugin) : nullptr;
				auto* f = form ? form->As<RE::TESFaction>() : nullptr;
				if (f) logger::info("{} faction -> {:08X}", label, f->GetFormID());
				else logger::warn("{} faction {}|{:06X} not found; civil-war detection is off for it", label, ref->plugin, ref->localId);
				return f;
			}

			std::string Lower(std::string_view s)
			{
				std::string o(s);
				for (auto& c : o) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
				return o;
			}

			bool HasKw(RE::TESForm* f, std::string_view kw)
			{
				auto* k = f ? f->As<RE::BGSKeywordForm>() : nullptr;
				return k && k->HasKeywordString(kw);
			}

			bool NameHasAny(std::string_view name, std::initializer_list<std::string_view> words)
			{
				const std::string n = Lower(name);
				for (auto w : words) {
					if (n.find(Lower(w)) != std::string::npos) return true;
				}
				return false;
			}

			RE::TESBoundObject* GoldForm() { return RE::TESForm::LookupByID<RE::TESBoundObject>(0xF); }

			int GoldIn(RE::TESObjectREFR* ref)
			{
				if (!ref) return 0;
				int total = 0;
				for (const auto& [obj, n] : ref->GetInventoryCounts([](RE::TESBoundObject& o) { return o.IsGold(); })) total += n;
				return total;
			}

			RE::Actor* CurrentMerchant()
			{
				auto* mtm = RE::MenuTopicManager::GetSingleton();
				if (!mtm) return nullptr;
				auto speaker = mtm->speaker.get();
				return speaker ? speaker->As<RE::Actor>() : nullptr;
			}

			RE::TESObjectREFR* MerchantChest(RE::Actor* merchant)
			{
				auto* faction = merchant ? merchant->GetVendorFaction() : nullptr;
				return faction ? faction->vendorData.merchantContainer : nullptr;
			}

			float Blend(float mult) { return std::clamp(1.0f + (mult - 1.0f) * Cfg().priceStrength, 0.5f, 2.5f); }

			void Reprice(RE::TESBoundObject* obj, float mult)
			{
				auto& s = S();
				if (!obj || s.saved.contains(obj->GetFormID()) || std::abs(mult - 1.0f) < 0.03f) return;
				if (auto* alch = obj->As<RE::AlchemyItem>()) {
					const std::int32_t current = alch->GetGoldValue();
					s.saved[obj->GetFormID()] = { SavedPrice::Kind::Alchemy, alch->data.costOverride, alch->data.flags.underlying() };
					alch->data.costOverride = std::max(1, static_cast<std::int32_t>(std::lround(current * mult)));
					alch->data.flags.set(RE::AlchemyItem::AlchemyFlag::kCostOverride);
				} else if (auto* ingr = obj->As<RE::IngredientItem>()) {
					const std::int32_t current = ingr->GetGoldValue();
					s.saved[obj->GetFormID()] = { SavedPrice::Kind::Ingredient, ingr->data.costOverride, ingr->data.flags.underlying() };
					ingr->data.costOverride = std::max(1, static_cast<std::int32_t>(std::lround(current * mult)));
					ingr->data.flags.set(RE::IngredientItem::IngredientFlag::kCostOverride);
				} else if (auto* vf = obj->As<RE::TESValueForm>()) {
					if (vf->value <= 0) return;
					s.saved[obj->GetFormID()] = { SavedPrice::Kind::Value, vf->value, 0 };
					vf->value = std::max(1, static_cast<std::int32_t>(std::lround(vf->value * mult)));
				}
			}

			void AdjustGold(RE::TESObjectREFR* chest, ledger::Hold hold)
			{
				auto& s = S();
				auto* gold = GoldForm();
				if (!chest || !gold) return;
				const int have = GoldIn(chest);
				auto [it, inserted] = s.baseGold.try_emplace(chest->GetFormID(), have);
				// Vanilla restocks the chest to its normal amount every couple of days;
				// if we see more than we remembered, that's the new normal.
				if (!inserted && have > it->second * 2) it->second = have;
				const int target = static_cast<int>(std::lround(static_cast<float>(it->second) * S().econ.MerchantGoldMult(hold)));
				const int delta = target - have;
				if (std::abs(delta) < std::max(25, have / 10)) return;
				if (delta > 0) chest->AddObjectToContainer(gold, nullptr, delta, nullptr);
				else chest->RemoveItem(gold, -delta, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
			}

			void ShiftStock(RE::TESObjectREFR* chest, ledger::Hold hold)
			{
				auto& s = S();
				if (!chest) return;
				const std::uint32_t today = Today();
				if (auto it = s.stockDay.find(chest->GetFormID()); it != s.stockDay.end() && it->second == today) return;
				s.stockDay[chest->GetFormID()] = today;

				std::uniform_real_distribution<float> roll(0.0f, 1.0f);
				int added = 0, removed = 0;
				for (const auto& [obj, count] : chest->GetInventoryCounts()) {
					if (!obj || obj->IsGold() || count <= 0) continue;
					const auto cat = Categorize(obj);
					if (!cat) continue;
					const float level = s.econ.StockLevel(hold, *cat);
					if (level > 0.35f && added < 4 && roll(g_rng) < level) {
						chest->AddObjectToContainer(obj, nullptr, std::max(1, count / 2), nullptr);
						++added;
					} else if (level < -0.35f && removed < 4 && roll(g_rng) < -level) {
						const int take = std::max(1, static_cast<int>(static_cast<float>(count) * std::min(0.7f, -level)));
						chest->RemoveItem(obj, take, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
						++removed;
					}
				}
				if (added || removed) logger::info("Stock shift in {}: +{} / -{} stacks", ledger::HoldName(hold), added, removed);
			}

			void Notify(std::string text)
			{
				if (Cfg().notifications) S().pendingNotices.push_back(std::move(text));
			}

			bool Throttle(std::unordered_map<std::uint32_t, std::uint32_t>& m, ledger::Hold h)
			{
				const auto key = static_cast<std::uint32_t>(h);
				const auto today = Today();
				if (auto it = m.find(key); it != m.end() && it->second == today) return true;
				m[key] = today;
				return false;
			}
		}

		void ResolveForms()
		{
			g_imperial = Resolve(Cfg().imperialFaction, "Imperial");
			g_stormcloak = Resolve(Cfg().stormcloakFaction, "Stormcloak");
		}

		std::uint32_t Today()
		{
			auto* cal = RE::Calendar::GetSingleton();
			return cal ? static_cast<std::uint32_t>(cal->GetDaysPassed()) + 1 : 1;
		}

		ledger::Hold HoldOf(RE::TESObjectREFR* ref)
		{
			auto* loc = ref ? ref->GetCurrentLocation() : nullptr;
			for (int i = 0; loc && i < 8; ++i, loc = loc->parentLoc) {
				if (const char* n = loc->GetFullName(); n && *n) {
					if (auto h = ledger::HoldFromLocationName(n); h != ledger::Hold::Unknown) return h;
				}
			}
			return ledger::Hold::Unknown;
		}

		bool InSettlement(RE::TESObjectREFR* ref)
		{
			auto* loc = ref ? ref->GetCurrentLocation() : nullptr;
			for (int i = 0; loc && i < 4; ++i, loc = loc->parentLoc) {
				if (loc->HasKeywordString("LocTypeCity") || loc->HasKeywordString("LocTypeTown") ||
					loc->HasKeywordString("LocTypeSettlement") || loc->HasKeywordString("LocTypeHabitation")) {
					return true;
				}
			}
			return false;
		}

		std::optional<ledger::Good> Categorize(RE::TESBoundObject* obj)
		{
			using G = ledger::Good;
			if (!obj) return std::nullopt;
			switch (obj->GetFormType()) {
			case RE::FormType::Weapon:
			case RE::FormType::Ammo:
				return G::Arms;
			case RE::FormType::Armor:
				if (HasKw(obj, "ArmorJewelry")) return G::Luxury;
				if (HasKw(obj, "ArmorHeavy") || HasKw(obj, "ArmorLight") || HasKw(obj, "ArmorShield")) return G::Arms;
				return std::nullopt;
			case RE::FormType::AlchemyItem:
				if (auto* alch = obj->As<RE::AlchemyItem>(); alch && alch->IsFood()) {
					const char* name = obj->GetName();
					if (name && NameHasAny(name, { "mead", "ale", "wine", "beer", "brandy", "sujamma", "flin", "shein", "matze", "spiced" })) return G::Drink;
					return G::Food;
				}
				return G::Alchemy;
			case RE::FormType::Ingredient:
				return G::Alchemy;
			case RE::FormType::SoulGem:
				return G::Luxury;
			case RE::FormType::Misc:
				if (HasKw(obj, "VendorItemOreIngot")) return G::Ore;
				if (HasKw(obj, "VendorItemGem")) return G::Luxury;
				if (HasKw(obj, "VendorItemAnimalHide") || HasKw(obj, "VendorItemAnimalPart")) return G::Pelts;
				if (HasKw(obj, "VendorItemFirewood") || (obj->GetName() && NameHasAny(obj->GetName(), { "firewood" }))) return G::Lumber;
				return std::nullopt;
			default:
				return std::nullopt;
			}
		}

		int PlayerGold() { return GoldIn(RE::PlayerCharacter::GetSingleton()); }

		void TakeGold(int amount)
		{
			if (auto* gold = GoldForm(); gold && amount > 0) {
				RE::PlayerCharacter::GetSingleton()->RemoveItem(gold, amount, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
			}
		}

		void GiveGold(int amount)
		{
			if (auto* gold = GoldForm(); gold && amount > 0) {
				RE::PlayerCharacter::GetSingleton()->AddObjectToContainer(gold, nullptr, amount, nullptr);
			}
		}

		// ---- barter -------------------------------------------------------

		void ApplyForMerchant()
		{
			auto& s = S();
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* merchant = CurrentMerchant();
			auto* chest = MerchantChest(merchant);
			if (!merchant || !chest) return;  // not a merchant
			s.barterHold = HoldOf(merchant);
			if (s.barterHold == ledger::Hold::Unknown) s.barterHold = HoldOf(player);
			if (s.barterHold == ledger::Hold::Unknown) return;
			s.applied = true;

			// Stock and gold first, so re-pricing sees the final inventory.
			if (Cfg().shiftStock) ShiftStock(chest, s.barterHold);
			if (Cfg().scaleMerchantGold) AdjustGold(chest, s.barterHold);

			if (Cfg().scalePrices) {
				auto apply = [&](RE::TESObjectREFR* ref) {
					if (!ref) return;
					for (const auto& [obj, count] : ref->GetInventoryCounts()) {
						if (!obj || obj->IsGold() || count <= 0) continue;
						if (auto cat = Categorize(obj)) Reprice(obj, Blend(s.econ.PriceMult(s.barterHold, *cat)));
					}
				};
				apply(player);
				apply(chest);
				apply(merchant);
			}
			logger::info("Trading in {}: {} item types re-priced", ledger::HoldName(s.barterHold), s.saved.size());
		}

		void OnMenu(std::string_view menu, bool opening)
		{
			auto& s = S();
			std::scoped_lock l(s.lock);
			if (menu == RE::BarterMenu::MENU_NAME) s.barterOpen = opening;
			else if (menu == RE::DialogueMenu::MENU_NAME) s.dialogueOpen = opening;
			else return;

			if (opening && !s.applied) {
				ApplyForMerchant();
			} else if (!s.dialogueOpen && !s.barterOpen && s.applied) {
				RestorePrices();
				s.applied = false;
			}
		}

		void RestorePrices()
		{
			auto& s = S();
			for (const auto& [id, sv] : s.saved) {
				auto* form = RE::TESForm::LookupByID(id);
				if (!form) continue;
				switch (sv.kind) {
				case SavedPrice::Kind::Alchemy:
					if (auto* a = form->As<RE::AlchemyItem>()) {
						a->data.costOverride = sv.value;
						a->data.flags = static_cast<RE::AlchemyItem::AlchemyFlag>(sv.flags);
					}
					break;
				case SavedPrice::Kind::Ingredient:
					if (auto* i = form->As<RE::IngredientItem>()) {
						i->data.costOverride = sv.value;
						i->data.flags = static_cast<RE::IngredientItem::IngredientFlag>(sv.flags);
					}
					break;
				default:
					if (auto* vf = form->As<RE::TESValueForm>()) vf->value = sv.value;
					break;
				}
			}
			s.saved.clear();
		}

		void OnContainerChanged(const RE::TESContainerChangedEvent& e)
		{
			auto& s = S();
			if (!s.barterOpen || s.barterHold == ledger::Hold::Unknown) return;
			constexpr RE::FormID kPlayer = 0x14;
			const bool sold = e.oldContainer == kPlayer && e.newContainer != 0;
			const bool bought = e.newContainer == kPlayer && e.oldContainer != 0;
			if (!sold && !bought) return;
			auto* obj = RE::TESForm::LookupByID<RE::TESBoundObject>(e.baseObj);
			if (!obj || obj->IsGold()) return;
			const auto cat = Categorize(obj);
			if (!cat) return;
			std::scoped_lock l(s.lock);
			s.econ.OnPlayerTrade(s.barterHold, *cat, e.itemCount, sold, obj->GetGoldValue());
		}

		// ---- world events -------------------------------------------------------

		void OnCombat(RE::Actor* actor, RE::Actor* target)
		{
			if (!actor || !target) return;
			auto& s = S();
			std::scoped_lock l(s.lock);
			if (Cfg().civilWar && g_imperial && g_stormcloak) {
				const bool battle = (actor->IsInFaction(g_imperial) && target->IsInFaction(g_stormcloak)) ||
				                    (actor->IsInFaction(g_stormcloak) && target->IsInFaction(g_imperial));
				if (battle) {
					const auto h = HoldOf(actor);
					if (h != ledger::Hold::Unknown && !Throttle(s.lastBattle, h)) s.econ.OnBattle(h);
				}
			}
			if (Cfg().dragons && actor->HasKeywordString("ActorTypeDragon")) {
				auto* player = RE::PlayerCharacter::GetSingleton();
				const auto h = HoldOf(player);
				if (h != ledger::Hold::Unknown && !Throttle(s.lastDragon, h)) s.econ.OnDragon(h, InSettlement(player));
			}
		}

		void OnDeath(RE::Actor* dying, RE::Actor* killer)
		{
			if (!dying) return;
			auto& s = S();
			std::scoped_lock l(s.lock);
			const auto h = HoldOf(dying);
			if (h == ledger::Hold::Unknown) return;
			if (dying->GetVendorFaction()) {
				s.econ.OnMerchantKilled(h);
				return;
			}
			if (killer && (killer->IsPlayerRef() || killer->IsPlayerTeammate()) && !s.econ.LinkedToBloodfeud()) {
				const std::string name = Lower(dying->GetName() ? dying->GetName() : "");
				for (const auto& w : Cfg().outlawNames) {
					if (!w.empty() && name.find(Lower(w)) != std::string::npos) {
						s.econ.OnOutlawKilled(h);
						break;
					}
				}
			}
		}

		void OnBloodfeud(SKSE::MessagingInterface::Message* msg)
		{
			if (!msg || !msg->data) return;
			auto& s = S();
			std::scoped_lock l(s.lock);
			if (msg->type == BloodfeudAPI::kHoldStatus && msg->dataLen == sizeof(BloodfeudAPI::HoldStatus)) {
				const auto* st = static_cast<const BloodfeudAPI::HoldStatus*>(msg->data);
				if (st->version != 1) return;
				for (std::uint32_t h = 0; h < BloodfeudAPI::kHoldCount; ++h) {
					s.econ.SetRoadDanger(static_cast<ledger::Hold>(h), st->roadDanger[h]);
				}
			} else if (msg->type == BloodfeudAPI::kWorldEvent && msg->dataLen == sizeof(BloodfeudAPI::WorldEvent)) {
				const auto* ev = static_cast<const BloodfeudAPI::WorldEvent*>(msg->data);
				if (ev->version != 1) return;
				const auto h = static_cast<ledger::Hold>(ev->hold);
				switch (ev->kind) {
				case BloodfeudAPI::EventKind::OfficerSlain: s.econ.OnOfficerSlain(h, ev->magnitude); break;
				case BloodfeudAPI::EventKind::HoldSeized: s.econ.OnHoldSeized(h); break;
				case BloodfeudAPI::EventKind::Skirmish: s.econ.OnSkirmish(h, ev->magnitude); break;
				default: break;
				}
			}
		}

		void Tick()
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* ui = RE::UI::GetSingleton();
			if (!player || !player->Is3DLoaded() || !ui || ui->GameIsPaused()) return;
			auto& s = S();
			std::scoped_lock l(s.lock);

			auto* cal = RE::Calendar::GetSingleton();
			const std::uint32_t today = Today();
			if (cal && today != g_lastDay) {
				g_lastDay = today;
				s.econ.TickDay(today, cal->GetMonth());
				if (const int due = s.econ.CollectDividends(); due > 0) GiveGold(due);
			}
			for (auto& n : s.econ.DrainNotices()) Notify(std::move(n));
			if (!s.pendingNotices.empty()) {
				RE::DebugNotification(s.pendingNotices.front().c_str(), nullptr, false);
				s.pendingNotices.erase(s.pendingNotices.begin());
			}
		}
	}
}

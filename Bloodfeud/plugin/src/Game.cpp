#include "Game.h"

#include "Settings.h"
#include "UI.h"
#include "BloodfeudAPI.h"
#include "bloodfeud/Text.hpp"

#include <cmath>
#include <numbers>

namespace bfp
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
			struct GroupForms
			{
				RE::TESFaction* bandit{ nullptr };
				RE::TESFaction* forsworn{ nullptr };
				RE::TESFaction* silverHand{ nullptr };
				RE::TESFaction* thalmor{ nullptr };
				RE::TESFaction* vampire{ nullptr };
				RE::TESFaction* necromancer{ nullptr };
			} g_forms;

			RE::TESFaction* Resolve(const std::optional<FormRef>& ref, const char* label)
			{
				if (!ref) return nullptr;
				auto* dh = RE::TESDataHandler::GetSingleton();
				auto* form = dh ? dh->LookupForm(ref->localId, ref->plugin) : nullptr;
				auto* faction = form ? form->As<RE::TESFaction>() : nullptr;
				if (faction) {
					logger::info("{} faction -> {:08X} '{}'", label, faction->GetFormID(), faction->GetName());
				} else {
					logger::warn("{} faction {}|{:06X} not found or not a faction; using name matching only", label, ref->plugin, ref->localId);
				}
				return faction;
			}

			std::string Lower(std::string_view s)
			{
				std::string out(s);
				for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
				return out;
			}

			bool NameHas(const std::string& lowerName, const std::vector<std::string>& words)
			{
				for (const auto& w : words) {
					if (!w.empty() && lowerName.find(Lower(w)) != std::string::npos) return true;
				}
				return false;
			}

			bf::GameGroup GroupOf(RE::Actor* a)
			{
				auto in = [&](RE::TESFaction* f) { return f && a->IsInFaction(f); };
				if (in(g_forms.forsworn)) return bf::GameGroup::Forsworn;
				if (in(g_forms.silverHand)) return bf::GameGroup::SilverHand;
				if (in(g_forms.thalmor)) return bf::GameGroup::Thalmor;
				if (in(g_forms.vampire)) return bf::GameGroup::Vampire;
				if (in(g_forms.necromancer)) return bf::GameGroup::Necromancer;
				if (in(g_forms.bandit)) return bf::GameGroup::Bandit;

				const auto& c = Cfg();
				const std::string name = Lower(a->GetName());
				if (NameHas(name, c.forswornNames)) return bf::GameGroup::Forsworn;
				if (NameHas(name, c.silverHandNames)) return bf::GameGroup::SilverHand;
				if (NameHas(name, c.thalmorNames)) return bf::GameGroup::Thalmor;
				if (NameHas(name, c.vampireNames)) return bf::GameGroup::Vampire;
				if (NameHas(name, c.necromancerNames)) return bf::GameGroup::Necromancer;
				if (NameHas(name, c.banditNames)) return bf::GameGroup::Bandit;
				return bf::GameGroup::Other;
			}

			// A base form we can PlaceAtMe later to bring this person back.
			RE::FormID SpawnBase(RE::Actor* a)
			{
				if (auto* lvl = a->extraList.GetByType<RE::ExtraLeveledCreature>()) {
					if (lvl->templateBase && !lvl->templateBase->IsDynamicForm()) return lvl->templateBase->GetFormID();
					if (lvl->originalBase && !lvl->originalBase->IsDynamicForm()) return lvl->originalBase->GetFormID();
				}
				auto* base = a->GetActorBase();
				return base && !base->IsDynamicForm() ? base->GetFormID() : 0;
			}

			std::optional<RE::ActorValue> ToAV(bf::Stat s)
			{
				using AV = RE::ActorValue;
				switch (s) {
				case bf::Stat::Health: return AV::kHealth;
				case bf::Stat::Stamina: return AV::kStamina;
				case bf::Stat::Magicka: return AV::kMagicka;
				case bf::Stat::DamageResist: return AV::kDamageResist;
				case bf::Stat::ResistFire: return AV::kResistFire;
				case bf::Stat::ResistFrost: return AV::kResistFrost;
				case bf::Stat::ResistShock: return AV::kResistShock;
				case bf::Stat::ResistMagic: return AV::kResistMagic;
				case bf::Stat::SpeedMult: return AV::kSpeedMult;
				case bf::Stat::AttackDamageMult: return AV::kAttackDamageMult;
				case bf::Stat::OneHanded: return AV::kOneHanded;
				case bf::Stat::TwoHanded: return AV::kTwoHanded;
				case bf::Stat::Archery: return AV::kArchery;
				case bf::Stat::Block: return AV::kBlock;
				}
				return std::nullopt;
			}

			RE::Actor* PlaceNear(RE::PlayerCharacter* player, RE::TESBoundObject* base, float angleOffset, float distance)
			{
				auto ref = player->PlaceObjectAtMe(base, true);
				auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
				if (!actor) return nullptr;
				// Behind the player so they come running in rather than popping into view.
				const float ang = player->GetAngleZ() + std::numbers::pi_v<float> + angleOffset;
				RE::NiPoint3 pos = player->GetPosition();
				pos.x += std::sin(ang) * distance;
				pos.y += std::cos(ang) * distance;
				pos.z += 96.0f;  // let them drop onto the ground
				ref->SetPosition(pos);  // REFR overload; Actor hides it
				auto* av = actor->AsActorValueOwner();
				av->SetActorValue(RE::ActorValue::kAggression, 2.0f);  // very aggressive
				av->SetActorValue(RE::ActorValue::kConfidence, 3.0f);  // brave
				actor->EvaluatePackage(true, false);
				return actor;
			}

			void Despawn(RE::TESObjectREFR* ref)
			{
				if (!ref) return;
				ref->Disable();
				ref->SetDelete(true);
			}

			void CleanupSpawned()
			{
				auto* player = RE::PlayerCharacter::GetSingleton();
				auto& s = S();
				const float now = DaysPassed();
				std::erase_if(s.spawned, [&](const Spawned& sp) {
					auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(sp.ref);
					if (!ref || ref->IsDisabled()) {
						if (sp.npc) s.world.Unbind(sp.npc);
						return true;
					}
					auto* actor = ref->As<RE::Actor>();
					const bool sameSpace = ref->GetWorldspace() == player->GetWorldspace() && ref->GetParentCell() && player->GetParentCell();
					const float dist = sameSpace ? player->GetPosition().GetDistance(ref->GetPosition()) : 1e9f;
					const bool fighting = actor && !actor->IsDead() && actor->IsInCombat();
					const bool stale = now - sp.dayCreated > 2.0f;
					if ((dist > Cfg().despawnDistance && !fighting) || stale) {
						if (sp.npc) {
							// Still alive and the player got away: they live to hunt again.
							if (actor && !actor->IsDead()) s.world.OnSurvived(sp.npc, Today(), PlaceName(player));
							s.world.Unbind(sp.npc);
						}
						s.buffed.erase(sp.ref);
						Despawn(ref);
						return true;
					}
					return false;
				});
			}

			// A nemesis met in a dungeon who is no longer loaded leaves to hunt the
			// player; disable the original so they can't be in two places at once.
			void ReleaseUnloadedNemeses()
			{
				auto& s = S();
				for (bf::NpcId id : s.world.Nemeses()) {
					bf::Npc* n = s.world.Get(id);
					if (!n || !n->refId) continue;
					const bool ours = std::ranges::any_of(s.spawned, [&](const Spawned& sp) { return sp.ref == n->refId; });
					if (ours) continue;
					auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(n->refId);
					if (ref && ref->Is3DLoaded()) continue;
					if (ref) {
						auto* actor = ref->As<RE::Actor>();
						if (actor && !actor->IsDead()) ref->Disable();
					}
					s.buffed.erase(n->refId);
					s.world.Unbind(id);
				}
			}
		}

		// ---- messages for other mods (see api/BloodfeudAPI.h) ----------------
		void BroadcastStatus()
		{
			auto& w = S().world;
			BloodfeudAPI::HoldStatus st;
			for (std::uint32_t h = 0; h < BloodfeudAPI::kHoldCount; ++h) {
				st.roadDanger[h] = w.RoadDanger(static_cast<bf::Hold>(h));
				st.controller[h] = static_cast<std::uint8_t>(w.ControllerOf(static_cast<bf::Hold>(h), false));
			}
			SKSE::GetMessagingInterface()->Dispatch(BloodfeudAPI::kHoldStatus, &st, sizeof(st), nullptr);
		}

		void BroadcastEvents()
		{
			for (const auto& e : S().world.DrainEvents()) {
				BloodfeudAPI::WorldEvent ev;
				ev.kind = static_cast<BloodfeudAPI::EventKind>(e.kind);
				ev.hold = static_cast<std::uint8_t>(e.hold);
				ev.faction = static_cast<std::uint8_t>(e.faction);
				ev.magnitude = e.magnitude;
				SKSE::GetMessagingInterface()->Dispatch(BloodfeudAPI::kWorldEvent, &ev, sizeof(ev), nullptr);
			}
		}

		void ResolveFactions()
		{
			const auto& c = Cfg();
			g_forms.bandit = Resolve(c.banditFaction, "Bandit");
			g_forms.forsworn = Resolve(c.forswornFaction, "Forsworn");
			g_forms.silverHand = Resolve(c.silverHandFaction, "Silver Hand");
			g_forms.thalmor = Resolve(c.thalmorFaction, "Thalmor");
			g_forms.vampire = Resolve(c.vampireFaction, "Vampire");
			g_forms.necromancer = Resolve(c.necromancerFaction, "Necromancer");
		}

		float DaysPassed()
		{
			auto* cal = RE::Calendar::GetSingleton();
			return cal ? cal->GetDaysPassed() : 0.0f;
		}

		std::uint32_t Today() { return static_cast<std::uint32_t>(DaysPassed()) + 1; }

		bf::Hold HoldOf(RE::TESObjectREFR* ref)
		{
			auto* loc = ref ? ref->GetCurrentLocation() : nullptr;
			for (int i = 0; loc && i < 8; ++i, loc = loc->parentLoc) {
				if (const char* name = loc->GetFullName(); name && *name) {
					if (auto h = bf::text::HoldFromLocationName(name); h != bf::Hold::Unknown) return h;
				}
			}
			return bf::Hold::Unknown;
		}

		std::string PlaceName(RE::TESObjectREFR* ref)
		{
			if (!ref) return "the wilds";
			if (auto* loc = ref->GetCurrentLocation(); loc && loc->GetFullName() && *loc->GetFullName()) return loc->GetFullName();
			if (auto* cell = ref->GetParentCell(); cell && cell->GetFullName() && *cell->GetFullName()) return cell->GetFullName();
			return std::string(bf::text::HoldName(HoldOf(ref)));
		}

		bool InSettlement(RE::TESObjectREFR* ref)
		{
			auto* loc = ref ? ref->GetCurrentLocation() : nullptr;
			for (int i = 0; loc && i < 4; ++i, loc = loc->parentLoc) {
				if (loc->HasKeywordString("LocTypeCity") || loc->HasKeywordString("LocTypeTown") ||
					loc->HasKeywordString("LocTypeSettlement") || loc->HasKeywordString("LocTypeHabitation") ||
					loc->HasKeywordString("LocTypePlayerHouse")) {
					return true;
				}
			}
			return false;
		}

		bool IsPlayerSide(RE::Actor* a)
		{
			return a && (a->IsPlayerRef() || a->IsPlayerTeammate() || (a->IsCommandedActor()));
		}

		void Notify(std::string text)
		{
			S().pendingNotices.push_back(std::move(text));
		}

		void Present(RE::Actor* actor, const bf::Npc& npc)
		{
			auto& s = S();
			if (Cfg().renameEnemies) {
				actor->SetDisplayName(npc.ShortName().c_str(), true);
			}
			const bool worthBuffing = Cfg().buffGrunts || npc.rank > bf::Rank::Grunt || npc.nemesis;
			if (!worthBuffing || s.buffed.contains(actor->GetFormID())) return;
			s.buffed.insert(actor->GetFormID());
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* av = actor->AsActorValueOwner();
			for (const auto& m : s.world.StatModsFor(npc, player->GetLevel(), actor->GetLevel())) {
				if (auto v = ToAV(m.stat)) av->ModActorValue(*v, m.delta);
			}
			av->RestoreActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, RE::ActorValue::kHealth, 100000.0f);
		}

		bf::NpcId Track(RE::Actor* a)
		{
			auto& s = S();
			if (!a) return bf::kNoNpc;
			if (const bf::NpcId known = s.world.FindByRef(a->GetFormID())) {
				if (const bf::Npc* n = s.world.Get(known)) Present(a, *n);
				return known;
			}
			if (a->IsPlayerRef() || a->IsDead() || a->IsPlayerTeammate() || a->IsCommandedActor() || a->IsSummoned() || a->IsChild()) return bf::kNoNpc;
			if (!a->HasKeywordString("ActorTypeNPC")) return bf::kNoNpc;
			auto* base = a->GetActorBase();
			if (!base || base->IsUnique() || a->IsEssential()) return bf::kNoNpc;

			bf::ActorInfo info;
			info.group = GroupOf(a);
			if (info.group == bf::GameGroup::Other) return bf::kNoNpc;
			info.refId = a->GetFormID();
			info.baseId = SpawnBase(a);
			if (auto* race = a->GetRace()) info.raceName = race->GetFullName() ? race->GetFullName() : "";
			info.female = base->GetSex() == RE::SEX::kFemale;
			info.level = a->GetLevel();
			info.hold = HoldOf(a);
			if (info.hold == bf::Hold::Unknown) info.hold = HoldOf(RE::PlayerCharacter::GetSingleton());
			info.place = PlaceName(a);
			info.boss = NameHas(Lower(a->GetName()), Cfg().bossNames);

			const bf::NpcId id = s.world.Register(info, Today());
			if (const bf::Npc* n = s.world.Get(id)) {
				Present(a, *n);
				logger::info("Tracking {:08X} as #{} {} ({})", info.refId, id, n->FullName(), bf::text::FactionName(n->faction));
			}
			return id;
		}

		bf::DamageType DamageTypeOf(RE::FormID source)
		{
			auto* form = RE::TESForm::LookupByID(source);
			if (!form) return bf::DamageType::Physical;
			if (auto* weap = form->As<RE::TESObjectWEAP>()) {
				return weap->IsBow() || weap->IsCrossbow() ? bf::DamageType::Arrow : bf::DamageType::Physical;
			}
			if (auto* magic = form->As<RE::MagicItem>()) {
				for (auto* eff : magic->effects) {
					if (!eff || !eff->baseEffect) continue;
					switch (eff->baseEffect->data.resistVariable) {
					case RE::ActorValue::kResistFire: return bf::DamageType::Fire;
					case RE::ActorValue::kResistFrost: return bf::DamageType::Frost;
					case RE::ActorValue::kResistShock: return bf::DamageType::Shock;
					default: break;
					}
				}
			}
			return bf::DamageType::Physical;
		}

		void Yield(RE::Actor* actor, bf::NpcId id)
		{
			actor->StopCombat();
			auto* av = actor->AsActorValueOwner();
			av->SetActorValue(RE::ActorValue::kAggression, 0.0f);
			actor->EvaluatePackage(true, true);
			const RE::FormID ref = actor->GetFormID();
			SKSE::GetTaskInterface()->AddTask([ref, id]() { ui::ShowYield(ref, id); });
		}

		void StageAmbush(const bf::AmbushPlan& plan)
		{
			auto& s = S();
			auto* player = RE::PlayerCharacter::GetSingleton();
			bf::Npc* leader = s.world.Get(plan.leader);
			if (!leader) return;
			auto* base = RE::TESForm::LookupByID<RE::TESBoundObject>(leader->baseId);
			if (!base) {
				logger::warn("Ambush skipped: base {:08X} for {} is missing", leader->baseId, leader->FullName());
				return;
			}
			const float dist = Cfg().spawnDistance;
			const float today = DaysPassed();

			auto* actor = PlaceNear(player, base, 0.0f, dist);
			if (!actor) return;
			s.world.OnAmbushSpawned(plan.leader, actor->GetFormID(), Today());
			s.spawned.push_back({ actor->GetFormID(), plan.leader, today });
			Present(actor, *s.world.Get(plan.leader));
			logger::info("Ambush: {} ({:08X}) with {} escorts{}", leader->FullName(), actor->GetFormID(), plan.escorts, plan.trap ? " [trap]" : "");

			for (int i = 0; i < plan.escorts; ++i) {
				const float spread = (i % 2 ? 1.0f : -1.0f) * (0.35f + 0.2f * static_cast<float>(i / 2));
				if (auto* e = PlaceNear(player, base, spread, dist + 150.0f)) {
					s.spawned.push_back({ e->GetFormID(), bf::kNoNpc, today });
				}
			}
			if (plan.informant) {
				if (bf::Npc* inf = s.world.Get(plan.informant)) {
					if (auto* ib = RE::TESForm::LookupByID<RE::TESBoundObject>(inf->baseId)) {
						if (auto* ia = PlaceNear(player, ib, 0.6f, dist - 200.0f)) {
							s.world.OnAmbushSpawned(plan.informant, ia->GetFormID(), Today());
							s.spawned.push_back({ ia->GetFormID(), plan.informant, today });
							Present(ia, *inf);
						}
					}
				}
			}
			RE::DebugNotification(plan.taunt.c_str(), nullptr, false);  // already "Name: \"line\"" or narration
		}

		void Tick()
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* ui = RE::UI::GetSingleton();
			if (!player || !player->Is3DLoaded() || !ui || ui->GameIsPaused()) return;

			auto& s = S();
			std::scoped_lock l(s.lock);

			for (auto& n : s.world.DrainNotices()) s.pendingNotices.push_back(std::move(n));
			// A couple per second so they don't stomp each other.
			for (int i = 0; i < 2 && !s.pendingNotices.empty(); ++i) {
				RE::DebugNotification(s.pendingNotices.front().c_str(), nullptr, false);
				s.pendingNotices.erase(s.pendingNotices.begin());
			}

			auto* cal = RE::Calendar::GetSingleton();
			if (!cal) return;
			const float hours = cal->GetHoursPassed();
			if (s.lastHours < 0.0f || hours < s.lastHours) {
				s.lastHours = hours;  // new game / loaded an older save
			}
			const float elapsed = hours - s.lastHours;
			if (elapsed >= 1.0f) {
				s.lastHours = hours;
				s.world.TickDay(Today());
				ReleaseUnloadedNemeses();

				auto* cell = player->GetParentCell();
				const bool outdoors = cell && cell->IsExteriorCell();
				const bool safe = !player->IsInCombat() && !player->IsOnMount() && !InSettlement(player) && !s.menuOpen;
				// +1 so the core's day numbers line up with Today().
				if (auto plan = s.world.TickHours(DaysPassed() + 1.0f, std::min(elapsed, 24.0f), player->GetLevel(), HoldOf(player), outdoors, safe)) {
					StageAmbush(*plan);
				}
			}
			CleanupSpawned();

			BroadcastEvents();
			if (Today() != s.lastStatusDay) {
				s.lastStatusDay = Today();
				BroadcastStatus();
			}
		}
	}
}

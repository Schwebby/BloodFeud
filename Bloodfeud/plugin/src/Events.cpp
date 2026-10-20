#include "Events.h"

#include "Game.h"
#include "Settings.h"
#include "UI.h"

namespace bfp::events
{
	namespace
	{
		using Ctl = RE::BSEventNotifyControl;

		class CombatSink : public RE::BSTEventSink<RE::TESCombatEvent>
		{
		public:
			Ctl ProcessEvent(const RE::TESCombatEvent* e, RE::BSTEventSource<RE::TESCombatEvent>*) override
			{
				if (!e || !e->actor) return Ctl::kContinue;
				auto* actor = e->actor->As<RE::Actor>();
				if (!actor || game::IsPlayerSide(actor)) return Ctl::kContinue;
				auto* target = e->targetActor ? e->targetActor->As<RE::Actor>() : nullptr;

				auto& s = S();
				std::scoped_lock l(s.lock);
				const auto state = e->newState.get();
				if (state == RE::ACTOR_COMBAT_STATE::kCombat && game::IsPlayerSide(target)) {
					if (const bf::NpcId id = game::Track(actor)) {
						s.world.OnEngaged(id, game::Today(), game::PlaceName(actor));
					}
				} else if (state == RE::ACTOR_COMBAT_STATE::kNone && !actor->IsDead()) {
					if (const bf::NpcId id = s.world.FindByRef(actor->GetFormID())) {
						s.world.OnSurvived(id, game::Today(), game::PlaceName(actor));
					}
				}
				return Ctl::kContinue;
			}
		};

		class HitSink : public RE::BSTEventSink<RE::TESHitEvent>
		{
		public:
			Ctl ProcessEvent(const RE::TESHitEvent* e, RE::BSTEventSource<RE::TESHitEvent>*) override
			{
				if (!e || !e->target || !e->cause) return Ctl::kContinue;
				auto* target = e->target->As<RE::Actor>();
				auto* cause = e->cause->As<RE::Actor>();
				if (!target || !cause || !game::IsPlayerSide(cause) || target->IsDead()) return Ctl::kContinue;

				auto& s = S();
				std::scoped_lock l(s.lock);
				const bf::NpcId id = game::Track(target);  // sneak attacks count before combat starts
				if (!id) return Ctl::kContinue;
				s.world.OnWounded(id, game::DamageTypeOf(e->source), 10.0f);

				if (Cfg().interrogation && cause->IsPlayerRef()) {
					auto* av = target->AsActorValueOwner();
					const float maxHp = av->GetPermanentActorValue(RE::ActorValue::kHealth);
					const float frac = maxHp > 0.0f ? av->GetActorValue(RE::ActorValue::kHealth) / maxHp : 1.0f;
					if (frac > 0.0f && s.world.RollYield(id, frac)) {
						game::Yield(target, id);
					}
				}
				return Ctl::kContinue;
			}
		};

		class DeathSink : public RE::BSTEventSink<RE::TESDeathEvent>
		{
		public:
			Ctl ProcessEvent(const RE::TESDeathEvent* e, RE::BSTEventSource<RE::TESDeathEvent>*) override
			{
				if (!e || !e->actorDying) return Ctl::kContinue;
				auto& s = S();
				std::scoped_lock l(s.lock);
				const bf::NpcId id = s.world.FindByRef(e->actorDying->GetFormID());
				if (!id) return Ctl::kContinue;  // already handled, or nobody we know
				auto* killer = e->actorKiller ? e->actorKiller->As<RE::Actor>() : nullptr;
				const std::string place = game::PlaceName(e->actorDying.get());
				if (game::IsPlayerSide(killer)) {
					const auto result = s.world.OnKilledByPlayer(id, game::Today(), place);
					if (result == bf::DeathResult::CheatedDeath) {
						logger::info("#{} cheated death at {}", id, place);
					}
				} else {
					s.world.OnKilledByOther(id, game::Today());
				}
				return Ctl::kContinue;
			}
		};

		class InputSink : public RE::BSTEventSink<RE::InputEvent*>
		{
		public:
			Ctl ProcessEvent(RE::InputEvent* const* a_event, RE::BSTEventSource<RE::InputEvent*>*) override
			{
				if (!a_event) return Ctl::kContinue;
				for (auto* e = *a_event; e; e = e->next) {
					if (e->GetEventType() != RE::INPUT_EVENT_TYPE::kButton || e->GetDevice() != RE::INPUT_DEVICE::kKeyboard) continue;
					auto* b = e->AsButtonEvent();
					if (!b || !b->IsDown() || b->GetIDCode() != Cfg().menuKey) continue;
					auto* ui = RE::UI::GetSingleton();
					if (!ui || ui->GameIsPaused() || ui->IsMenuOpen(RE::Console::MENU_NAME) || S().menuOpen) continue;
					SKSE::GetTaskInterface()->AddTask([]() { ui::ShowMain(); });
				}
				return Ctl::kContinue;
			}
		};
	}

	void Install()
	{
		static CombatSink combat;
		static HitSink hit;
		static DeathSink death;
		static InputSink input;
		auto* holder = RE::ScriptEventSourceHolder::GetSingleton();
		holder->AddEventSink<RE::TESCombatEvent>(&combat);
		holder->AddEventSink<RE::TESHitEvent>(&hit);
		holder->AddEventSink<RE::TESDeathEvent>(&death);
		RE::BSInputDeviceManager::GetSingleton()->AddEventSink(&input);
		logger::info("Event sinks installed");
	}
}

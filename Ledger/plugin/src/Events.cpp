#include "Events.h"

#include "Game.h"
#include "Settings.h"
#include "UI.h"

namespace lgp::events
{
	namespace
	{
		using Ctl = RE::BSEventNotifyControl;

		class MenuSink : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
		{
		public:
			Ctl ProcessEvent(const RE::MenuOpenCloseEvent* e, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
			{
				if (e) game::OnMenu(e->menuName.c_str(), e->opening);
				return Ctl::kContinue;
			}
		};

		class ContainerSink : public RE::BSTEventSink<RE::TESContainerChangedEvent>
		{
		public:
			Ctl ProcessEvent(const RE::TESContainerChangedEvent* e, RE::BSTEventSource<RE::TESContainerChangedEvent>*) override
			{
				if (e) game::OnContainerChanged(*e);
				return Ctl::kContinue;
			}
		};

		class CombatSink : public RE::BSTEventSink<RE::TESCombatEvent>
		{
		public:
			Ctl ProcessEvent(const RE::TESCombatEvent* e, RE::BSTEventSource<RE::TESCombatEvent>*) override
			{
				if (e && e->actor && e->targetActor && e->newState.get() == RE::ACTOR_COMBAT_STATE::kCombat) {
					game::OnCombat(e->actor->As<RE::Actor>(), e->targetActor->As<RE::Actor>());
				}
				return Ctl::kContinue;
			}
		};

		class DeathSink : public RE::BSTEventSink<RE::TESDeathEvent>
		{
		public:
			Ctl ProcessEvent(const RE::TESDeathEvent* e, RE::BSTEventSource<RE::TESDeathEvent>*) override
			{
				// The event can fire twice per death (dying, then dead); count it once.
				if (!e || !e->actorDying) return Ctl::kContinue;
				const RE::FormID id = e->actorDying->GetFormID();
				if (std::ranges::find(_recent, id) != _recent.end()) return Ctl::kContinue;
				_recent[_next++ % _recent.size()] = id;
				game::OnDeath(e->actorDying->As<RE::Actor>(), e->actorKiller ? e->actorKiller->As<RE::Actor>() : nullptr);
				return Ctl::kContinue;
			}

		private:
			std::array<RE::FormID, 64> _recent{};
			std::size_t _next{ 0 };
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
		static MenuSink menu;
		static ContainerSink container;
		static CombatSink combat;
		static DeathSink death;
		static InputSink input;
		RE::UI::GetSingleton()->AddEventSink<RE::MenuOpenCloseEvent>(&menu);
		auto* holder = RE::ScriptEventSourceHolder::GetSingleton();
		holder->AddEventSink<RE::TESContainerChangedEvent>(&container);
		holder->AddEventSink<RE::TESCombatEvent>(&combat);
		holder->AddEventSink<RE::TESDeathEvent>(&death);
		RE::BSInputDeviceManager::GetSingleton()->AddEventSink(&input);
		logger::info("Event sinks installed");
	}
}

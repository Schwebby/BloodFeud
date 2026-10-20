#pragma once
#include "bloodfeud/World.hpp"

namespace bfp
{
	// An actor we placed in the world (ambushers). Saved in the co-save so they
	// can be cleaned up after a reload.
	struct Spawned
	{
		RE::FormID ref{ 0 };
		bf::NpcId npc{ bf::kNoNpc };  // 0 for escorts
		float dayCreated{ 0.0f };
	};

	struct State
	{
		std::recursive_mutex lock;
		bf::World world;
		std::vector<Spawned> spawned;
		std::unordered_set<RE::FormID> buffed;  // refs that already got stat mods
		std::vector<std::string> pendingNotices;
		float lastHours{ -1.0f };
		std::uint32_t lastStatusDay{ 0 };  // last day HoldStatus went out to other mods
		bool menuOpen{ false };
	};

	State& S();

	namespace game
	{
		void ResolveFactions();

		std::uint32_t Today();
		float DaysPassed();
		bf::Hold HoldOf(RE::TESObjectREFR* ref);
		std::string PlaceName(RE::TESObjectREFR* ref);
		bool IsPlayerSide(RE::Actor* actor);
		bool InSettlement(RE::TESObjectREFR* ref);

		// Classify + register + rename + buff. Returns 0 for actors we don't track.
		bf::NpcId Track(RE::Actor* actor);
		void Present(RE::Actor* actor, const bf::Npc& npc);
		bf::DamageType DamageTypeOf(RE::FormID source);

		void Yield(RE::Actor* actor, bf::NpcId id);
		void StageAmbush(const bf::AmbushPlan& plan);

		void Notify(std::string text);
		// Runs on the main thread about once per real second.
		void Tick();
	}
}

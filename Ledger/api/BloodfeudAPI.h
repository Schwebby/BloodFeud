#pragma once
// Bloodfeud public message API.
//
// Bloodfeud broadcasts these through the SKSE messaging interface. To listen,
// register during your SKSEPluginLoad:
//
//     SKSE::GetMessagingInterface()->RegisterListener("Bloodfeud", MyCallback);
//
// then check msg->type and msg->dataLen (it must equal sizeof the struct) before
// reading msg->data. Both structs are versioned; only read versions you know.
//
// Hold indices: 0 Haafingar, 1 Hjaalmarch, 2 The Pale, 3 Winterhold, 4 The Reach,
// 5 Whiterun, 6 Eastmarch, 7 Falkreath, 8 The Rift, 255 unknown.
// Faction indices: 0 Forsworn, 1 Silver Hand, 2 Thalmor, 3 Volkihar, 4 Necromancers,
// 5 Bloodhorn Clan, 6 Ashen Hand, 7 Greymane Raiders, 255 none.

#include <cstdint>

namespace BloodfeudAPI
{
	inline constexpr std::uint32_t kHoldCount = 9;

	enum MessageType : std::uint32_t
	{
		kHoldStatus = 0x42464853,  // 'BFHS' - sent once per game day and after a load
		kWorldEvent = 0x42464556,  // 'BFEV' - sent as things happen
	};

	struct HoldStatus
	{
		std::uint32_t version{ 1 };
		float roadDanger[kHoldCount]{};       // 0 (safe) .. 1 (lawless)
		std::uint8_t controller[kHoldCount]{};  // faction index, 255 = none
		std::uint8_t pad[3]{};
	};

	enum class EventKind : std::uint8_t
	{
		OfficerSlain = 0,   // player killed a captain/warchief; magnitude 1 or 2
		HoldSeized = 1,     // a faction took a hold's wilds
		Skirmish = 2,       // factions fought; magnitude ~0.5..1.5
		Ambush = 3,         // player was ambushed
		FactionBroken = 4,  // faction collapsed (hold = 255)
	};

	struct WorldEvent
	{
		std::uint32_t version{ 1 };
		EventKind kind{ EventKind::Skirmish };
		std::uint8_t hold{ 255 };
		std::uint8_t faction{ 255 };
		std::uint8_t pad{ 0 };
		float magnitude{ 1.0f };
	};

	static_assert(sizeof(HoldStatus) == 4 + 36 + 9 + 3);
	static_assert(sizeof(WorldEvent) == 12);
}

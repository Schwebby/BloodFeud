// Ledger - a living economy for Skyrim SE/AE. Works alone, and listens to
// Bloodfeud (if installed) for road danger and outlaw wars.

#include "Events.h"
#include "Game.h"
#include "Settings.h"

namespace
{
	constexpr std::uint32_t kUniqueId = 'LDGR';
	constexpr std::uint32_t kEconRecord = 'ECON';
	constexpr std::uint32_t kGoldRecord = 'GOLD';
	constexpr std::uint32_t kRecordVersion = 1;

	void SetupLog()
	{
		auto dir = SKSE::log::log_directory();
		if (!dir) return;
		auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>((*dir / "Ledger.log").string(), true);
		auto log = std::make_shared<spdlog::logger>("global", std::move(sink));
		log->set_level(spdlog::level::info);
		log->flush_on(spdlog::level::info);
		spdlog::set_default_logger(std::move(log));
		spdlog::set_pattern("[%H:%M:%S] [%l] %v");
	}

	void OnSave(SKSE::SerializationInterface* intfc)
	{
		auto& s = lgp::S();
		std::scoped_lock l(s.lock);
		const auto bytes = s.econ.Save();
		if (intfc->OpenRecord(kEconRecord, kRecordVersion)) {
			const auto size = static_cast<std::uint32_t>(bytes.size());
			intfc->WriteRecordData(size);
			intfc->WriteRecordData(bytes.data(), size);
		}
		if (intfc->OpenRecord(kGoldRecord, kRecordVersion)) {
			const auto n = static_cast<std::uint32_t>(s.baseGold.size());
			intfc->WriteRecordData(n);
			for (const auto& [chest, gold] : s.baseGold) {
				intfc->WriteRecordData(chest);
				intfc->WriteRecordData(gold);
			}
		}
	}

	void Reset(std::uint64_t seed)
	{
		auto& s = lgp::S();
		lgp::game::RestorePrices();
		s.econ = ledger::Economy(lgp::Cfg().core, seed);
		s.baseGold.clear();
		s.stockDay.clear();
		s.pendingNotices.clear();
		s.applied = s.barterOpen = s.dialogueOpen = false;
	}

	void OnLoad(SKSE::SerializationInterface* intfc)
	{
		auto& s = lgp::S();
		std::scoped_lock l(s.lock);
		Reset(0x1ED6E4ull);
		std::uint32_t type, version, length;
		while (intfc->GetNextRecordInfo(type, version, length)) {
			if (version != kRecordVersion) continue;
			if (type == kEconRecord) {
				std::uint32_t size = 0;
				intfc->ReadRecordData(size);
				std::vector<std::uint8_t> bytes(size);
				if (size && intfc->ReadRecordData(bytes.data(), size) == size && s.econ.Load(bytes)) {
					s.econ.GetSettings() = lgp::Cfg().core;
					logger::info("Loaded economy (day {})", s.econ.Day());
				} else {
					logger::error("Economy data unreadable; starting fresh");
				}
			} else if (type == kGoldRecord) {
				std::uint32_t n = 0;
				intfc->ReadRecordData(n);
				for (std::uint32_t i = 0; i < n; ++i) {
					RE::FormID chest = 0;
					std::int32_t gold = 0;
					intfc->ReadRecordData(chest);
					intfc->ReadRecordData(gold);
					RE::FormID resolved = 0;
					if (intfc->ResolveFormID(chest, resolved)) s.baseGold[resolved] = gold;
				}
			}
		}
	}

	void OnRevert(SKSE::SerializationInterface*)
	{
		std::scoped_lock l(lgp::S().lock);
		Reset(static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()));
	}

	std::atomic_bool g_tickerStarted{ false };

	void StartTicker()
	{
		if (g_tickerStarted.exchange(true)) return;
		std::thread([]() {
			for (;;) {
				std::this_thread::sleep_for(1s);
				SKSE::GetTaskInterface()->AddTask([]() { lgp::game::Tick(); });
			}
		}).detach();
	}

	void OnMessage(SKSE::MessagingInterface::Message* msg)
	{
		if (msg->type == SKSE::MessagingInterface::kDataLoaded) {
			lgp::game::ResolveForms();
			lgp::events::Install();
			StartTicker();
		}
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* skse)
{
	SKSE::Init(skse);
	SetupLog();
	logger::info("Ledger loading");

	lgp::Cfg().Load("Data/SKSE/Plugins/Ledger.ini");
	lgp::S().econ = ledger::Economy(lgp::Cfg().core);

	auto* ser = SKSE::GetSerializationInterface();
	ser->SetUniqueID(kUniqueId);
	ser->SetSaveCallback(OnSave);
	ser->SetLoadCallback(OnLoad);
	ser->SetRevertCallback(OnRevert);

	auto* messaging = SKSE::GetMessagingInterface();
	messaging->RegisterListener(OnMessage);
	// Harmless if Bloodfeud isn't installed: no messages will ever arrive and
	// Ledger estimates road danger from vanilla events instead.
	messaging->RegisterListener("Bloodfeud", lgp::game::OnBloodfeud);
	return true;
}

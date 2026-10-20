// Bloodfeud - a Nemesis-style feud and faction-war system for Skyrim SE/AE.
// Inspired by the enRAGE mod for Red Dead Redemption 2.

#include "Events.h"
#include "Game.h"
#include "Settings.h"

namespace
{
	constexpr std::uint32_t kUniqueId = 'BFUD';
	constexpr std::uint32_t kWorldRecord = 'WRLD';
	constexpr std::uint32_t kSpawnRecord = 'SPWN';
	constexpr std::uint32_t kRecordVersion = 1;

	void SetupLog()
	{
		auto dir = SKSE::log::log_directory();
		if (!dir) return;
		auto path = *dir / "Bloodfeud.log";
		auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path.string(), true);
		auto log = std::make_shared<spdlog::logger>("global", std::move(sink));
		log->set_level(spdlog::level::info);
		log->flush_on(spdlog::level::info);
		spdlog::set_default_logger(std::move(log));
		spdlog::set_pattern("[%H:%M:%S] [%l] %v");
	}

	// ---- co-save ------------------------------------------------------------

	void OnSave(SKSE::SerializationInterface* intfc)
	{
		auto& s = bfp::S();
		std::scoped_lock l(s.lock);
		const auto bytes = s.world.Save();
		if (intfc->OpenRecord(kWorldRecord, kRecordVersion)) {
			const auto size = static_cast<std::uint32_t>(bytes.size());
			intfc->WriteRecordData(size);
			intfc->WriteRecordData(bytes.data(), size);
		}
		if (intfc->OpenRecord(kSpawnRecord, kRecordVersion)) {
			const auto n = static_cast<std::uint32_t>(s.spawned.size());
			intfc->WriteRecordData(n);
			for (const auto& sp : s.spawned) {
				intfc->WriteRecordData(sp.ref);
				intfc->WriteRecordData(sp.npc);
				intfc->WriteRecordData(sp.dayCreated);
			}
			const auto b = static_cast<std::uint32_t>(s.buffed.size());
			intfc->WriteRecordData(b);
			for (auto ref : s.buffed) intfc->WriteRecordData(ref);
		}
		logger::info("Saved {} bytes of feud data", bytes.size());
	}

	void OnLoad(SKSE::SerializationInterface* intfc)
	{
		auto& s = bfp::S();
		std::scoped_lock l(s.lock);
		s.world = bf::World(bfp::Cfg().core);
		s.spawned.clear();
		s.buffed.clear();
		s.lastHours = -1.0f;

		auto resolve = [intfc](std::uint32_t id) -> std::uint32_t {
			RE::FormID out = 0;
			return intfc->ResolveFormID(id, out) ? out : 0;
		};

		std::uint32_t type, version, length;
		while (intfc->GetNextRecordInfo(type, version, length)) {
			if (version != kRecordVersion) continue;
			if (type == kWorldRecord) {
				std::uint32_t size = 0;
				intfc->ReadRecordData(size);
				std::vector<std::uint8_t> bytes(size);
				if (size && intfc->ReadRecordData(bytes.data(), size) == size && s.world.Load(bytes)) {
					s.world.GetSettings() = bfp::Cfg().core;  // INI wins over what was saved
					s.world.RemapForms(resolve, resolve);
					logger::info("Loaded feud data: {} people, {} nemeses", s.world.NpcCount(), s.world.Nemeses().size());
				} else {
					logger::error("Feud data was unreadable; starting fresh");
					s.world = bf::World(bfp::Cfg().core);
				}
			} else if (type == kSpawnRecord) {
				std::uint32_t n = 0;
				intfc->ReadRecordData(n);
				for (std::uint32_t i = 0; i < n; ++i) {
					bfp::Spawned sp;
					intfc->ReadRecordData(sp.ref);
					intfc->ReadRecordData(sp.npc);
					intfc->ReadRecordData(sp.dayCreated);
					if ((sp.ref = resolve(sp.ref))) s.spawned.push_back(sp);
				}
				std::uint32_t b = 0;
				intfc->ReadRecordData(b);
				for (std::uint32_t i = 0; i < b; ++i) {
					RE::FormID ref = 0;
					intfc->ReadRecordData(ref);
					if ((ref = resolve(ref))) s.buffed.insert(ref);
				}
			}
		}
	}

	void OnRevert(SKSE::SerializationInterface*)
	{
		auto& s = bfp::S();
		std::scoped_lock l(s.lock);
		const auto seed = static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
		s.world = bf::World(bfp::Cfg().core, seed);
		s.spawned.clear();
		s.buffed.clear();
		s.pendingNotices.clear();
		s.lastHours = -1.0f;
	}

	// ---- ticker ---------------------------------------------------------------

	std::atomic_bool g_tickerStarted{ false };

	void StartTicker()
	{
		if (g_tickerStarted.exchange(true)) return;
		std::thread([]() {
			for (;;) {
				std::this_thread::sleep_for(1s);
				SKSE::GetTaskInterface()->AddTask([]() { bfp::game::Tick(); });
			}
		}).detach();
	}

	void OnMessage(SKSE::MessagingInterface::Message* msg)
	{
		switch (msg->type) {
		case SKSE::MessagingInterface::kDataLoaded:
			bfp::game::ResolveFactions();
			bfp::events::Install();
			StartTicker();
			break;
		case SKSE::MessagingInterface::kNewGame:
		case SKSE::MessagingInterface::kPostLoadGame:
			bfp::S().lastHours = -1.0f;
			bfp::S().lastStatusDay = 0;  // re-announce hold status to listeners
			break;
		default:
			break;
		}
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* skse)
{
	SKSE::Init(skse);
	SetupLog();
	logger::info("Bloodfeud loading");

	bfp::Cfg().Load("Data/SKSE/Plugins/Bloodfeud.ini");
	bfp::S().world = bf::World(bfp::Cfg().core);

	auto* ser = SKSE::GetSerializationInterface();
	ser->SetUniqueID(kUniqueId);
	ser->SetSaveCallback(OnSave);
	ser->SetLoadCallback(OnLoad);
	ser->SetRevertCallback(OnRevert);

	SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
	logger::info("Bloodfeud ready");
	return true;
}

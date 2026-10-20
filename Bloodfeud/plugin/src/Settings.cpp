#include "Settings.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <unordered_map>

namespace bfp
{
	namespace
	{
		std::string Trim(std::string s)
		{
			const auto notSpace = [](unsigned char c) { return !std::isspace(c); };
			s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
			s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
			return s;
		}

		std::string Lower(std::string s)
		{
			std::ranges::transform(s, s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return s;
		}

		std::vector<std::string> SplitList(const std::string& v)
		{
			std::vector<std::string> out;
			std::string cur;
			for (char c : v) {
				if (c == ',') {
					if (auto t = Trim(cur); !t.empty()) out.push_back(t);
					cur.clear();
				} else {
					cur += c;
				}
			}
			if (auto t = Trim(cur); !t.empty()) out.push_back(t);
			return out;
		}

		// "Skyrim.esm|0x01BCC0" -> FormRef. Blank or malformed -> nullopt.
		std::optional<FormRef> ParseForm(const std::string& v)
		{
			const auto bar = v.find('|');
			if (bar == std::string::npos) return std::nullopt;
			FormRef r;
			r.plugin = Trim(v.substr(0, bar));
			const std::string id = Trim(v.substr(bar + 1));
			if (r.plugin.empty() || id.empty()) return std::nullopt;
			try {
				r.localId = static_cast<std::uint32_t>(std::stoul(id, nullptr, 16)) & 0x00FFFFFF;
			} catch (...) {
				return std::nullopt;
			}
			return r;
		}
	}

	Config& Cfg()
	{
		static Config c;
		return c;
	}

	void Config::Load(const std::filesystem::path& path)
	{
		std::ifstream in(path);
		if (!in) return;  // defaults

		std::unordered_map<std::string, std::string> kv;  // "section.key" -> value
		std::string section;
		std::string line;
		while (std::getline(in, line)) {
			if (const auto c = line.find_first_of(";#"); c != std::string::npos) line.erase(c);
			line = Trim(line);
			if (line.empty()) continue;
			if (line.front() == '[' && line.back() == ']') {
				section = Lower(Trim(line.substr(1, line.size() - 2)));
				continue;
			}
			const auto eq = line.find('=');
			if (eq == std::string::npos) continue;
			kv[section + "." + Lower(Trim(line.substr(0, eq)))] = Trim(line.substr(eq + 1));
		}

		const auto get = [&](const char* key) -> const std::string* {
			auto it = kv.find(key);
			return it == kv.end() ? nullptr : &it->second;
		};
		const auto num = [&](const char* key, auto& out) {
			if (auto* v = get(key)) {
				try {
					using T = std::remove_reference_t<decltype(out)>;
					if constexpr (std::is_floating_point_v<T>) {
						out = static_cast<T>(std::stod(*v));
					} else {
						out = static_cast<T>(std::stoll(*v, nullptr, 0));
					}
				} catch (...) {
				}
			}
		};
		const auto flag = [&](const char* key, bool& out) {
			if (auto* v = get(key)) {
				const auto l = Lower(*v);
				out = l == "1" || l == "true" || l == "yes" || l == "on";
			}
		};
		const auto list = [&](const char* key, std::vector<std::string>& out) {
			if (auto* v = get(key)) out = SplitList(*v);
		};
		const auto form = [&](const char* key, std::optional<FormRef>& out) {
			if (auto* v = get(key)) out = ParseForm(*v);
		};

		num("general.imenukey", menuKey);
		flag("general.bshowrumors", showRumors);
		flag("general.brenameenemies", renameEnemies);
		flag("general.bbuffgrunts", buffGrunts);
		flag("general.binterrogation", interrogation);

		num("nemesis.fcheatdeathchance", core.cheatDeathChance);
		num("nemesis.fkinvengeancechance", core.kinVengeanceChance);
		num("nemesis.fyieldchancemult", core.yieldChanceMult);
		num("nemesis.inemesishatred", core.nemesisHatred);
		num("nemesis.imaxnemeses", core.maxNemeses);

		num("ambush.fchanceperhour", core.ambushChancePerHour);
		num("ambush.fcooldowndays", core.ambushCooldownDays);
		num("ambush.imaxescorts", core.maxEscorts);
		num("ambush.fspawndistance", spawnDistance);
		num("ambush.fdespawndistance", despawnDistance);

		flag("world.bfactionwar", core.factionWar);
		flag("world.blifeevents", core.lifeEvents);
		num("world.ichroniclelimit", core.chronicleLimit);

		list("detection.sbanditnames", banditNames);
		list("detection.sforswornnames", forswornNames);
		list("detection.ssilverhandnames", silverHandNames);
		list("detection.sthalmornames", thalmorNames);
		list("detection.svampirenames", vampireNames);
		list("detection.snecromancernames", necromancerNames);
		list("detection.sbossnames", bossNames);

		form("factions.sbandit", banditFaction);
		form("factions.sforsworn", forswornFaction);
		form("factions.ssilverhand", silverHandFaction);
		form("factions.sthalmor", thalmorFaction);
		form("factions.svampire", vampireFaction);
		form("factions.snecromancer", necromancerFaction);

		// Keep values sane even if the INI is not.
		core.cheatDeathChance = std::clamp(core.cheatDeathChance, 0.0f, 1.0f);
		core.kinVengeanceChance = std::clamp(core.kinVengeanceChance, 0.0f, 1.0f);
		core.ambushChancePerHour = std::clamp(core.ambushChancePerHour, 0.0f, 1.0f);
		core.maxEscorts = std::clamp(core.maxEscorts, 0, 8);
		core.maxNemeses = std::clamp(core.maxNemeses, 1, 50);
		core.nemesisHatred = std::clamp(core.nemesisHatred, 10, 100);
		spawnDistance = std::clamp(spawnDistance, 300.0f, 6000.0f);
	}
}

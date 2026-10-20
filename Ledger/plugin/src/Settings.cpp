#include "Settings.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <unordered_map>

namespace lgp
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
			for (char c : v + ",") {
				if (c == ',') {
					if (auto t = Trim(cur); !t.empty()) out.push_back(t);
					cur.clear();
				} else {
					cur += c;
				}
			}
			return out;
		}

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
		if (!in) return;

		std::unordered_map<std::string, std::string> kv;
		std::string section, line;
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
		const auto get = [&](const char* k) -> const std::string* {
			auto it = kv.find(k);
			return it == kv.end() ? nullptr : &it->second;
		};
		const auto num = [&](const char* k, auto& out) {
			if (auto* v = get(k)) {
				try {
					using T = std::remove_reference_t<decltype(out)>;
					if constexpr (std::is_floating_point_v<T>) out = static_cast<T>(std::stod(*v));
					else out = static_cast<T>(std::stoll(*v, nullptr, 0));
				} catch (...) {
				}
			}
		};
		const auto flag = [&](const char* k, bool& out) {
			if (auto* v = get(k)) {
				const auto l = Lower(*v);
				out = l == "1" || l == "true" || l == "yes" || l == "on";
			}
		};

		num("general.imenukey", menuKey);
		flag("general.bnotifications", notifications);

		flag("prices.bscaleprices", scalePrices);
		num("prices.fpricestrength", priceStrength);
		num("prices.fpriceswing", core.priceSwing);
		flag("prices.bscalemerchantgold", scaleMerchantGold);
		flag("prices.bshiftstock", shiftStock);

		num("investing.fsharefee", core.shareFee);
		num("investing.fraidchance", core.raidChance);
		num("investing.fdividendshare", core.dividendShare);

		flag("events.bcivilwar", civilWar);
		flag("events.bdragons", dragons);
		if (auto* v = get("events.simperialfaction")) imperialFaction = ParseForm(*v);
		if (auto* v = get("events.sstormcloakfaction")) stormcloakFaction = ParseForm(*v);
		if (auto* v = get("events.soutlawnames")) outlawNames = SplitList(*v);
		num("events.inewslimit", core.newsLimit);

		priceStrength = std::clamp(priceStrength, 0.0f, 1.5f);
		core.priceSwing = std::clamp(core.priceSwing, 0.0f, 2.0f);
		core.shareFee = std::clamp(core.shareFee, 0.0f, 0.5f);
		core.raidChance = std::clamp(core.raidChance, 0.0f, 1.0f);
		core.dividendShare = std::clamp(core.dividendShare, 0.0f, 1.0f);
	}
}

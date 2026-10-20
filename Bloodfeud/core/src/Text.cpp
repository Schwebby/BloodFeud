#include "bloodfeud/Text.hpp"

#include <algorithm>
#include <array>
#include <cctype>

namespace bf
{
	std::string Npc::ShortName() const
	{
		return epithet.empty() ? first : first + " " + epithet;
	}

	std::string Npc::FullName() const
	{
		std::string out = first;
		if (!surname.empty()) {
			out += " " + surname;
		}
		if (!epithet.empty()) {
			out += " " + epithet;
		}
		return out;
	}
}

namespace bf::text
{
	namespace
	{
		using List = std::vector<std::string>;

		struct CulturePool
		{
			List male;
			List female;
			List surnames;  // empty => patronymic or none
		};

		const CulturePool& Pool(Culture c)
		{
			static const std::array<CulturePool, static_cast<std::size_t>(Culture::Count)> pools{ {
				// Nord: patronymic or clan surnames
				{ { "Hjorvar", "Ulfgar", "Brandr", "Sigurd", "Torvald", "Kjeld", "Haldor", "Orrin", "Svenrik", "Grimvar", "Eirik", "Bolli", "Raknar", "Vidgrod", "Hrothmar", "Steinar", "Gunnulf", "Arngeir", "Yngvild", "Thrandr" },
				  { "Astrid", "Hilde", "Sigrun", "Ragnhild", "Jorunn", "Ingrid", "Brenna", "Thora", "Gerd", "Svana", "Halla", "Eydis", "Runa", "Solveig", "Vigdis" },
				  { "Iron-Jaw", "Frost-Hand", "Grey-Beard", "Stone-Fist", "Ash-Born", "Cold-Blood", "Ice-Veins", "Black-Tooth", "Snow-Strider", "Wolf-Kin" } },
				// Imperial
				{ { "Marcus", "Decimus", "Varro", "Quintus", "Lucan", "Aulus", "Cassius", "Titus", "Gallus", "Severus", "Octavius", "Rufus" },
				  { "Livia", "Claudia", "Aurelia", "Valeria", "Junia", "Sabina", "Fausta", "Camilla", "Drusa" },
				  { "Varus", "Corvinus", "Albus", "Calvo", "Maro", "Sulla", "Pontius", "Aquila", "Cotta", "Faustus" } },
				// Breton
				{ { "Gaston", "Remy", "Alain", "Bastien", "Eloi", "Henri", "Jules", "Rolund", "Odric", "Thibaut", "Lucien" },
				  { "Elise", "Margot", "Celine", "Aveline", "Odette", "Brigitte", "Solene", "Maelle", "Ysabel" },
				  { "Dumont", "Larouche", "Belmont", "Fontaine", "Corbeau", "Delacroix", "Marchand", "Vernier", "Rochefort" } },
				// Redguard
				{ { "Kematu", "Ahmar", "Sayid", "Jabir", "Rashid", "Faraz", "Tazir", "Nadir", "Hakim", "Zahir" },
				  { "Amira", "Nazeem", "Sahra", "Leyla", "Zaina", "Farah", "Imani", "Rania" },
				  { "al-Hazir", "at-Sentinel", "Sand-Born", "of Hegathe", "al-Rihad", "of Taneth" } },
				// Reachman (Forsworn)
				{ { "Madanach", "Duach", "Cosnach", "Bradach", "Uaile", "Kaie", "Eltrys", "Dervenin", "Muiri", "Odvan", "Nepos", "Braig", "Cailean", "Fionn" },
				  { "Mena", "Uaile", "Eighlis", "Caeda", "Bothela", "Ainethach", "Morag", "Sorcha", "Brenna", "Deirdre" },
				  {} },
				// Altmer
				{ { "Ancano", "Ondolemar", "Estormo", "Rulindil", "Aicantar", "Nelacar", "Taarion", "Faendal", "Calmaril", "Linwe" },
				  { "Elenwen", "Taarie", "Nirya", "Melaran", "Sinnere", "Arquen", "Lillandril", "Ilmara" },
				  {} },
				// Bosmer
				{ { "Gwilin", "Anoriath", "Elrindir", "Niruin", "Faendir", "Thorin", "Glarthir", "Valindor" },
				  { "Mjoll", "Elynea", "Nimriel", "Silwen", "Valaste", "Indara" },
				  {} },
				// Dunmer
				{ { "Drevis", "Adril", "Fethis", "Revyn", "Sarthis", "Dravin", "Teldryn", "Malur", "Nivos", "Galdyn" },
				  { "Irileth", "Brelyna", "Avrusa", "Dreyla", "Ienith", "Narana", "Veleth", "Nilara" },
				  { "Sadri", "Arano", "Ienth", "Indoril", "Drothan", "Llervu", "Telvanni-Exile", "Hlaalu" } },
				// Khajiit
				{ { "Ri'saad", "J'zargo", "Kharjo", "Ma'dran", "Ahkari", "Dro'marash", "Ra'zhinda", "Kesh", "Jo'dar", "Ra'kheran" },
				  { "Ri'shan", "Atahbah", "Zaynabi", "Khayla", "Shavari", "Ahjisi", "Ma'zaka", "Dar'ra" },
				  {} },
				// Argonian
				{ { "Talen-Jei", "Derkeethus", "Wujeeta", "Neetrenaza", "Teeba-Ei", "Gulum-Ei", "Veezara", "Deeja", "Haj-Ei", "Keerava" },
				  { "Keerava", "Shahvee", "Wujeeta", "Scouts-Many-Marshes", "Deeja", "Saden", "Tumna" },
				  {} },
				// Orc
				{ { "Ghorbash", "Mauhulakh", "Yamarz", "Burguk", "Larak", "Dushnamub", "Ogol", "Grushnag", "Shagrol", "Borkul" },
				  { "Ghorza", "Atub", "Shuftharz", "Murbul", "Sharamph", "Bolar", "Lazgar", "Garakh" },
				  {} },
			} };
			return pools[static_cast<std::size_t>(c)];
		}

		const List kEpithets{ "the Cruel", "the Red", "Bone-Breaker", "the Quiet", "the Butcher", "Half-Hand", "the Grinning", "Blacktongue", "the Patient", "the Wolf", "Skull-Splitter", "the Lucky", "the Unkind", "Crowfeeder", "the Tall", "the Pale", "Mead-Thief", "the Hollow", "Ash-Eater", "the Cold" };

		const List kTrades{ "a failed miner", "a deserter from the Legion", "a disgraced Stormcloak", "an ex-sailor", "a former hunter", "a cattle thief", "a smuggler", "a sellsword", "an outcast smith", "a hedge-mage", "a gravedigger", "a former guard", "a poacher", "a skooma runner", "a ferryman" };

		const List kFaiths{ "Talos", "Mara", "Kynareth", "Arkay", "Akatosh", "Dibella", "Stendarr", "Zenithar", "Julianos", "Hircine", "Boethiah", "Molag Bal", "Namira", "Malacath", "Sanguine", "Mephala", "no god at all", "no god at all", "no god at all" };

		const List kBackstories{
			"lost a farm to the Jarl's tax collectors",
			"was born in a Riften alley and never left the gutter's habits behind",
			"killed a man in a tavern brawl and fled before the guards came",
			"deserted the Legion after Helgen",
			"was sold to a Khajiit caravan as a child and escaped",
			"buried three brothers in one winter",
			"was cast out of the clan for cowardice and means to disprove it",
			"once guarded a merchant who never paid",
			"was promised a Jarl's pardon that never came",
			"lost an eye to a sabre cat and a wife to a Nord with a warm house",
			"watched the Thalmor take their father for worshipping Talos",
			"was a college apprentice until the fire",
			"won a fortune at dice and lost it the same night",
			"was raised in an orphanage and ran away at eleven",
		};

		std::string Lower(std::string_view s)
		{
			std::string out(s);
			std::ranges::transform(out, out.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
			return out;
		}

		std::string Squash(std::string_view s)
		{
			std::string out = Lower(s);
			std::erase_if(out, [](char ch) { return ch == ' ' || ch == '-' || ch == '_' || ch == '\''; });
			return out;
		}

		// Case/space-insensitive: "DarkElfRace" matches "dark elf".
		bool Has(std::string_view hay, std::string_view needle)
		{
			return Squash(hay).find(Squash(needle)) != std::string::npos;
		}

		const Memory* Salient(const Npc& npc)
		{
			// The most recent memory that is worth a grudge.
			for (auto it = npc.memories.rbegin(); it != npc.memories.rend(); ++it) {
				switch (it->kind) {
				case MemoryKind::LeftForDead:
				case MemoryKind::KinSlain:
				case MemoryKind::LeaderSlain:
				case MemoryKind::FledFrom:
				case MemoryKind::Humiliated:
				case MemoryKind::WoundedBy:
				case MemoryKind::Spared:
					return &*it;
				default:
					break;
				}
			}
			return nullptr;
		}
	}

	std::string Fmt(std::string_view pattern, std::initializer_list<std::string_view> args)
	{
		std::string out;
		out.reserve(pattern.size() + 32);
		for (std::size_t i = 0; i < pattern.size(); ++i) {
			if (pattern[i] == '{' && i + 2 < pattern.size() && pattern[i + 2] == '}' && std::isdigit(static_cast<unsigned char>(pattern[i + 1]))) {
				const std::size_t idx = static_cast<std::size_t>(pattern[i + 1] - '0');
				if (idx < args.size()) {
					out += *(args.begin() + idx);
				}
				i += 2;
			} else {
				out += pattern[i];
			}
		}
		return out;
	}

	std::string RandomFirstName(Rng& rng, Culture c, bool female)
	{
		const auto& p = Pool(c);
		return female ? rng.Pick(p.female) : rng.Pick(p.male);
	}

	std::string RandomSurname(Rng& rng, Culture c, bool female, std::string_view father)
	{
		const auto& p = Pool(c);
		if (c == Culture::Nord && !father.empty() && rng.Chance(0.5f)) {
			return std::string(father) + (female ? "-Daughter" : "-Son");
		}
		if (c == Culture::Orc) {
			return (female ? "gra-" : "gro-") + rng.Pick(Pool(Culture::Orc).male);
		}
		if (p.surnames.empty()) {
			return {};
		}
		return rng.Pick(p.surnames);
	}

	std::string RandomEpithet(Rng& rng) { return rng.Pick(kEpithets); }

	std::string EarnedEpithet(Rng& rng, const Npc& npc)
	{
		std::vector<std::string> options;
		if (npc.cheatedDeath >= 2) {
			options.emplace_back("Twice-Dead");
			options.emplace_back("the Unburied");
		} else if (npc.cheatedDeath == 1) {
			options.emplace_back("the Returned");
			options.emplace_back("Grave-Spurned");
		}
		if (npc.injuries & kBurnScars) options.emplace_back("the Burned");
		if (npc.injuries & kFrostbitten) options.emplace_back("Frost-Bitten");
		if (npc.injuries & kLimp) options.emplace_back("the Lame");
		if (npc.injuries & kOneEye) options.emplace_back("One-Eye");
		if (npc.injuries & kMaimedHand) options.emplace_back("Three-Fingers");
		if (npc.injuries & kBrokenJaw) options.emplace_back("Crook-Jaw");
		if (npc.rank == Rank::Warchief) options.emplace_back("the Warlord");
		if (options.empty()) {
			return RandomEpithet(rng);
		}
		return rng.Pick(options);
	}

	void GenerateIdentity(Rng& rng, Npc& npc, GameGroup group)
	{
		npc.first = RandomFirstName(rng, npc.culture, npc.female);
		std::string father = npc.culture == Culture::Nord ? RandomFirstName(rng, Culture::Nord, false) : std::string{};
		npc.surname = RandomSurname(rng, npc.culture, npc.female, father);
		npc.epithet = rng.Chance(0.35f) ? RandomEpithet(rng) : std::string{};

		// Temperament weights shift by group.
		std::array<int, static_cast<std::size_t>(Temperament::Count)> w{ 14, 14, 12, 12, 12, 6, 12, 8 };
		switch (group) {
		case GameGroup::Forsworn:
		case GameGroup::SilverHand:
			w[static_cast<int>(Temperament::Zealot)] += 18;
			break;
		case GameGroup::Thalmor:
			w[static_cast<int>(Temperament::Proud)] += 18;
			w[static_cast<int>(Temperament::Cold)] += 10;
			break;
		case GameGroup::Vampire:
		case GameGroup::Necromancer:
			w[static_cast<int>(Temperament::Cunning)] += 12;
			w[static_cast<int>(Temperament::Unhinged)] += 10;
			break;
		default:
			break;
		}
		int total = 0;
		for (int x : w) total += x;
		int roll = rng.Range(0, total - 1);
		for (std::size_t i = 0; i < w.size(); ++i) {
			if (roll < w[i]) {
				npc.temper = static_cast<Temperament>(i);
				break;
			}
			roll -= w[i];
		}

		// 1-2 quirks
		static constexpr std::array<std::uint32_t, 12> quirks{ kDrunkard, kSober, kPerceptive, kIntrovert, kBraggart, kDogLover, kPious, kGreedy, kVengeful, kLoyal, kTraitorous, kSadist };
		const int n = rng.Range(1, 2);
		for (int i = 0; i < n; ++i) {
			npc.traits |= quirks[rng.Below(quirks.size())];
		}
		if ((npc.traits & kDrunkard) && (npc.traits & kSober)) npc.traits &= ~kSober;
		if ((npc.traits & kLoyal) && (npc.traits & kTraitorous)) npc.traits &= ~kTraitorous;

		npc.trade = rng.Pick(kTrades);
		npc.faith = group == GameGroup::Thalmor ? "the Aldmeri pantheon" : group == GameGroup::Forsworn ? "the Old Gods of the Reach" : group == GameGroup::Vampire ? "Molag Bal" : rng.Pick(kFaiths);
		npc.backstory = rng.Pick(kBackstories);

		// 1-2 combat edges from the start so even first meetings feel distinct
		static constexpr std::array<std::uint32_t, 10> strengths{ kIronHide, kFireproof, kFrostborn, kStormward, kSpellbreaker, kBerserker, kSwift, kDuelist, kMarksman, kHardy };
		static constexpr std::array<std::uint32_t, 6> weaknesses{ kFearsFire, kBrittle, kConductive, kGlassJaw, kSlow, kWeakArm };
		if (rng.Chance(0.5f)) npc.edges |= strengths[rng.Below(strengths.size())];
		if (rng.Chance(0.6f)) npc.edges |= weaknesses[rng.Below(weaknesses.size())];
	}

	Culture CultureFromRaceName(std::string_view raceName, GameGroup group)
	{
		if (group == GameGroup::Forsworn) return Culture::Reachman;
		if (Has(raceName, "nord")) return Culture::Nord;
		if (Has(raceName, "imperial")) return Culture::Imperial;
		if (Has(raceName, "breton")) return Culture::Breton;
		if (Has(raceName, "redguard")) return Culture::Redguard;
		if (Has(raceName, "high elf") || Has(raceName, "altmer")) return Culture::Altmer;
		if (Has(raceName, "wood elf") || Has(raceName, "bosmer")) return Culture::Bosmer;
		if (Has(raceName, "dark elf") || Has(raceName, "dunmer")) return Culture::Dunmer;
		if (Has(raceName, "khajiit")) return Culture::Khajiit;
		if (Has(raceName, "argonian")) return Culture::Argonian;
		if (Has(raceName, "orc") || Has(raceName, "orsimer")) return Culture::Orc;
		return Culture::Nord;
	}

	Hold HoldFromLocationName(std::string_view name)
	{
		if (Has(name, "haafingar") || Has(name, "solitude")) return Hold::Haafingar;
		if (Has(name, "hjaalmarch") || Has(name, "morthal")) return Hold::Hjaalmarch;
		if (Has(name, "the pale") || Has(name, "pale hold") || Has(name, "dawnstar")) return Hold::ThePale;
		if (Has(name, "winterhold")) return Hold::Winterhold;
		if (Has(name, "the reach") || Has(name, "reach hold") || Has(name, "markarth")) return Hold::TheReach;
		if (Has(name, "whiterun")) return Hold::Whiterun;
		if (Has(name, "eastmarch") || Has(name, "windhelm")) return Hold::Eastmarch;
		if (Has(name, "falkreath")) return Hold::Falkreath;
		if (Has(name, "the rift") || Has(name, "rift hold") || Has(name, "riften")) return Hold::TheRift;
		return Hold::Unknown;
	}

	std::string_view HoldName(Hold h)
	{
		static constexpr std::array<std::string_view, kHoldCount> names{ "Haafingar", "Hjaalmarch", "The Pale", "Winterhold", "The Reach", "Whiterun Hold", "Eastmarch", "Falkreath Hold", "The Rift" };
		return h == Hold::Unknown ? std::string_view{ "the wilds" } : names[static_cast<std::size_t>(h)];
	}

	std::string_view FactionName(FactionId f)
	{
		static constexpr std::array<std::string_view, kFactionCount> names{ "the Forsworn", "the Silver Hand", "the Thalmor Justiciars", "the Volkihar Thralls", "the Necromancer Conclave", "the Bloodhorn Clan", "the Ashen Hand", "the Greymane Raiders" };
		return f == FactionId::None ? std::string_view{ "no banner" } : names[static_cast<std::size_t>(f)];
	}

	std::string_view RankName(Rank r, FactionId f)
	{
		switch (f) {
		case FactionId::Forsworn:
			return std::array<std::string_view, 4>{ "Forsworn", "Briarheart", "Shaman", "Briar King" }[static_cast<int>(r)];
		case FactionId::Thalmor:
			return std::array<std::string_view, 4>{ "Soldier", "Justiciar", "Inquisitor", "High Justiciar" }[static_cast<int>(r)];
		case FactionId::Volkihar:
			return std::array<std::string_view, 4>{ "Thrall", "Fledgling", "Night Master", "Blood Lord" }[static_cast<int>(r)];
		case FactionId::Necromancers:
			return std::array<std::string_view, 4>{ "Acolyte", "Adept", "Master", "Archlich" }[static_cast<int>(r)];
		case FactionId::SilverHand:
			return std::array<std::string_view, 4>{ "Hunter", "Skinner", "Fist", "Hand of Silver" }[static_cast<int>(r)];
		default:
			return std::array<std::string_view, 4>{ "Cutthroat", "Lieutenant", "Captain", "Warchief" }[static_cast<int>(r)];
		}
	}

	std::string_view TemperName(Temperament t)
	{
		static constexpr std::array<std::string_view, static_cast<std::size_t>(Temperament::Count)> n{ "Hot-headed", "Cowardly", "Cold", "Proud", "Cunning", "Zealous", "Honorable", "Unhinged" };
		return n[static_cast<std::size_t>(t)];
	}

	std::string_view CultureName(Culture c)
	{
		static constexpr std::array<std::string_view, static_cast<std::size_t>(Culture::Count)> n{ "Nord", "Imperial", "Breton", "Redguard", "Reachman", "Altmer", "Bosmer", "Dunmer", "Khajiit", "Argonian", "Orc" };
		return n[static_cast<std::size_t>(c)];
	}

	std::string TraitList(std::uint32_t t)
	{
		static constexpr std::array<std::pair<std::uint32_t, std::string_view>, 12> n{ { { kDrunkard, "drunkard" }, { kSober, "sober" }, { kPerceptive, "perceptive" }, { kIntrovert, "introverted" }, { kBraggart, "braggart" }, { kDogLover, "keeps a dog" }, { kPious, "pious" }, { kGreedy, "greedy" }, { kVengeful, "vengeful" }, { kLoyal, "loyal" }, { kTraitorous, "two-faced" }, { kSadist, "cruel" } } };
		std::string out;
		for (auto& [bit, name] : n) {
			if (t & bit) {
				if (!out.empty()) out += ", ";
				out += name;
			}
		}
		return out.empty() ? "unremarkable" : out;
	}

	std::string EdgeList(std::uint32_t e, bool strengths)
	{
		static constexpr std::array<std::pair<std::uint32_t, std::string_view>, 16> n{ { { kIronHide, "Iron Hide" }, { kFireproof, "Fireproof" }, { kFrostborn, "Frostborn" }, { kStormward, "Stormward" }, { kSpellbreaker, "Spellbreaker" }, { kBerserker, "Berserker" }, { kSwift, "Swift" }, { kDuelist, "Duelist" }, { kMarksman, "Marksman" }, { kHardy, "Hardy" }, { kFearsFire, "Fears Fire" }, { kBrittle, "Brittle in the Cold" }, { kConductive, "Weak to Shock" }, { kGlassJaw, "Glass Jaw" }, { kSlow, "Slow" }, { kWeakArm, "Weak Arm" } } };
		std::string out;
		for (auto& [bit, name] : n) {
			const bool isWeak = (bit & kWeaknessMask) != 0;
			if ((e & bit) && isWeak != strengths) {
				if (!out.empty()) out += ", ";
				out += name;
			}
		}
		return out.empty() ? "none" : out;
	}

	std::string InjuryList(std::uint32_t i)
	{
		static constexpr std::array<std::pair<std::uint32_t, std::string_view>, 7> n{ { { kScarred, "scarred" }, { kBurnScars, "burn scars" }, { kFrostbitten, "frostbitten" }, { kLimp, "limps" }, { kOneEye, "lost an eye" }, { kBrokenJaw, "broken jaw" }, { kMaimedHand, "maimed hand" } } };
		std::string out;
		for (auto& [bit, name] : n) {
			if (i & bit) {
				if (!out.empty()) out += ", ";
				out += name;
			}
		}
		return out.empty() ? "none" : out;
	}

	std::string Taunt(Rng& rng, const Npc& npc, const std::string& kinName)
	{
		const Memory* m = Salient(npc);
		const std::string place = m && !m->place.empty() ? m->place : std::string(HoldName(npc.home));
		std::vector<std::string> lines;
		if (m) {
			switch (m->kind) {
			case MemoryKind::LeftForDead:
				lines = { Fmt("You left me for dead at {0}. You should have made sure.", { place }),
					Fmt("Remember {0}? I crawled out of that grave for you.", { place }),
					"Death wouldn't have me. Now I'm here for you." };
				break;
			case MemoryKind::KinSlain:
				lines = { Fmt("You killed {0}. I've come to collect the blood-price.", { kinName }),
					Fmt("{0} was my blood. Draw your blade.", { kinName }),
					Fmt("I buried {0} with my own hands. Your turn.", { kinName }) };
				break;
			case MemoryKind::LeaderSlain:
				lines = { "You cut the head off our clan. We grew a new one.", "Our chief's blood is still on your hands. Let's wash it off." };
				break;
			case MemoryKind::FledFrom:
				lines = { Fmt("I ran from you at {0}. I'm done running.", { place }), "Last time you got lucky. Not today." };
				break;
			case MemoryKind::Humiliated:
				lines = { "You made me grovel. Now you'll bleed for it.", "I told you nothing worth knowing. And I'll take your head." };
				break;
			case MemoryKind::Spared:
				lines = { "You spared me. That was your mistake.", "Mercy? I spat on it the moment you turned your back." };
				break;
			default:
				lines = { Fmt("Remember me? {0}.", { place }), "I've been looking for you." };
				break;
			}
		} else {
			lines = { "You don't know me. You will.", "There you are." };
		}
		if (npc.injuries & kBrokenJaw) {
			lines.push_back("*spits through a crooked jaw* You did this.");
		}
		if (npc.temper == Temperament::Zealot) {
			lines.push_back("The gods sent me to end you.");
		}
		return rng.Pick(lines);
	}

	std::string YieldLine(Rng& rng, const Npc& npc)
	{
		switch (npc.temper) {
		case Temperament::Coward:
			return rng.Chance(0.5f) ? "Enough! Please, I yield! I'll tell you anything!" : "Mercy! Gods, mercy!";
		case Temperament::Cunning:
			return "Alright, alright. Let's talk like reasonable people.";
		case Temperament::Proud:
			return "...Fine. You've won. For now.";
		case Temperament::Honorable:
			return "I yield. You fought well.";
		default:
			return "I yield! I yield!";
		}
	}

	std::string LifeEvent(Rng& rng, Npc& npc, std::string& outEpithet)
	{
		const std::string who = npc.ShortName();
		outEpithet.clear();
		switch (rng.Below(8)) {
		case 0:
			if (npc.traits & kDrunkard) {
				npc.traits &= ~kDrunkard;
				npc.traits |= kSober;
				return Fmt("{0} has sworn off mead, they say. Sober as a priest.", { who });
			}
			npc.traits |= kDrunkard;
			npc.traits &= ~kSober;
			return Fmt("{0} has taken to the bottle.", { who });
		case 1:
			if (npc.traits & kDogLover) {
				return Fmt("{0}'s dog was seen guarding a camp in {1}.", { who, HoldName(npc.home) });
			}
			npc.traits |= kDogLover;
			return Fmt("{0} adopted a stray dog.", { who });
		case 2:
			npc.traits |= kPious;
			return Fmt("{0} was seen leaving an offering at a shrine of {1}.", { who, npc.faith });
		case 3:
			npc.edges |= kDuelist;
			return Fmt("{0} won a duel outside a tavern in {1}.", { who, HoldName(npc.home) });
		case 4:
			if (npc.epithet.empty()) {
				outEpithet = RandomEpithet(rng);
				npc.epithet = outEpithet;
				return Fmt("Folk have started calling {0} \"{1}\".", { npc.first, outEpithet });
			}
			return Fmt("{0} has been boasting about you in the taverns.", { who });
		case 5:
			npc.traits |= kGreedy;
			return Fmt("{0} robbed a caravan on the road through {1}.", { who, HoldName(npc.home) });
		case 6:
			npc.edges |= kHardy;
			return Fmt("{0} survived a fever that killed half a camp.", { who });
		default:
			npc.traits |= kVengeful;
			return Fmt("{0} has been asking travellers about you.", { who });
		}
	}
}

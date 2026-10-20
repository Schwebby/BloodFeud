// Checks the shipped INI parses into the values we expect.
#include "Settings.h"

#include <cstdio>
#include <fstream>

static int g_fail = 0;
#define CHECK(c)                                                         \
	do {                                                                 \
		if (!(c)) {                                                      \
			std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #c);   \
			++g_fail;                                                    \
		}                                                                \
	} while (0)

int main()
{
	bfp::Config c;
	c.Load("dist/SKSE/Plugins/Bloodfeud.ini");
	CHECK(c.menuKey == 65);
	CHECK(c.renameEnemies);
	CHECK(c.core.cheatDeathChance == 0.25f);
	CHECK(c.core.maxNemeses == 10);
	CHECK(c.core.maxEscorts == 3);
	CHECK(c.spawnDistance == 1800.0f);
	CHECK(c.banditNames.size() == 5 && c.banditNames[1] == "Highwayman");
	CHECK(c.silverHandFaction && c.silverHandFaction->plugin == "Skyrim.esm" && c.silverHandFaction->localId == 0x0AA0A4);
	CHECK(c.necromancerFaction && c.necromancerFaction->localId == 0x034B74);

	// Hostile input: clamps, bad numbers, blank forms.
	{
		std::ofstream o("settings_test_tmp.ini");
		o << "[Ambush]\niMaxEscorts = 99\nfChancePerHour = banana\n[Factions]\nsBandit =\nsThalmor = Skyrim.esm|zz\n[General]\nbRenameEnemies = off ; comment\n";
	}
	bfp::Config d;
	d.Load("settings_test_tmp.ini");
	CHECK(d.core.maxEscorts == 8);
	CHECK(d.core.ambushChancePerHour == 0.06f);
	CHECK(!d.banditFaction);
	CHECK(!d.thalmorFaction);
	CHECK(!d.renameEnemies);
	std::remove("settings_test_tmp.ini");

	std::printf(g_fail ? "%d FAILURES\n" : "settings tests passed\n", g_fail);
	return g_fail ? 1 : 0;
}

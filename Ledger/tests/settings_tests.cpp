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
	lgp::Config c;
	c.Load("dist/SKSE/Plugins/Ledger.ini");
	CHECK(c.menuKey == 66);
	CHECK(c.scalePrices && c.scaleMerchantGold && c.shiftStock);
	CHECK(c.priceStrength == 1.0f);
	CHECK(c.core.shareFee == 0.05f);
	CHECK(c.imperialFaction && c.imperialFaction->localId == 0x01C9FC);
	CHECK(c.stormcloakFaction && c.stormcloakFaction->localId == 0x01C9FD);
	CHECK(c.outlawNames.size() == 6 && c.outlawNames[0] == "Bandit");

	{
		std::ofstream o("ledger_test_tmp.ini");
		o << "[Prices]\nfPriceStrength = 9\nbScalePrices = no\n[Events]\nsImperialFaction =\n";
	}
	lgp::Config d;
	d.Load("ledger_test_tmp.ini");
	CHECK(d.priceStrength == 1.5f);
	CHECK(!d.scalePrices);
	CHECK(!d.imperialFaction);
	CHECK(d.stormcloakFaction.has_value());  // default kept
	std::remove("ledger_test_tmp.ini");

	std::printf(g_fail ? "%d FAILURES\n" : "settings tests passed\n", g_fail);
	return g_fail ? 1 : 0;
}

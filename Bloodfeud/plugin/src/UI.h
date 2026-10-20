#pragma once
#include "bloodfeud/Types.hpp"

namespace bfp::ui
{
	// Skyrim message box with buttons; the callback gets the 0-based button index.
	void Show(const std::string& body, const std::vector<std::string>& buttons, std::function<void(int)> onPick);

	void ShowMain();
	void ShowYield(RE::FormID ref, bf::NpcId id);
}

#pragma once

namespace lgp::ui
{
	// Skyrim message box with buttons; the callback gets the 0-based button index.
	void Show(const std::string& body, const std::vector<std::string>& buttons, std::function<void(int)> onPick);
	void ShowMain();
}

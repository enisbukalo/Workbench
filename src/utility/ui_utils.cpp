#include "ui_utils.h"

#include "ThemeManager.h"

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <utility>

using namespace ftxui;

DashFillerNode::DashFillerNode() : Node()
{
}

void DashFillerNode::ComputeRequirement()
{
	requirement_.min_x = 0;
	requirement_.min_y = 1;
	requirement_.flex_grow_x = 1;
	requirement_.flex_shrink_x = 1;
}

void DashFillerNode::SetBox(Box box)
{
	Node::SetBox(box);
}

void DashFillerNode::Render(Screen &screen)
{
	const int w = box_.x_max - box_.x_min + 1;
	if (w <= 0)
		return;

	for (int x = 0; x < w; ++x) {
		int px = box_.x_min + x;
		const int py = box_.y_min;
		Cell &p = screen.CellAt(px, py);
		if (w >= 6) {
			p.character = "-";
			p.foreground_color = ThemeManager::instance().getActive()->mutedText;
		} else {
			p.character = " ";
		}
	}
}

namespace ui_utils {

// Cap for content-sized input boxes so a long file path doesn't blow out the
// settings column; the FTXUI Input scrolls horizontally past this width.
static constexpr int INPUT_BOX_MAX_WIDTH = 40;

std::string formatFloat(float value, int precision)
{
	std::ostringstream oss;
	oss << std::fixed << std::setprecision(precision) << value;
	return oss.str();
}

Element settingRowComponent(const std::string &label,
							Element componentRender,
							const std::string *valueStr)
{
	// When a value is supplied, size the box to it: floor of 5 cols keeps the
	// cursor visible for short values, and a cap of INPUT_BOX_MAX_WIDTH stops a
	// long path from blowing out the column (the input scrolls past the cap).
	// Otherwise leave the input flexing to fill the row.
	if (valueStr) {
		const int boxWidth = std::clamp(static_cast<int>(valueStr->size()) + 1,
										5,
										INPUT_BOX_MAX_WIDTH);
		componentRender = componentRender | size(WIDTH, EQUAL, boxWidth);
	}
	return hbox({ text(label) |
					  color(ThemeManager::instance().getActive()->label),
				  text("  "),
				  std::make_shared<DashFillerNode>(),
				  text("  "),
				  componentRender }) |
		   xflex;
}

Element numberRow(const std::string &label,
				  Element minusBtn,
				  Element inputRender,
				  Element plusBtn,
				  const std::string &valueStr)
{
	// Size the box to the value: floor of 4 cols keeps the cursor visible for
	// short values ("-1"); +1 leaves room for the edit cursor. Rebuilt each
	// frame by callers, so the box shrinks/grows with the value.
	const int boxWidth = std::max(static_cast<int>(valueStr.size()), 4) + 1;
	return hbox({ text(label) |
					  color(ThemeManager::instance().getActive()->label) |
					  vcenter,
				  text("  "),
				  std::make_shared<DashFillerNode>(),
				  text("  "),
				  minusBtn,
				  separatorLight(),
				  inputRender | size(WIDTH, EQUAL, boxWidth),
				  separatorLight(),
				  plusBtn }) |
		   xflex;
}

Element checkboxRow(const std::string &label, Element componentRender)
{
	return hbox({ text(label) |
					  color(ThemeManager::instance().getActive()->label),
				  text("  "),
				  std::make_shared<DashFillerNode>(),
				  text("  "),
				  componentRender }) |
		   xflex;
}

Component makeDropdown(const std::vector<std::string> *entries,
					   int *selected,
					   std::function<void()> onChange)
{
	// No-callback callers (e.g. the model selector) get the plain stock
	// dropdown — same widget as Dropdown(&entries, &selected).
	if (!onChange)
		return Dropdown(entries, selected);

	DropdownOption opt;
	opt.radiobox.entries = entries;
	opt.radiobox.selected = selected;
	opt.radiobox.on_change = std::move(onChange);

	// Themed entries in the open list (matches the old toggle menus):
	// active = toggleOn, others = toggleOff, focused = bold.
	opt.radiobox.transform = [](const EntryState &s) {
		auto theme = ThemeManager::instance().getActive();
		auto e =
			text(s.label) | color(s.active ? theme->toggleOn : theme->toggleOff);
		if (s.focused)
			e |= bold;
		return e;
	};

	// Themed collapsed header line. Default arrow prefix is kept by reusing
	// FTXUI's "→ "/"↓ " convention; selection text uses toggleOn.
	opt.checkbox.transform = [](const EntryState &s) {
		auto theme = ThemeManager::instance().getActive();
		auto prefix = text(s.state ? "↓ " : "→ ");
		auto e = hbox({ prefix, text(s.label) }) | color(theme->toggleOn);
		if (s.focused)
			e |= bold;
		return e;
	};

	// Match FTXUI's stock layout but drop the border so the closed dropdown is
	// a single compact line. Keep the trailing filler() in both branches — it
	// gives the closed component a flex body so the parent Container can focus
	// it (without it the box collapses and the dropdown can't be opened).
	opt.transform = [](bool open, Element chk, Element radio) {
		if (open)
			return vbox(
				{ chk,
				  radio | vscroll_indicator | frame | size(HEIGHT, LESS_THAN, 8),
				  filler() });
		return vbox({ chk, filler() });
	};

	return Dropdown(std::move(opt));
}

} // namespace ui_utils

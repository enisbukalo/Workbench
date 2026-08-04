#pragma once

#include <ftxui/component/component.hpp>          // ftxui::Component
#include <ftxui/component/component_options.hpp>  // ftxui::DropdownOption
#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/node.hpp>
#include <functional>
#include <memory>
#include <string>
#include <vector>

/**
 * @file ui_utils.h
 * @brief FTXUI helper nodes and labelled-row builders shared across panels.
 */

/**
 * @brief Custom FTXUI Node — renders "-" dashes in GrayDark to fill available space.
 * Acts as a drop-in replacement for filler(). If width < 6, renders blank spaces instead.
 */
class DashFillerNode : public ftxui::Node {
public:
    DashFillerNode();

private:
    void ComputeRequirement() override;
    void SetBox(ftxui::Box box) override;
    void Render(ftxui::Screen& screen) override;
};

namespace ui_utils {

/**
 * @brief Format a float value to string with specified precision
 *
 * @param value The float value to format
 * @param precision Decimal places (default: 2)
 * @return Formatted string representation
 */
std::string formatFloat(float value, int precision = 2);

/**
 * @brief Render a labelled row for text inputs
 *
 * Creates a horizontal box with magenta label on left and component on right.
 * By default the input flexes to fill the row (good for paths / free text). For
 * short numeric values, pass @p valueStr to size the box to its content
 * (`max(length, 4) + 1` columns), matching numberRow; callers rebuild the row
 * each frame so the box tracks the value.
 *
 * @param label The label text (rendered in MagentaLight)
 * @param componentRender The rendered FTXUI component element
 * @param valueStr Optional current value; when non-null the box is sized to it,
 *                 otherwise the input flexes to fill (default).
 * @return Element representing the setting row
 */
ftxui::Element settingRowComponent(const std::string &label,
                                   ftxui::Element componentRender,
                                   const std::string *valueStr = nullptr);

/**
 * @brief Render a labelled number input row with +/- controls
 *
 * Creates: [Label] [-] [input] [+] layout with magenta label. The input box
 * width is sized to the current value: `max(valueStr.length(), 4) + 1` columns
 * (the floor keeps the cursor visible for short values like "-1"; the +1 leaves
 * room for the edit cursor). Because callers rebuild this row every frame from
 * the live value string, the box shrinks and grows as the value changes.
 *
 * @param label The label text (rendered in MagentaLight)
 * @param minusBtn The rendered minus button element
 * @param inputRender The rendered input field element
 * @param plusBtn The rendered plus button element
 * @param valueStr Current value text, used only to size the input box
 * @return Element representing the number row with controls
 */
ftxui::Element numberRow(const std::string &label,
                         ftxui::Element minusBtn,
                         ftxui::Element inputRender,
                         ftxui::Element plusBtn,
                         const std::string &valueStr);

/**
 * @brief Render a checkbox/toggle row
 *
 * Creates magenta label on left with component on right.
 *
 * @param label The label text (rendered in MagentaLight)
 * @param componentRender The rendered checkbox/toggle element
 * @return Element representing the checkbox row
 */
ftxui::Element checkboxRow(const std::string &label, ftxui::Element componentRender);

/**
 * @brief Themed dropdown bound to an external options vector + selected index.
 *
 * Drop-in replacement for the inline `MenuOption::Toggle()` menus: collapses the
 * options behind a single header line that expands on Enter. Same widget as the
 * model selector (`Dropdown(entries, selected)`); the only addition is the
 * optional `onChange` hook so callers can persist on selection.
 *
 * @param entries  External options vector (may be const). Not owned; must outlive the component.
 * @param selected External selected-index int. Not owned; must outlive the component.
 * @param onChange Fired when the selection changes. Pass {} for none.
 * @return FTXUI Component wrapping the dropdown.
 */
[[nodiscard]] ftxui::Component makeDropdown(const std::vector<std::string> *entries,
                                            int *selected,
                                            std::function<void()> onChange);

} // namespace ui_utils

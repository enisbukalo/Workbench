/**
 * @file terminalPanel.cpp
 * @brief Terminal panel implementation — PTY + VTerm + FTXUI rendering.
 *
 * Wires together PtyHandler (platform PTY), libvterm (ANSI parser), and
 * FTXUI (TUI rendering) to provide an embedded terminal emulator inside
 * the Workbench application.
 */

#include "terminalPanel.h"

#include <spdlog/spdlog.h>
#include <vterm.h>

#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>

#include <cstring>
#include <string>
#include <vector>

using namespace ftxui;

// ---------------------------------------------------------------------------
// UTF-8 encoding helper
// ---------------------------------------------------------------------------

static std::string Codepoint_to_utf8(uint32_t cp)
{
	std::string out;
	if (cp < 0x80) {
		out += static_cast<char>(cp);
	} else if (cp < 0x800) {
		out += static_cast<char>(0xC0 | (cp >> 6));
		out += static_cast<char>(0x80 | (cp & 0x3F));
	} else if (cp < 0x10000) {
		out += static_cast<char>(0xE0 | (cp >> 12));
		out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
		out += static_cast<char>(0x80 | (cp & 0x3F));
	} else if (cp < 0x110000) {
		out += static_cast<char>(0xF0 | (cp >> 18));
		out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
		out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
		out += static_cast<char>(0x80 | (cp & 0x3F));
	}
	return out;
}

// ---------------------------------------------------------------------------
// vterm output callback — keypresses flow through vterm → PTY
// ---------------------------------------------------------------------------

void Vterm_output_cb(const char *s, size_t len, void *user)
{
	auto *panel = static_cast<TerminalPanel *>(user);
	panel->m_pty.write(s, len);
}

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

TerminalPanel::TerminalPanel(ftxui::ScreenInteractive &screen,
							 std::string initialCommand)
	: m_screen(screen), m_initialCommand(std::move(initialCommand))
{
}

TerminalPanel::~TerminalPanel()
{
	shutdown();
}

bool TerminalPanel::isSpawned() const
{
	return m_spawned.load();
}

// ---------------------------------------------------------------------------
// Spawn — create vterm, start PTY, launch read thread
// ---------------------------------------------------------------------------

void TerminalPanel::spawn()
{
	if (m_spawned.load())
		return;

	// Use reflected box dimensions if available, otherwise keep defaults.
	int boxCols = (m_box.x_max - m_box.x_min + 1) - 2;
	int boxRows = (m_box.y_max - m_box.y_min + 1) - 2;
	if (boxCols > 0 && boxRows > 0) {
		m_cols = boxCols;
		m_rows = boxRows;
	}

	m_vt = vterm_new(m_rows, m_cols);
	vterm_set_utf8(m_vt, 1);

	m_vts = vterm_obtain_screen(m_vt);
	vterm_screen_reset(m_vts, 1);

	vterm_output_set_callback(m_vt, Vterm_output_cb, this);

	if (!m_pty.spawn(m_cols, m_rows)) {
		spdlog::error("Failed to spawn terminal: PTY spawn failed");
		vterm_free(m_vt);
		m_vt = nullptr;
		m_vts = nullptr;
		return;
	}

	spdlog::info("Terminal spawned (cols: {}, rows: {})", m_cols, m_rows);

	m_stopFlag.store(false);
	m_ptyDead.store(false);
	m_spawned.store(true);

	m_initialCmdSent.store(false);
	m_readThread = std::thread(&TerminalPanel::ReadLoop, this);
}

// ---------------------------------------------------------------------------
// Shutdown — stop thread, close PTY, free vterm
// ---------------------------------------------------------------------------

void TerminalPanel::shutdown()
{
	if (!m_spawned.load())
		return;

	m_stopFlag.store(true);
	m_cv.notify_one();

	if (m_readThread.joinable())
		m_readThread.join();

	m_pty.close();

	if (m_vt) {
		vterm_free(m_vt);
		m_vt = nullptr;
		m_vts = nullptr;
	}

	m_spawned.store(false);
	spdlog::debug("Terminal shut down");
}

// ---------------------------------------------------------------------------
// ReadLoop — background thread: PTY → vterm → redraw
// ---------------------------------------------------------------------------

void TerminalPanel::ReadLoop()
{
	char buf[4096];

	while (!m_stopFlag.load()) {
		int n = m_pty.read(buf, sizeof(buf));

		if (n > 0) {
			std::lock_guard<std::mutex> lock(m_vtermMutex);
			vterm_input_write(m_vt, buf, static_cast<size_t>(n));
			m_screen.PostEvent(Event::Custom);

			// Send the initial command after the shell produces its first
			// output (prompt), so it is ready to accept input.
			if (!m_initialCommand.empty() && !m_initialCmdSent.load()) {
				m_initialCmdSent.store(true);
				std::string cmd = m_initialCommand + "\r";
				m_pty.write(cmd.data(), cmd.size());
			}
		} else if (n == 0) {
			// No data available — sleep briefly
			std::unique_lock<std::mutex> lock(m_cvMutex);
			m_cv.wait_for(lock, std::chrono::milliseconds(16), [this] {
				return m_stopFlag.load();
			});
		} else {
			// PTY error / closed
			if (!m_pty.isAlive()) {
				m_ptyDead.store(true);
				m_screen.PostEvent(Event::Custom);
				break;
			}
		}
	}
}

// ---------------------------------------------------------------------------
// Resize — update vterm + PTY dimensions (caller must hold vtermMutex_)
// ---------------------------------------------------------------------------

void TerminalPanel::resize(int newCols, int newRows)
{
	vterm_set_size(m_vt, newRows, newCols);
	m_pty.resize(newCols, newRows);
	m_cols = newCols;
	m_rows = newRows;
}

// ---------------------------------------------------------------------------
// RenderScreen — read vterm cells → FTXUI elements
// ---------------------------------------------------------------------------

Element TerminalPanel::renderScreen()
{
	if (!m_spawned.load()) {
		return window(text(""),
					  center(text("Terminal not started.") | dim),
					  EMPTY) |
			   flex;
	}

	if (m_ptyDead.load()) {
		return window(text(""),
					  center(text("Shell process has exited.") | dim),
					  EMPTY) |
			   flex;
	}

	std::lock_guard<std::mutex> lock(m_vtermMutex);

	// Resize vterm + PTY if the container dimensions changed.
	int newCols = (m_box.x_max - m_box.x_min + 1) - 2; // subtract window border
	int newRows = (m_box.y_max - m_box.y_min + 1) - 2;
	if (newCols < 1)
		newCols = 1;
	if (newRows < 1)
		newRows = 1;
	if (newCols > 0 && newRows > 0 && m_box.x_max > 0 && m_box.y_max > 0 &&
		(newCols != m_cols || newRows != m_rows)) {
		resize(newCols, newRows);
	}

	// Query cursor position
	VTermPos cursorPos = {};
	vterm_state_get_cursorpos(vterm_obtain_state(m_vt), &cursorPos);

	Elements lines;
	lines.reserve(static_cast<size_t>(m_rows));

	for (int row = 0; row < m_rows; ++row) {
		Elements cells;
		cells.reserve(static_cast<size_t>(m_cols));

		for (int col = 0; col < m_cols;) {
			VTermPos pos;
			pos.row = row;
			pos.col = col;

			VTermScreenCell cell;
			vterm_screen_get_cell(m_vts, pos, &cell);

			// Build character string from codepoints
			std::string ch;
			if (cell.chars[0] == 0) {
				ch = " ";
			} else {
				for (int i = 0;
					 i < VTERM_MAX_CHARS_PER_CELL && cell.chars[i] != 0;
					 ++i) {
					ch += Codepoint_to_utf8(cell.chars[i]);
				}
			}

			auto elem = text(ch);

			// Apply foreground colour
			VTermColor fg = cell.fg;
			vterm_screen_convert_color_to_rgb(m_vts, &fg);
			if (!VTERM_COLOR_IS_DEFAULT_FG(&fg)) {
				elem = elem |
					   color(Color::RGB(fg.rgb.red, fg.rgb.green, fg.rgb.blue));
			}

			// Apply background colour
			VTermColor bg = cell.bg;
			vterm_screen_convert_color_to_rgb(m_vts, &bg);
			if (!VTERM_COLOR_IS_DEFAULT_BG(&bg)) {
				elem =
					elem |
					bgcolor(Color::RGB(bg.rgb.red, bg.rgb.green, bg.rgb.blue));
			}

			// Apply attributes
			if (cell.attrs.bold)
				elem = elem | bold;
			if (cell.attrs.underline)
				elem = elem | underlined;
			if (cell.attrs.reverse)
				elem = elem | inverted;
			if (cell.attrs.italic)
				elem = elem | dim; // FTXUI has no italic; use dim as fallback

			// Apply cursor inversion if this cell is at cursor position
			if (cursorPos.row == row && cursorPos.col == col)
				elem = elem | inverted;

			cells.push_back(elem);

			// Advance by cell width (wide chars occupy 2 columns)
			col += (cell.width > 1) ? cell.width : 1;
		}

		lines.push_back(hbox(std::move(cells)));
	}

	auto content = vbox(std::move(lines));
	if (!m_capturing.load()) {
		auto hint =
			text(" DETACHED - Press Ctrl+T to re-attach ") | bold | inverted;
		content = vbox({ content, hint });
	}
	return window(text(""), content, EMPTY) | flex;
}

// ---------------------------------------------------------------------------
// HandleEvent — keyboard input → vterm → PTY
// ---------------------------------------------------------------------------

bool TerminalPanel::isCapturing() const
{
	return m_capturing.load();
}

void TerminalPanel::setCapturing(bool value)
{
	m_capturing.store(value);
}

void TerminalPanel::sendCtrlC()
{
	// Write ETX byte (\x03) to the PTY master. The slave's line discipline
	// converts this to SIGINT for the foreground process group.
	const char etx = '\x03';
	m_pty.write(&etx, 1);
}

bool TerminalPanel::handleEvent(Event event)
{
	if (!m_spawned.load() || m_ptyDead.load())
		return false;

	// Mouse events - don't pass escape sequences to PTY to prevent garbled text.
	// But always consume them (return true) so they don't go to FTXUI/focus.
	if (event.is_mouse())
		return true;

	// Ctrl+T toggles capture mode — releases input to the UI.
	// Check both raw input byte and FTXUI character representation.
	const std::string &raw = event.input();
	if (raw.size() == 1 && raw[0] == '\x14') {
		m_capturing.store(!m_capturing.load());
		m_screen.PostEvent(Event::Custom); // trigger redraw for hint
		return true;
	}

	// Terminal always captures all input when spawned - send to PTY.
	// The m_capturing flag only controls whether we show the "DETACHED" hint.

	std::lock_guard<std::mutex> lock(m_vtermMutex);

	VTermModifier mod = VTERM_MOD_NONE;

	// Ctrl+Alt+J sends a literal newline (LF) for multi-line input.
	if (raw == std::string("\x1b\x0a")) {
		vterm_keyboard_unichar(m_vt, '\n', mod);
		return true;
	}

	// Enter — FTXUI normalizes both Enter and Ctrl+J to Event::Return with
	// raw byte 0x0A.  We send VTERM_KEY_ENTER (produces CR).
	if (event == Event::Return) {
		vterm_keyboard_key(m_vt, VTERM_KEY_ENTER, mod);
		return true;
	}
	if (event == Event::Tab) {
		vterm_keyboard_key(m_vt, VTERM_KEY_TAB, mod);
		return true;
	}
	if (event == Event::Backspace) {
		vterm_keyboard_key(m_vt, VTERM_KEY_BACKSPACE, mod);
		return true;
	}
	if (event == Event::Escape) {
		vterm_keyboard_key(m_vt, VTERM_KEY_ESCAPE, mod);
		return true;
	}
	if (event == Event::ArrowUp) {
		vterm_keyboard_key(m_vt, VTERM_KEY_UP, mod);
		return true;
	}
	if (event == Event::ArrowDown) {
		vterm_keyboard_key(m_vt, VTERM_KEY_DOWN, mod);
		return true;
	}
	if (event == Event::ArrowLeft) {
		vterm_keyboard_key(m_vt, VTERM_KEY_LEFT, mod);
		return true;
	}
	if (event == Event::ArrowRight) {
		vterm_keyboard_key(m_vt, VTERM_KEY_RIGHT, mod);
		return true;
	}
	if (event == Event::Delete) {
		vterm_keyboard_key(m_vt, VTERM_KEY_DEL, mod);
		return true;
	}
	if (event == Event::Home) {
		vterm_keyboard_key(m_vt, VTERM_KEY_HOME, mod);
		return true;
	}
	if (event == Event::End) {
		vterm_keyboard_key(m_vt, VTERM_KEY_END, mod);
		return true;
	}
	if (event == Event::PageUp) {
		vterm_keyboard_key(m_vt, VTERM_KEY_PAGEUP, mod);
		return true;
	}
	if (event == Event::PageDown) {
		vterm_keyboard_key(m_vt, VTERM_KEY_PAGEDOWN, mod);
		return true;
	}
	if (event == Event::Insert) {
		vterm_keyboard_key(m_vt, VTERM_KEY_INS, mod);
		return true;
	}

	// Function keys F1-F12
	static const Event fkeys[] = {
		Event::F1, Event::F2, Event::F3, Event::F4,	 Event::F5,	 Event::F6,
		Event::F7, Event::F8, Event::F9, Event::F10, Event::F11, Event::F12,
	};
	for (int i = 0; i < 12; ++i) {
		if (event == fkeys[i]) {
			vterm_keyboard_key(m_vt,
							   static_cast<VTermKey>(VTERM_KEY_FUNCTION(i + 1)),
							   mod);
			return true;
		}
	}

	// Character input
	if (event.is_character()) {
		std::string input = event.character();
		// Decode UTF-8 to codepoint for vterm
		const auto *bytes =
			reinterpret_cast<const unsigned char *>(input.data());
		uint32_t cp = 0;
		size_t len = input.size();

		if (len == 1) {
			cp = bytes[0];
		} else if (len == 2 && (bytes[0] & 0xE0) == 0xC0) {
			cp = ((bytes[0] & 0x1F) << 6) | (bytes[1] & 0x3F);
		} else if (len == 3 && (bytes[0] & 0xF0) == 0xE0) {
			cp = ((bytes[0] & 0x0F) << 12) | ((bytes[1] & 0x3F) << 6) |
				 (bytes[2] & 0x3F);
		} else if (len == 4 && (bytes[0] & 0xF8) == 0xF0) {
			cp = ((bytes[0] & 0x07) << 18) | ((bytes[1] & 0x3F) << 12) |
				 ((bytes[2] & 0x3F) << 6) | (bytes[3] & 0x3F);
		}

		if (cp > 0) {
			vterm_keyboard_unichar(m_vt, cp, mod);
			return true;
		}
	}

	// Pass any remaining input (Ctrl+key combos, Alt+key, etc.) directly
	// to the PTY as raw bytes. Skip null-only sequences (e.g. Event::Custom).
	if (!raw.empty() && raw.find_first_not_of('\0') != std::string::npos) {
		m_pty.write(raw.data(), raw.size());
		return true;
	}

	return false;
}

// ---------------------------------------------------------------------------
// Component — combines Renderer + CatchEvent
// ---------------------------------------------------------------------------

Component TerminalPanel::component()
{
	// The bool overload of Renderer makes this component focusable,
	// so it receives keyboard events when selected in the tab container.
	auto impl = Renderer([this](bool focused) {
		auto elem = renderScreen();
		if (focused)
			elem = elem | focus;
		return elem | reflect(m_box);
	});
	return impl | CatchEvent([this](Event e) {
			   return wantsEvent(e) && handleEvent(e);
		   });
}

bool TerminalPanel::wantsEvent(ftxui::Event event) const
{
	if (!m_spawned.load() || m_ptyDead.load())
		return false;

	// Always intercept Ctrl+T to toggle capture mode regardless of capture
	// state.
	if (event.input() == std::string("\x14"))
		return true;

	// When not capturing (detached), let events pass through to the UI.
	if (!m_capturing.load())
		return false;

	// When capturing, all input goes to the terminal.
	return true;
}

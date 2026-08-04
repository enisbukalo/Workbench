#pragma once

#include "ILlamaServerProcess.h"

#include <ftxui/dom/elements.hpp>

/**
 * @file serverInfoPanel.h
 * @brief Animated panel displaying an pulsing indicator for server presence.
 *
 * This panel provides a simple visual animation using a frame counter that
 * increments on each render call. The color alternates between red and green
 * every 8 frames to create a continuous pulsing effect:
 *
 * - Frames 0-7: Green light (connected indicator)
 * - Frames 8+: Red dark (disconnected indicator), then cycles back to green
 *
 * At the default ~4fps rendering rate, each color persists for approximately
 * 2 seconds before switching.
 *
 * @note This is a placeholder implementation. A production version would
 *       monitor actual server health via HTTP probes or socket connections,
 *       displaying green only when the inference server responds successfully.
 */
class ServerInfoPanel
{
  public:
	/**
	 * @brief Builds and returns an FTXUI element with animated pulsing
	 * indicator.
	 *
	 * Takes a server reference rather than calling singletons, enabling
	 * unit testing with mocks. The caller (app.cpp) passes singleton instances.
	 *
	 * @param server LlamaServerProcess reference for running state.
	 * @return An @c ftxui::Element containing the animated status display.
	 */
	static ftxui::Element render(ILlamaServerProcess &server);

  private:
};

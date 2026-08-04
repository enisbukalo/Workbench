/**
 * @file temperatureAverager.cpp
 * @brief Implementation of the time-windowed temperature rolling average.
 */

#include "temperatureAverager.h"

TemperatureAverager::TemperatureAverager(std::chrono::seconds window) noexcept
	: m_window(window)
{
}

double TemperatureAverager::add(double value, TimePoint now)
{
	if (value < 0.0) {
		// Unavailable reading: do not store, do not poison the average.
		return UNAVAILABLE;
	}

	m_samples.push_back({ now, value });
	evictExpired(now);

	if (m_samples.empty()) {
		return UNAVAILABLE;
	}

	double sum = 0.0;
	for (const auto &s : m_samples) {
		sum += s.value;
	}
	return sum / static_cast<double>(m_samples.size());
}

std::size_t TemperatureAverager::sampleCount() const noexcept
{
	return m_samples.size();
}

void TemperatureAverager::evictExpired(TimePoint now) noexcept
{
	const TimePoint cutoff = now - m_window;
	while (!m_samples.empty() && m_samples.front().timestamp < cutoff) {
		m_samples.pop_front();
	}
}

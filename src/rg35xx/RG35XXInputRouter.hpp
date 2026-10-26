#ifndef RG35XX_INPUT_ROUTER_HPP
#define RG35XX_INPUT_ROUTER_HPP

#include "RG35XXControls.hpp"
#include "SoftKeyboard.hpp"
#include "TrsKeyboardAdapter.hpp"
#include "RG1Mapping.hpp"

#include <cstdint>
#include <deque>

namespace rg35xx {

class RG35XXInputRouter {
public:
    static const std::uint32_t kRepeatDelayMs = 300;
    static const std::uint32_t kRepeatIntervalMs = 90;
    static constexpr float kAxisEngageThreshold = 0.55f;
    static constexpr float kAxisReleaseThreshold = 0.35f;

    RG35XXInputRouter(RG35XXControls& controls, TrsKeyboardAdapter& keyboard,
                      bool reserve_all_when_hidden = true);

    bool handleSDLEvent(const SDL_Event& event, std::uint32_t now_ms);
    void update(std::uint32_t now_ms);
    void setSuspended(bool suspended, std::uint32_t now_ms);
    void lifecycleCleanup(std::uint32_t now_ms);
    bool guestKeyEvent(int owner, int symbol, bool down);
    // Returns false for a rejected complete frame; the previous map survives.
    bool mappingByte(unsigned char value, std::uint32_t now_ms);
    bool mappingActive() const { return mapping_active_; }

    int popMenuKey();
    void setTextEntry(bool enabled, std::uint32_t now_ms);
    bool textEntry() const;

    bool visible() const;
    bool dirty() const;
    void clearDirty();
    const SoftKeyboard& softKeyboard() const;
    SoftKeyboard& softKeyboardForTesting();
    NavigationDirection navigationDirection() const;

private:
    bool drainControlEvents(std::uint32_t now_ms, bool route_consumption);
    bool transitionConsumed(const ControlEvent& event, bool& passthrough);
    void processControlEvent(const ControlEvent& event, std::uint32_t now_ms);
    void openOverlay(std::uint32_t now_ms);
    void closeOverlay(bool release_all, std::uint32_t now_ms);
    void gateHeldControls();
    void updateDefaultKeys();
    void updateMappedKeys();
    static int defaultButtonSymbol(Button button);
    void updateNavigation(std::uint32_t now_ms);
    NavigationDirection desiredDirection();
    static int hysteresisDirection(float value, int previous);
    static bool deadlineReached(std::uint32_t now, std::uint32_t deadline);
    void markDirty();
    void moveNavigation(NavigationDirection direction);

    RG35XXControls& controls_;
    TrsKeyboardAdapter& keyboard_;
    SoftKeyboard soft_keyboard_;
    std::deque<int> menu_keys_;
    bool text_entry_;
    bool visible_;
    bool dirty_;
    bool suspended_;
    bool reserve_all_when_hidden_;
    bool gated_buttons_[static_cast<std::size_t>(Button::Count)];
    bool gated_axes_[static_cast<std::size_t>(Axis::Count)];
    bool captured_buttons_[static_cast<std::size_t>(Button::Count)];
    bool captured_axes_[static_cast<std::size_t>(Axis::Count)];
    bool passthrough_buttons_[static_cast<std::size_t>(Button::Count)];
    bool passthrough_axes_[static_cast<std::size_t>(Axis::Count)];
    int analog_x_direction_;
    int analog_y_direction_;
    NavigationDirection navigation_direction_;
    std::uint32_t next_repeat_ms_;
    bool b_consumes_shift_;
    RG1Mapping mapping_;
    bool mapping_active_;
    std::string mapping_buffer_;
    bool mapping_overflow_;
    int mapped_axis_directions_[static_cast<std::size_t>(Axis::Count)];
};

} // namespace rg35xx

#endif

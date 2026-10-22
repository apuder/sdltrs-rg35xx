#ifndef RG35XX_CONTROLS_HPP
#define RG35XX_CONTROLS_HPP

#include <SDL.h>
#include <cstddef>
#include <cstdint>

namespace rg35xx {

enum class Button : std::uint8_t {
    A,
    B,
    X,
    Y,
    DPadUp,
    DPadDown,
    DPadLeft,
    DPadRight,
    Start,
    Select,
    Menu,
    L1,
    R1,
    L2,
    R2,
    LeftStick,
    RightStick,
    Count
};

enum class Axis : std::uint8_t {
    LeftX,
    LeftY,
    RightX,
    RightY,
    Count
};

enum class ControlEventType : std::uint8_t {
    ButtonDown,
    ButtonUp,
    AxisChanged
};

struct ControlEvent {
    ControlEventType type;
    std::uint32_t timestamp;
    Button button;
    Axis axis;
    float value;

    static ControlEvent buttonEvent(ControlEventType type, Button button,
                                    std::uint32_t timestamp);
    static ControlEvent axisEvent(Axis axis, float value,
                                  std::uint32_t timestamp);
};

struct ControlState {
    bool buttons[static_cast<std::size_t>(Button::Count)];
    float axes[static_cast<std::size_t>(Axis::Count)];

    ControlState();
    bool pressed(Button button) const;
    float value(Axis axis) const;
};

class RG35XXControls {
public:
    virtual ~RG35XXControls() {}
    virtual bool handleSDLEvent(const SDL_Event& event) = 0;
    virtual void update(std::uint32_t now_ms) = 0;
    virtual bool popEvent(ControlEvent& event) = 0;
    virtual const ControlState& state() const = 0;
    virtual void releaseAll(std::uint32_t now_ms) = 0;
};

} // namespace rg35xx

#endif

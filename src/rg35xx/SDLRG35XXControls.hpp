#ifndef SDL_RG35XX_CONTROLS_HPP
#define SDL_RG35XX_CONTROLS_HPP

#include "RG35XXControls.hpp"

#include <deque>
#include <string>

namespace rg35xx {

enum class SDLButtonBindingKind : std::uint8_t {
    Unmapped,
    Button,
    Hat
};

struct SDLButtonBinding {
    SDLButtonBindingKind kind;
    int index;
    Uint8 hat_mask;

    SDLButtonBinding();
};

struct SDLAxisBinding {
    int index;
    int direction;

    SDLAxisBinding();
};

struct SDLJoystickMapping {
    SDLButtonBinding buttons[static_cast<std::size_t>(Button::Count)];
    SDLAxisBinding axes[static_cast<std::size_t>(Axis::Count)];

    static bool parse(const std::string& text, SDLJoystickMapping& mapping,
                      std::string& error);
    bool validateDevice(SDL_Joystick* joystick, std::string& error) const;
};

/*
 * Stateful raw-event translator kept separate from device ownership so it can
 * be tested without an SDL virtual-joystick API (which SDL 2.0.12 lacks).
 */
class SDLJoystickEventTranslator {
public:
    explicit SDLJoystickEventTranslator(const SDLJoystickMapping& mapping);

    bool handleSDLEvent(const SDL_Event& event, SDL_JoystickID instance_id);
    bool popEvent(ControlEvent& event);
    const ControlState& state() const;
    void releaseAll(std::uint32_t now_ms);
    void suppressHeld(SDL_Joystick* joystick, std::uint32_t now_ms);
    void reconcileSuppressed(SDL_Joystick* joystick, std::uint32_t now_ms);

private:
    static const std::size_t kQueueCapacity = 128;

    bool pushEvent(const ControlEvent& event);
    void setButton(Button button, bool down, std::uint32_t timestamp);
    void setAxis(Axis axis, float value, std::uint32_t timestamp);
    static float normalizeAxis(Sint16 value);

    SDLJoystickMapping mapping_;
    ControlState state_;
    bool suppressed_buttons_[static_cast<std::size_t>(Button::Count)];
    bool suppressed_axes_[static_cast<std::size_t>(Axis::Count)];
    std::deque<ControlEvent> events_;
};

class SDLRG35XXControls : public RG35XXControls {
public:
    SDLRG35XXControls(const SDLJoystickMapping& mapping, int device_index,
                      const std::string& expected_guid);
    ~SDLRG35XXControls() override;

    bool handleSDLEvent(const SDL_Event& event) override;
    void update(std::uint32_t now_ms) override;
    bool popEvent(ControlEvent& event) override;
    const ControlState& state() const override;
    void releaseAll(std::uint32_t now_ms) override;

    bool connected() const;
    const std::string& deviceName() const;
    const std::string& deviceGUID() const;
    const std::string& lastError() const;

private:
    bool tryOpen(std::uint32_t now_ms);
    bool openDevice(int device_index, std::uint32_t now_ms);
    bool deviceMatches(int device_index) const;
    void closeDevice();
    static std::string guidString(SDL_JoystickGUID guid);
    static bool deadlineReached(std::uint32_t now, std::uint32_t deadline);

    SDLJoystickMapping mapping_;
    SDLJoystickEventTranslator translator_;
    int requested_device_index_;
    std::string target_guid_;
    std::string device_name_;
    std::string device_guid_;
    std::string last_error_;
    SDL_Joystick* joystick_;
    SDL_JoystickID instance_id_;
    std::uint32_t next_retry_ms_;
};

} // namespace rg35xx

#endif

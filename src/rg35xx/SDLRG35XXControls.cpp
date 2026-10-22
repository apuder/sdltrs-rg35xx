#include "SDLRG35XXControls.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <vector>

namespace rg35xx {
namespace {

const float kRearmThreshold = 0.35f;
#ifndef RG35XX_TEST_TRANSLATOR_ONLY
const std::uint32_t kReconnectIntervalMs = 1000;
#endif

struct ButtonName {
    const char* name;
    Button button;
};

struct AxisName {
    const char* name;
    Axis axis;
};

const ButtonName kButtonNames[] = {
    {"a", Button::A}, {"b", Button::B}, {"x", Button::X}, {"y", Button::Y},
    {"up", Button::DPadUp}, {"down", Button::DPadDown},
    {"left", Button::DPadLeft}, {"right", Button::DPadRight},
    {"start", Button::Start}, {"select", Button::Select},
    {"menu", Button::Menu}, {"l1", Button::L1}, {"r1", Button::R1},
    {"l2", Button::L2}, {"r2", Button::R2},
    {"ls", Button::LeftStick}, {"rs", Button::RightStick}
};

const AxisName kAxisNames[] = {
    {"lx", Axis::LeftX}, {"ly", Axis::LeftY},
    {"rx", Axis::RightX}, {"ry", Axis::RightY}
};

std::string trim(const std::string& input)
{
    std::size_t first = 0;
    while (first < input.size() &&
           std::isspace(static_cast<unsigned char>(input[first])))
        ++first;
    std::size_t last = input.size();
    while (last > first &&
           std::isspace(static_cast<unsigned char>(input[last - 1])))
        --last;
    return input.substr(first, last - first);
}

std::string lower(const std::string& input)
{
    std::string result(input);
    for (std::size_t i = 0; i < result.size(); ++i)
        result[i] = static_cast<char>(
            std::tolower(static_cast<unsigned char>(result[i])));
    return result;
}

bool parseIndex(const std::string& text, std::size_t first,
                std::size_t last, int& value)
{
    if (first >= last)
        return false;
    errno = 0;
    char* end = NULL;
    const std::string number = text.substr(first, last - first);
    const long parsed = std::strtol(number.c_str(), &end, 10);
    if (errno != 0 || end == number.c_str() || *end != '\0' ||
        parsed < 0 || parsed > std::numeric_limits<Uint8>::max())
        return false;
    value = static_cast<int>(parsed);
    return true;
}

bool findButton(const std::string& name, Button& button)
{
    for (std::size_t i = 0; i < sizeof(kButtonNames) / sizeof(kButtonNames[0]);
         ++i) {
        if (name == kButtonNames[i].name) {
            button = kButtonNames[i].button;
            return true;
        }
    }
    return false;
}

bool findAxis(const std::string& name, Axis& axis)
{
    for (std::size_t i = 0; i < sizeof(kAxisNames) / sizeof(kAxisNames[0]); ++i) {
        if (name == kAxisNames[i].name) {
            axis = kAxisNames[i].axis;
            return true;
        }
    }
    return false;
}

bool isDPad(Button button)
{
    return button == Button::DPadUp || button == Button::DPadDown ||
           button == Button::DPadLeft || button == Button::DPadRight;
}

bool mapped(const SDLButtonBinding& binding)
{
    return binding.kind != SDLButtonBindingKind::Unmapped;
}

bool mapped(const SDLAxisBinding& binding)
{
    return binding.index >= 0;
}

bool sameButtonSource(const SDLButtonBinding& left,
                      const SDLButtonBinding& right)
{
    if (!mapped(left) || !mapped(right) || left.kind != right.kind ||
        left.index != right.index)
        return false;
    return left.kind == SDLButtonBindingKind::Button ||
           left.hat_mask == right.hat_mask;
}

Uint8 hatMask(char direction)
{
    switch (direction) {
        case 'u': return SDL_HAT_UP;
        case 'd': return SDL_HAT_DOWN;
        case 'l': return SDL_HAT_LEFT;
        case 'r': return SDL_HAT_RIGHT;
        default: return 0;
    }
}

} // namespace

SDLButtonBinding::SDLButtonBinding()
    : kind(SDLButtonBindingKind::Unmapped), index(-1), hat_mask(0)
{
}

SDLAxisBinding::SDLAxisBinding() : index(-1), direction(1)
{
}

bool SDLJoystickMapping::parse(const std::string& text,
                               SDLJoystickMapping& mapping,
                               std::string& error)
{
    SDLJoystickMapping parsed;
    std::vector<std::string> entries;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t comma = text.find(',', start);
        entries.push_back(trim(text.substr(start, comma == std::string::npos
            ? std::string::npos : comma - start)));
        if (comma == std::string::npos)
            break;
        start = comma + 1;
    }

    if (text.empty()) {
        error = "SDLTRS_RG35XX_MAP is empty";
        return false;
    }

    for (std::size_t entry_index = 0; entry_index < entries.size();
         ++entry_index) {
        const std::string& entry = entries[entry_index];
        const std::size_t equals = entry.find('=');
        if (entry.empty() || equals == std::string::npos ||
            equals == 0 || equals + 1 >= entry.size() ||
            entry.find('=', equals + 1) != std::string::npos) {
            error = "invalid mapping entry '" + entry + "'";
            return false;
        }

        const std::string name = lower(trim(entry.substr(0, equals)));
        const std::string source = lower(trim(entry.substr(equals + 1)));
        Button button = Button::A;
        Axis axis = Axis::LeftX;

        if (findButton(name, button)) {
            SDLButtonBinding& binding =
                parsed.buttons[static_cast<std::size_t>(button)];
            if (mapped(binding)) {
                error = "duplicate logical control '" + name + "'";
                return false;
            }

            if (source.size() >= 2 && source[0] == 'b') {
                if (!parseIndex(source, 1, source.size(), binding.index)) {
                    error = "invalid button source '" + source + "'";
                    return false;
                }
                binding.kind = SDLButtonBindingKind::Button;
            } else if (source.size() >= 3 && source[0] == 'h') {
                if (!isDPad(button)) {
                    error = "hat source '" + source +
                            "' is valid only for a D-pad direction";
                    return false;
                }
                binding.hat_mask = hatMask(source[source.size() - 1]);
                if (binding.hat_mask == 0 ||
                    !parseIndex(source, 1, source.size() - 1, binding.index)) {
                    error = "invalid hat source '" + source + "'";
                    return false;
                }
                binding.kind = SDLButtonBindingKind::Hat;
            } else {
                error = "button control '" + name +
                    "' requires bN or hN[u|d|l|r]";
                return false;
            }

            for (std::size_t i = 0;
                 i < static_cast<std::size_t>(Button::Count); ++i) {
                if (i != static_cast<std::size_t>(button) &&
                    sameButtonSource(binding, parsed.buttons[i])) {
                    error = "physical source '" + source + "' is mapped twice";
                    return false;
                }
            }
        } else if (findAxis(name, axis)) {
            SDLAxisBinding& binding =
                parsed.axes[static_cast<std::size_t>(axis)];
            if (mapped(binding)) {
                error = "duplicate logical axis '" + name + "'";
                return false;
            }
            std::size_t axis_start = 0;
            if (!source.empty() && source[0] == '-') {
                binding.direction = -1;
                axis_start = 1;
            }
            if (axis_start >= source.size() || source[axis_start] != 'a' ||
                !parseIndex(source, axis_start + 1, source.size(), binding.index)) {
                error = "axis control '" + name + "' requires aN or -aN";
                return false;
            }
            for (std::size_t i = 0;
                 i < static_cast<std::size_t>(Axis::Count); ++i) {
                if (i != static_cast<std::size_t>(axis) &&
                    parsed.axes[i].index == binding.index) {
                    error = "physical axis a" +
                        std::to_string(binding.index) + " is mapped twice";
                    return false;
                }
            }
        } else {
            error = "unknown logical control '" + name + "'";
            return false;
        }
    }

    if (!mapped(parsed.buttons[static_cast<std::size_t>(Button::A)]) ||
        !mapped(parsed.buttons[static_cast<std::size_t>(Button::B)])) {
        error = "mapping must define the physical A and B buttons";
        return false;
    }
    const bool has_dpad =
        mapped(parsed.buttons[static_cast<std::size_t>(Button::DPadUp)]) &&
        mapped(parsed.buttons[static_cast<std::size_t>(Button::DPadDown)]) &&
        mapped(parsed.buttons[static_cast<std::size_t>(Button::DPadLeft)]) &&
        mapped(parsed.buttons[static_cast<std::size_t>(Button::DPadRight)]);
    const bool has_left_stick =
        mapped(parsed.axes[static_cast<std::size_t>(Axis::LeftX)]) &&
        mapped(parsed.axes[static_cast<std::size_t>(Axis::LeftY)]);
    if (!has_dpad && !has_left_stick) {
        error = "mapping must define all four D-pad directions or lx and ly";
        return false;
    }

    mapping = parsed;
    error.clear();
    return true;
}

#ifndef RG35XX_TEST_TRANSLATOR_ONLY
bool SDLJoystickMapping::validateDevice(SDL_Joystick* joystick,
                                        std::string& error) const
{
    if (joystick == NULL) {
        error = "cannot validate a null SDL joystick";
        return false;
    }
    const int button_count = SDL_JoystickNumButtons(joystick);
    const int hat_count = SDL_JoystickNumHats(joystick);
    const int axis_count = SDL_JoystickNumAxes(joystick);
    if (button_count < 0 || hat_count < 0 || axis_count < 0) {
        error = std::string("could not query SDL joystick capabilities: ") +
                SDL_GetError();
        return false;
    }

    for (std::size_t i = 0;
         i < static_cast<std::size_t>(Button::Count); ++i) {
        const SDLButtonBinding& binding = buttons[i];
        if (binding.kind == SDLButtonBindingKind::Button &&
            binding.index >= button_count) {
            error = std::string("logical ") + kButtonNames[i].name +
                    " uses button " + std::to_string(binding.index) +
                    ", but the device reports only " +
                    std::to_string(button_count) + " buttons";
            return false;
        }
        if (binding.kind == SDLButtonBindingKind::Hat &&
            binding.index >= hat_count) {
            error = std::string("logical ") + kButtonNames[i].name +
                    " uses hat " + std::to_string(binding.index) +
                    ", but the device reports only " +
                    std::to_string(hat_count) + " hats";
            return false;
        }
    }
    for (std::size_t i = 0; i < static_cast<std::size_t>(Axis::Count); ++i) {
        if (axes[i].index >= axis_count) {
            error = std::string("logical ") + kAxisNames[i].name +
                    " uses axis " + std::to_string(axes[i].index) +
                    ", but the device reports only " +
                    std::to_string(axis_count) + " axes";
            return false;
        }
    }
    error.clear();
    return true;
}
#endif

SDLJoystickEventTranslator::SDLJoystickEventTranslator(
    const SDLJoystickMapping& mapping)
    : mapping_(mapping)
{
    std::fill(suppressed_buttons_,
              suppressed_buttons_ + static_cast<std::size_t>(Button::Count),
              false);
    std::fill(suppressed_axes_,
              suppressed_axes_ + static_cast<std::size_t>(Axis::Count), false);
}

bool SDLJoystickEventTranslator::pushEvent(const ControlEvent& event)
{
    if (events_.size() >= kQueueCapacity) {
        if (event.type == ControlEventType::ButtonUp ||
            (event.type == ControlEventType::AxisChanged && event.value == 0.0f)) {
            std::deque<ControlEvent>::iterator discard = events_.begin();
            while (discard != events_.end() &&
                   (discard->type == ControlEventType::ButtonUp ||
                    (discard->type == ControlEventType::AxisChanged &&
                     discard->value == 0.0f)))
                ++discard;
            if (discard != events_.end())
                events_.erase(discard);
            else
                return false;
        } else {
            return false;
        }
    }
    events_.push_back(event);
    return true;
}

void SDLJoystickEventTranslator::setButton(Button button, bool down,
                                            std::uint32_t timestamp)
{
    const std::size_t index = static_cast<std::size_t>(button);
    if (state_.buttons[index] == down)
        return;
    const bool old = state_.buttons[index];
    state_.buttons[index] = down;
    if (!pushEvent(ControlEvent::buttonEvent(
            down ? ControlEventType::ButtonDown : ControlEventType::ButtonUp,
            button, timestamp)))
        state_.buttons[index] = old;
}

void SDLJoystickEventTranslator::setAxis(Axis axis, float value,
                                         std::uint32_t timestamp)
{
    const std::size_t index = static_cast<std::size_t>(axis);
    if (std::fabs(state_.axes[index] - value) < 0.00001f)
        return;
    const float old = state_.axes[index];
    state_.axes[index] = value;
    if (!pushEvent(ControlEvent::axisEvent(axis, value, timestamp)))
        state_.axes[index] = old;
}

float SDLJoystickEventTranslator::normalizeAxis(Sint16 value)
{
    return value < 0 ? static_cast<float>(value) / 32768.0f
                     : static_cast<float>(value) / 32767.0f;
}

bool SDLJoystickEventTranslator::handleSDLEvent(const SDL_Event& event,
                                                SDL_JoystickID instance_id)
{
    if (event.type == SDL_JOYBUTTONDOWN || event.type == SDL_JOYBUTTONUP) {
        if (event.jbutton.which != instance_id)
            return false;
        const bool down = event.type == SDL_JOYBUTTONDOWN;
        bool recognized = false;
        for (std::size_t i = 0;
             i < static_cast<std::size_t>(Button::Count); ++i) {
            const SDLButtonBinding& binding = mapping_.buttons[i];
            if (binding.kind != SDLButtonBindingKind::Button ||
                binding.index != event.jbutton.button)
                continue;
            recognized = true;
            if (suppressed_buttons_[i]) {
                if (!down) {
                    suppressed_buttons_[i] = false;
                    pushEvent(ControlEvent::buttonEvent(
                        ControlEventType::ButtonUp,
                        static_cast<Button>(i), event.jbutton.timestamp));
                }
                continue;
            }
            setButton(static_cast<Button>(i), down, event.jbutton.timestamp);
        }
        return recognized;
    }

    if (event.type == SDL_JOYHATMOTION) {
        if (event.jhat.which != instance_id)
            return false;
        bool recognized = false;
        for (std::size_t i = 0;
             i < static_cast<std::size_t>(Button::Count); ++i) {
            const SDLButtonBinding& binding = mapping_.buttons[i];
            if (binding.kind != SDLButtonBindingKind::Hat ||
                binding.index != event.jhat.hat)
                continue;
            recognized = true;
            const bool down = (event.jhat.value & binding.hat_mask) != 0;
            if (suppressed_buttons_[i]) {
                if (!down) {
                    suppressed_buttons_[i] = false;
                    pushEvent(ControlEvent::buttonEvent(
                        ControlEventType::ButtonUp,
                        static_cast<Button>(i), event.jhat.timestamp));
                }
                continue;
            }
            setButton(static_cast<Button>(i), down, event.jhat.timestamp);
        }
        return recognized;
    }

    if (event.type == SDL_JOYAXISMOTION) {
        if (event.jaxis.which != instance_id)
            return false;
        bool recognized = false;
        for (std::size_t i = 0;
             i < static_cast<std::size_t>(Axis::Count); ++i) {
            const SDLAxisBinding& binding = mapping_.axes[i];
            if (binding.index != event.jaxis.axis)
                continue;
            recognized = true;
            const float value = normalizeAxis(event.jaxis.value) *
                                static_cast<float>(binding.direction);
            if (suppressed_axes_[i]) {
                if (std::fabs(value) <= kRearmThreshold) {
                    suppressed_axes_[i] = false;
                    pushEvent(ControlEvent::axisEvent(
                        static_cast<Axis>(i), 0.0f, event.jaxis.timestamp));
                }
                continue;
            }
            setAxis(static_cast<Axis>(i), value, event.jaxis.timestamp);
        }
        return recognized;
    }

    return false;
}

bool SDLJoystickEventTranslator::popEvent(ControlEvent& event)
{
    if (events_.empty())
        return false;
    event = events_.front();
    events_.pop_front();
    return true;
}

const ControlState& SDLJoystickEventTranslator::state() const
{
    return state_;
}

void SDLJoystickEventTranslator::releaseAll(std::uint32_t now_ms)
{
    for (std::size_t i = 0;
         i < static_cast<std::size_t>(Button::Count); ++i) {
        if (state_.buttons[i]) {
            suppressed_buttons_[i] = true;
            setButton(static_cast<Button>(i), false, now_ms);
        }
    }
    for (std::size_t i = 0; i < static_cast<std::size_t>(Axis::Count); ++i) {
        if (state_.axes[i] != 0.0f) {
            suppressed_axes_[i] = true;
            setAxis(static_cast<Axis>(i), 0.0f, now_ms);
        }
    }
}

#ifndef RG35XX_TEST_TRANSLATOR_ONLY
void SDLJoystickEventTranslator::suppressHeld(SDL_Joystick* joystick,
                                              std::uint32_t now_ms)
{
    releaseAll(now_ms);
    std::fill(suppressed_buttons_,
              suppressed_buttons_ + static_cast<std::size_t>(Button::Count),
              false);
    std::fill(suppressed_axes_,
              suppressed_axes_ + static_cast<std::size_t>(Axis::Count), false);
    if (joystick == NULL)
        return;

    SDL_JoystickUpdate();
    for (std::size_t i = 0;
         i < static_cast<std::size_t>(Button::Count); ++i) {
        const SDLButtonBinding& binding = mapping_.buttons[i];
        if (binding.kind == SDLButtonBindingKind::Button)
            suppressed_buttons_[i] =
                SDL_JoystickGetButton(joystick, binding.index) != 0;
        else if (binding.kind == SDLButtonBindingKind::Hat)
            suppressed_buttons_[i] =
                (SDL_JoystickGetHat(joystick, binding.index) &
                 binding.hat_mask) != 0;
    }
    for (std::size_t i = 0; i < static_cast<std::size_t>(Axis::Count); ++i) {
        const SDLAxisBinding& binding = mapping_.axes[i];
        if (binding.index >= 0) {
            const float value = normalizeAxis(
                SDL_JoystickGetAxis(joystick, binding.index));
            suppressed_axes_[i] = std::fabs(value) > kRearmThreshold;
        }
    }
}

void SDLJoystickEventTranslator::reconcileSuppressed(SDL_Joystick* joystick,
                                                     std::uint32_t now_ms)
{
    if (joystick == NULL)
        return;
    bool any_suppressed = false;
    for (std::size_t i = 0;
         i < static_cast<std::size_t>(Button::Count); ++i)
        any_suppressed = any_suppressed || suppressed_buttons_[i];
    for (std::size_t i = 0; i < static_cast<std::size_t>(Axis::Count); ++i)
        any_suppressed = any_suppressed || suppressed_axes_[i];
    if (!any_suppressed)
        return;
    SDL_JoystickUpdate();
    for (std::size_t i = 0;
         i < static_cast<std::size_t>(Button::Count); ++i) {
        if (!suppressed_buttons_[i])
            continue;
        const SDLButtonBinding& binding = mapping_.buttons[i];
        bool down = false;
        if (binding.kind == SDLButtonBindingKind::Button)
            down = SDL_JoystickGetButton(joystick, binding.index) != 0;
        else if (binding.kind == SDLButtonBindingKind::Hat)
            down = (SDL_JoystickGetHat(joystick, binding.index) &
                    binding.hat_mask) != 0;
        if (!down) {
            suppressed_buttons_[i] = false;
            pushEvent(ControlEvent::buttonEvent(
                ControlEventType::ButtonUp, static_cast<Button>(i), now_ms));
        }
    }
    for (std::size_t i = 0; i < static_cast<std::size_t>(Axis::Count); ++i) {
        if (!suppressed_axes_[i])
            continue;
        const SDLAxisBinding& binding = mapping_.axes[i];
        const float value = normalizeAxis(
            SDL_JoystickGetAxis(joystick, binding.index));
        if (std::fabs(value) <= kRearmThreshold) {
            suppressed_axes_[i] = false;
            pushEvent(ControlEvent::axisEvent(
                static_cast<Axis>(i), 0.0f, now_ms));
        }
    }
}

SDLRG35XXControls::SDLRG35XXControls(const SDLJoystickMapping& mapping,
                                     int device_index,
                                     const std::string& expected_guid)
    : mapping_(mapping),
      translator_(mapping),
      requested_device_index_(device_index),
      target_guid_(lower(expected_guid)),
      joystick_(NULL),
      instance_id_(-1),
      next_retry_ms_(0)
{
    tryOpen(SDL_GetTicks());
}

SDLRG35XXControls::~SDLRG35XXControls()
{
    closeDevice();
}

std::string SDLRG35XXControls::guidString(SDL_JoystickGUID guid)
{
    char text[33] = {};
    SDL_JoystickGetGUIDString(guid, text, sizeof(text));
    return lower(text);
}

bool SDLRG35XXControls::deviceMatches(int device_index) const
{
    if (device_index < 0 || device_index >= SDL_NumJoysticks())
        return false;
    if (!target_guid_.empty())
        return guidString(SDL_JoystickGetDeviceGUID(device_index)) == target_guid_;
    return device_index == requested_device_index_;
}

bool SDLRG35XXControls::openDevice(int device_index, std::uint32_t now_ms)
{
    if (!deviceMatches(device_index))
        return false;
    SDL_Joystick* opened = SDL_JoystickOpen(device_index);
    if (opened == NULL) {
        last_error_ = std::string("SDL_JoystickOpen failed: ") + SDL_GetError();
        return false;
    }
    std::string validation_error;
    if (!mapping_.validateDevice(opened, validation_error)) {
        last_error_ = "SDL joystick mapping does not match the selected "
                      "device: " + validation_error;
        SDL_JoystickClose(opened);
        return false;
    }
    const SDL_JoystickID id = SDL_JoystickInstanceID(opened);
    if (id < 0) {
        last_error_ = std::string("SDL_JoystickInstanceID failed: ") +
                      SDL_GetError();
        SDL_JoystickClose(opened);
        return false;
    }

    joystick_ = opened;
    instance_id_ = id;
    const char* name = SDL_JoystickName(joystick_);
    device_name_ = name != NULL ? name : "unnamed joystick";
    device_guid_ = guidString(SDL_JoystickGetGUID(joystick_));
    if (target_guid_.empty())
        target_guid_ = device_guid_;
    last_error_.clear();
    translator_.suppressHeld(joystick_, now_ms);
    std::fprintf(stderr,
        "SDLTRS RG35XX: SDL joystick connected: %s (instance %d, GUID %s)\n",
        device_name_.c_str(), static_cast<int>(instance_id_),
        device_guid_.c_str());
    return true;
}

bool SDLRG35XXControls::tryOpen(std::uint32_t now_ms)
{
    if (joystick_ != NULL)
        return true;
    const int count = SDL_NumJoysticks();
    bool matched = false;
    for (int index = 0; index < count; ++index) {
        if (!deviceMatches(index))
            continue;
        matched = true;
        if (openDevice(index, now_ms))
            return true;
    }
    if (matched && !last_error_.empty())
        return false;
    if (count == 0)
        last_error_ = "no SDL joysticks are currently connected";
    else if (!target_guid_.empty())
        last_error_ = "no SDL joystick matches GUID " + target_guid_;
    else
        last_error_ = "SDL joystick index " +
                      std::to_string(requested_device_index_) +
                      " is not available";
    return false;
}

void SDLRG35XXControls::closeDevice()
{
    if (joystick_ != NULL)
        SDL_JoystickClose(joystick_);
    joystick_ = NULL;
    instance_id_ = -1;
    device_name_.clear();
    device_guid_.clear();
}

bool SDLRG35XXControls::handleSDLEvent(const SDL_Event& event)
{
    if (event.type == SDL_JOYDEVICEREMOVED &&
        event.jdevice.which == instance_id_) {
        translator_.releaseAll(event.jdevice.timestamp);
        std::fprintf(stderr, "SDLTRS RG35XX: SDL joystick disconnected; "
                     "waiting for GUID %s\n", target_guid_.c_str());
        closeDevice();
        next_retry_ms_ = event.jdevice.timestamp;
        return true;
    }
    if (event.type == SDL_JOYDEVICEADDED && joystick_ == NULL) {
        next_retry_ms_ = event.jdevice.timestamp + kReconnectIntervalMs;
        return tryOpen(event.jdevice.timestamp);
    }
    if (joystick_ == NULL)
        return false;
    return translator_.handleSDLEvent(event, instance_id_);
}

bool SDLRG35XXControls::deadlineReached(std::uint32_t now,
                                        std::uint32_t deadline)
{
    return static_cast<std::int32_t>(now - deadline) >= 0;
}

void SDLRG35XXControls::update(std::uint32_t now_ms)
{
    if (joystick_ == NULL && deadlineReached(now_ms, next_retry_ms_)) {
        tryOpen(now_ms);
        next_retry_ms_ = now_ms + kReconnectIntervalMs;
    }
    if (joystick_ != NULL)
        translator_.reconcileSuppressed(joystick_, now_ms);
}

bool SDLRG35XXControls::popEvent(ControlEvent& event)
{
    return translator_.popEvent(event);
}

const ControlState& SDLRG35XXControls::state() const
{
    return translator_.state();
}

void SDLRG35XXControls::releaseAll(std::uint32_t now_ms)
{
    translator_.releaseAll(now_ms);
}

bool SDLRG35XXControls::connected() const
{
    return joystick_ != NULL;
}

const std::string& SDLRG35XXControls::deviceName() const
{
    return device_name_;
}

const std::string& SDLRG35XXControls::deviceGUID() const
{
    return device_guid_;
}

const std::string& SDLRG35XXControls::lastError() const
{
    return last_error_;
}
#endif

} // namespace rg35xx

#ifndef RG35XX_SYSTEM_VOLUME_HPP
#define RG35XX_SYSTEM_VOLUME_HPP

#include <SDL.h>
#include <cstdint>
#include <memory>

namespace rg35xx {

long stockVolumeMixerLevel(int level);

long nextVolumeLevel(long current, long minimum, long maximum,
                     long step, int direction);

class VolumeMixer {
public:
    virtual ~VolumeMixer() {}
    virtual bool adjust(int direction) = 0;
};

// Shared button policy for SDL and Linux input, independent of guest input.
class VolumeButtons {
public:
    explicit VolumeButtons(VolumeMixer& mixer);
    void key(bool up, bool down, std::uint32_t now);
    void update(std::uint32_t now);
    void release();
private:
    VolumeMixer& mixer_;
    bool held_[2];
    std::uint32_t repeat_at_;
};

class SystemVolume {
public:
    SystemVolume();
    ~SystemVolume();
    bool handleSDLEvent(const SDL_Event& event, std::uint32_t now);
    void update(std::uint32_t now);
private:
    struct Device;
    std::unique_ptr<Device> device_;
};

} // namespace rg35xx
#endif

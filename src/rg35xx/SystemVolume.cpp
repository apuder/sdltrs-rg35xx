#include "SystemVolume.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef __linux__
#include <fcntl.h>
#include <linux/input.h>
#include <sound/asound.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace rg35xx {

long stockVolumeMixerLevel(int level)
{
    // Verified against volumeCtrl.dge on RG35XXPRO StockOS 20260522.
    static const long levels[] = {0, 5, 10, 16, 19, 21, 23, 25, 27, 29, 31};
    return levels[std::max(0, std::min(10, level))];
}

long nextVolumeLevel(long current, long minimum, long maximum,
                     long step, int direction)
{
    current = std::max(minimum, std::min(maximum, current));
    step = std::max(1L, std::min(maximum - minimum, step));
    if (direction > 0) return current + std::min(step, maximum - current);
    if (direction < 0) return current - std::min(step, current - minimum);
    return current;
}

VolumeButtons::VolumeButtons(VolumeMixer& mixer) : mixer_(mixer), repeat_at_(0)
{
    release();
}

void VolumeButtons::release() { held_[0] = held_[1] = false; }

void VolumeButtons::key(bool up, bool down, std::uint32_t now)
{
    const int index = up ? 1 : 0;
    if (held_[index] == down) return;
    held_[index] = down;
    if (down && !held_[1 - index]) mixer_.adjust(up ? 1 : -1);
    repeat_at_ = now + 350;
}

void VolumeButtons::update(std::uint32_t now)
{
    if (held_[0] == held_[1]) return;
    if (static_cast<std::int32_t>(now - repeat_at_) < 0) return;
    // Never replay a backlog after a blocking dialog or a stalled frame.
    mixer_.adjust(held_[1] ? 1 : -1);
    repeat_at_ = now + 100;
}

#ifdef __linux__
struct SystemVolume::Device : VolumeMixer {
    int mixer_fd;
    int input_fd;
    snd_ctl_elem_info info;
    VolumeButtons buttons;
    bool sync_lost;
    bool reported_error;
    bool stock_state;

    static const char* stockStatePath()
    { return "/sys/class/power_supply/axp2202-battery/openbor_volume"; }

    Device() : mixer_fd(-1), input_fd(-1), info(), buttons(*this),
               sync_lost(false), reported_error(false), stock_state(false) {}
    ~Device()
    {
        if (input_fd >= 0) close(input_fd);
        if (mixer_fd >= 0) close(mixer_fd);
        // The hardware volume deliberately survives process exit.
    }

    bool openStockMixer()
    {
        mixer_fd = open("/dev/snd/controlC0", O_RDWR | O_NONBLOCK | O_CLOEXEC);
        if (mixer_fd < 0) return false;
        info.id.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
        std::strcpy(reinterpret_cast<char*>(info.id.name), "lineout volume");
        if (ioctl(mixer_fd, SNDRV_CTL_IOCTL_ELEM_INFO, &info) < 0) return false;
        return info.type == SNDRV_CTL_ELEM_TYPE_INTEGER &&
            (info.access & SNDRV_CTL_ELEM_ACCESS_READWRITE) == SNDRV_CTL_ELEM_ACCESS_READWRITE &&
            !(info.access & SNDRV_CTL_ELEM_ACCESS_INACTIVE) &&
            info.count > 0 && info.count <= 128 &&
            info.value.integer.min >= 0 && info.value.integer.max <= 100000 &&
            info.value.integer.min < info.value.integer.max;
    }

    bool readStockLevel(int& level)
    {
        FILE* file = std::fopen(stockStatePath(), "r");
        if (!file) return false;
        const bool valid = std::fscanf(file, "%d", &level) == 1 && level >= 0 && level <= 10;
        std::fclose(file);
        if (!valid) errno = EINVAL;
        return valid;
    }

    bool initializeStockLevel()
    {
        stock_state = access(stockStatePath(), F_OK) == 0;
        if (!stock_state) return true;
        // StockOS stores a logical 0..10 value separately from the ALSA mixer.
        // Honor that shared setting at launch, as the native volume helper does.
        if (info.value.integer.min != 0 || info.value.integer.max != 31) {
            errno = EINVAL;
            return failure();
        }
        return adjust(0);
    }

    bool adjust(int direction) override
    {
        int stock_level = 0;
        int stock_fd = -1;
        if (stock_state) {
            if (!readStockLevel(stock_level)) return failure();
            stock_level = static_cast<int>(nextVolumeLevel(stock_level, 0, 10, 1, direction));
            stock_fd = open(stockStatePath(), O_WRONLY | O_CLOEXEC);
            if (stock_fd < 0) return failure();
        }
        snd_ctl_elem_value value = {};
        value.id = info.id;
        // Read for every adjustment: never overwrite another app's newer level
        // using a cached or emulator-private volume setting.
        if (ioctl(mixer_fd, SNDRV_CTL_IOCTL_ELEM_READ, &value) < 0) {
            if (stock_fd >= 0) close(stock_fd);
            return failure();
        }
        const snd_ctl_elem_value previous = value;
        bool changed = false;
        for (unsigned int i = 0; i < info.count; ++i) {
            const long old_level = value.value.integer.value[i];
            value.value.integer.value[i] = stock_state ? stockVolumeMixerLevel(stock_level) :
                nextVolumeLevel(old_level, info.value.integer.min, info.value.integer.max,
                                info.value.integer.step, direction);
            changed = changed || old_level != value.value.integer.value[i];
        }
        if (changed && ioctl(mixer_fd, SNDRV_CTL_IOCTL_ELEM_WRITE, &value) < 0) {
            if (stock_fd >= 0) close(stock_fd);
            return failure();
        }
        if (stock_fd >= 0) {
            char text[16];
            const int length = std::snprintf(text, sizeof(text), "%d", stock_level);
            const ssize_t written = write(stock_fd, text, length);
            const int saved_errno = written < 0 ? errno : EIO;
            close(stock_fd);
            if (written != length) {
                // Keep hardware and shared state consistent if saving fails.
                snd_ctl_elem_value restore = previous;
                ioctl(mixer_fd, SNDRV_CTL_IOCTL_ELEM_WRITE, &restore);
                errno = saved_errno;
                return failure();
            }
        }
        return true;
    }

    bool failure()
    {
        if (!reported_error) {
            std::fprintf(stderr, "SDLTRS RG35XX: system volume adjustment failed: %s\n",
                         std::strerror(errno));
            reported_error = true;
        }
        buttons.release();
        return false;
    }

    void openInput()
    {
        // Volume buttons need not be part of the SDL joystick. Observe evdev
        // without grabbing it, so all other input remains available to SDL.
        for (int i = 0; i < 64; ++i) {
            char path[64];
            std::snprintf(path, sizeof(path), "/dev/input/event%d", i);
            const int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
            if (fd < 0) continue;
            unsigned char bits[(KEY_MAX + 8) / 8] = {};
            if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(bits)), bits) >= 0 &&
                (bits[KEY_VOLUMEUP / 8] & (1 << (KEY_VOLUMEUP % 8))) &&
                (bits[KEY_VOLUMEDOWN / 8] & (1 << (KEY_VOLUMEDOWN % 8)))) {
                input_fd = fd;
                std::fprintf(stderr, "SDLTRS RG35XX: system volume buttons on %s\n", path);
                return;
            }
            close(fd);
        }
        std::fprintf(stderr, "SDLTRS RG35XX: no readable volume evdev device; using SDL volume keys\n");
    }

    void update(std::uint32_t now)
    {
        if (input_fd >= 0) {
            input_event event;
            ssize_t length;
            // Bound work per frame even if a device floods the input stream.
            for (int i = 0; i < 128; ++i) {
                length = read(input_fd, &event, sizeof(event));
                if (length < 0 && errno == EINTR) continue;
                if (length < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
                if (length != sizeof(event)) {
                    buttons.release();
                    close(input_fd);
                    input_fd = -1;
                    break;
                }
                if (event.type == EV_SYN && event.code == SYN_DROPPED) {
                    buttons.release();
                    sync_lost = true;
                }
                if (sync_lost) {
                    // Ignore the incomplete batch; require a fresh press.
                    if (event.type == EV_SYN && event.code == SYN_REPORT) sync_lost = false;
                    continue;
                }
                if (event.type == EV_KEY && (event.value == 0 || event.value == 1) &&
                    (event.code == KEY_VOLUMEUP || event.code == KEY_VOLUMEDOWN))
                    buttons.key(event.code == KEY_VOLUMEUP, event.value == 1, now);
            }
        }
        buttons.update(now);
    }
};
#else
struct SystemVolume::Device {};
#endif

SystemVolume::SystemVolume()
{
#ifdef __linux__
    const char* mode = std::getenv("SDLTRS_RG35XX_VOLUME");
    if (mode && std::strcmp(mode, "off") == 0) return;
    // Enable only on StockOS; do not take over a desktop or another firmware's
    // volume daemon. StockOS shared volume is applied at launch and left on exit.
    if (access("/mnt/vendor/oem/board.ini", F_OK) != 0) return;
    std::unique_ptr<Device> device(new Device());
    if (!device->openStockMixer()) {
        std::fprintf(stderr, "SDLTRS RG35XX: stock system volume unavailable (lineout volume)\n");
        return;
    }
    if (!device->initializeStockLevel()) return;
    device->openInput();
    device_ = std::move(device);
#endif
}

SystemVolume::~SystemVolume() {}

bool SystemVolume::handleSDLEvent(const SDL_Event& event, std::uint32_t now)
{
#ifdef __linux__
    if (!device_) return false;
    if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_FOCUS_LOST)
        device_->buttons.release();
    if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) {
        const SDL_Keycode key = event.key.keysym.sym;
        if (key != SDLK_VOLUMEUP && key != SDLK_VOLUMEDOWN) return false;
        // evdev and SDL can report the same press. Pick one source, never both.
        if (device_->input_fd < 0 && !event.key.repeat)
            device_->buttons.key(key == SDLK_VOLUMEUP, event.type == SDL_KEYDOWN, now);
        return true;
    }
#else
    (void)event;
    (void)now;
#endif
    return false;
}

void SystemVolume::update(std::uint32_t now)
{
#ifdef __linux__
    if (device_) device_->update(now);
#else
    (void)now;
#endif
}

} // namespace rg35xx

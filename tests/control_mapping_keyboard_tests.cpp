// Exercise the real C keyboard matrix through the production C++ bridge.
#include "../src/rg35xx/rg35xx_bridge.h"

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <string>

extern "C" {
#include "../src/trs.h"
#include "../src/trs_clones.h"
#include "../src/trs_sdl_keyboard.h"

int trs_model = 3;
int turbo_mode = 1;
struct z80_state_struct z80_state;
struct trs_clones trs_clones;

// Serialization is outside this test's keyboard/input boundary.
void trs_save_int(FILE*, const int*, int) { std::abort(); }
void trs_load_int(FILE*, int*, int) { std::abort(); }
void trs_save_uint64(FILE*, const Uint64*) { std::abort(); }
void trs_load_uint64(FILE*, Uint64*) { std::abort(); }
}

static int failures;
#define EXPECT(condition) do { \
    if (!(condition)) { \
        std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
        ++failures; \
    } \
} while (0)

static void mapping(const char* text)
{
    while (*text) EXPECT(rg35xx_bridge_mapping_out(*text++) == 1);
    EXPECT(rg35xx_bridge_mapping_out(0) == 1);
}

static void button(SDL_Scancode scancode, bool down)
{
    SDL_Event event = {};
    event.type = down ? SDL_KEYDOWN : SDL_KEYUP;
    event.key.keysym.scancode = scancode;
    EXPECT(rg35xx_bridge_handle_event(&event, SDL_GetTicks()) == 1);
}

static void flushHostKeys()
{
    trs_kb_heartbeat();
    trs_kb_heartbeat();
    trs_kb_heartbeat();
    trs_kb_mem_read(0xFF);
}

static void testMultiKeyActions()
{
    rg35xx_bridge_lifecycle_cleanup(SDL_GetTicks());
    flushHostKeys();
    mapping("R1:SPACE,R2:LEFT+RIGHT"); // Missile Defense's actual CSV binding.
    button(SDL_SCANCODE_3, true);
    EXPECT(trs_kb_mem_read(0x40) == 0x60); // Both arrows in the first matrix read.
    rg35xx_bridge_update(SDL_GetTicks() + 1000);
    EXPECT(trs_kb_mem_read(0x40) == 0x60);
    button(SDL_SCANCODE_E, true);
    EXPECT(trs_kb_mem_read(0x40) == 0xe0); // Independent R1 fire remains active.
    button(SDL_SCANCODE_E, false);
    button(SDL_SCANCODE_LEFT, true);
    flushHostKeys();
    button(SDL_SCANCODE_3, false);
    EXPECT(trs_kb_mem_read(0x40) == 0x20); // Default D-pad still owns LEFT.
    button(SDL_SCANCODE_LEFT, false);
    flushHostKeys();
    EXPECT(trs_kb_mem_read(0xFF) == 0);

    // One control can hold every supported letter, digit, and named key.
    std::string keys = "@";
    for (char c = 'A'; c <= 'Z'; ++c) { keys += '+'; keys += c; }
    for (char c = '0'; c <= '9'; ++c) { keys += '+'; keys += c; }
    keys += "+ENTER+CLEAR+BREAK+SPACE+UP+DOWN+LEFT+RIGHT+COMMA+PERIOD+SEMICOLON+SHIFT";
    mapping(("R2:" + keys).c_str());
    button(SDL_SCANCODE_3, true);
    const int rows[] = {0xff, 0xff, 0xff, 0x07, 0xff, 0x5b, 0xff, 0x01};
    for (int row = 0; row < 8; ++row)
        EXPECT(trs_kb_mem_read(1 << row) == rows[row]);
    button(SDL_SCANCODE_F6, true); // A also owns ENTER through its default.
    flushHostKeys();
    button(SDL_SCANCODE_3, false);
    for (int row = 0; row < 8; ++row)
        EXPECT(trs_kb_mem_read(1 << row) == (row == 6 ? 1 : 0));
    button(SDL_SCANCODE_F6, false);
    flushHostKeys();
    EXPECT(trs_kb_mem_read(0xFF) == 0);
    mapping("");
}

int main()
{
    SDL_setenv("SDLTRS_RG35XX_INPUT", "keyboard", 1);
    EXPECT(rg35xx_bridge_init() == 1);
    for (trs_model = 1; trs_model <= 4; ++trs_model) {
        trs_kb_reset();
        mapping("");
        const SDL_Scancode shoulders[] = {
            SDL_SCANCODE_Q, SDL_SCANCODE_1, SDL_SCANCODE_E, SDL_SCANCODE_3
        };
        for (unsigned int i = 0; i < 4; ++i) {
            button(shoulders[i], true);
            flushHostKeys();
            EXPECT(trs_kb_mem_read(0xFF) == 0);
            button(shoulders[i], false);
        }
        mapping("L1:1,L2:2,R1:3,R2:4");
        for (unsigned int i = 0; i < 4; ++i) button(shoulders[i], true);
        EXPECT(trs_kb_mem_read(0x10) == 0x1e);
        for (unsigned int i = 0; i < 4; ++i) button(shoulders[i], false);
        EXPECT(trs_kb_mem_read(0xFF) == 0);
        mapping("A:A,B:SHIFT+BREAK,R1:LEFT+RIGHT,R2:SPACE,L1:SPACE,X:@,Y:SEMICOLON");
        button(SDL_SCANCODE_F6, true); // Printed A.
        EXPECT(trs_kb_mem_read(0x01) == 0x02);
        EXPECT(trs_kb_mem_read(0x80) == 0); // A identifies a physical key.
        button(SDL_SCANCODE_F6, false);
        button(SDL_SCANCODE_SPACE, true); // Printed B.
        EXPECT(trs_kb_mem_read(0x40) == 0x04);
        EXPECT(trs_kb_mem_read(0x80) == 0x01);

        // Host lowercase translation forces SHIFT off; mapped SHIFT still asserts.
        const int host_owner = rg35xx_bridge_physical_owner(SDL_SCANCODE_Z);
        EXPECT(rg35xx_bridge_guest_key_event(host_owner, 'a', 1));
        flushHostKeys();
        EXPECT(trs_kb_mem_read(0x01) == 0x02);
        EXPECT(trs_kb_mem_read(0x80) == 0x01);
        button(SDL_SCANCODE_SPACE, false);
        EXPECT(trs_kb_mem_read(0x80) == 0);

        // Releasing the mapping leaves the host's same physical key down.
        button(SDL_SCANCODE_F6, true);
        button(SDL_SCANCODE_F6, false);
        EXPECT(trs_kb_mem_read(0x01) == 0x02);
        EXPECT(rg35xx_bridge_guest_key_event(host_owner, 'a', 0));
        flushHostKeys();
        EXPECT(trs_kb_mem_read(0x01) == 0);

        // Conversely, host key-up cannot release an active mapping.
        button(SDL_SCANCODE_F6, true);
        EXPECT(rg35xx_bridge_guest_key_event(host_owner, 'a', 1));
        EXPECT(rg35xx_bridge_guest_key_event(host_owner, 'a', 0));
        flushHostKeys();
        EXPECT(trs_kb_mem_read(0x01) == 0x02);
        button(SDL_SCANCODE_F6, false);
        button(SDL_SCANCODE_E, true); // R1: physically opposing arrows.
        EXPECT(trs_kb_mem_read(0x40) == 0x60);
        button(SDL_SCANCODE_E, false);
        button(SDL_SCANCODE_Q, true);
        button(SDL_SCANCODE_3, true);
        button(SDL_SCANCODE_Q, false);
        EXPECT(trs_kb_mem_read(0x40) == 0x80);
        button(SDL_SCANCODE_3, false);
        EXPECT(trs_kb_mem_read(0x40) == 0);
        button(SDL_SCANCODE_TAB, true);
        button(SDL_SCANCODE_BACKSPACE, true);
        EXPECT(trs_kb_mem_read(0x01) == 0x01); // @ without synthesized SHIFT.
        EXPECT(trs_kb_mem_read(0x20) == 0x08); // Physical semicolon.
        EXPECT(trs_kb_mem_read(0x40) == 0); // Custom X replaces default CLEAR.
        EXPECT(trs_kb_mem_read(0x80) == 0);
        mapping("");
        EXPECT(trs_kb_mem_read(0xFF) == 0);
        rg35xx_bridge_lifecycle_cleanup(SDL_GetTicks());
        mapping("L1:1,L2:2,Y:D");
        button(SDL_SCANCODE_Q, true);
        EXPECT(trs_kb_mem_read(0x10) == 0x02);
        button(SDL_SCANCODE_Q, false);
        button(SDL_SCANCODE_1, true);
        EXPECT(trs_kb_mem_read(0x10) == 0x04);
        button(SDL_SCANCODE_1, false);
        button(SDL_SCANCODE_BACKSPACE, true);
        EXPECT(trs_kb_mem_read(0x01) == 0x10); // Printed Y holds physical D.
        EXPECT(trs_kb_mem_read(0x80) == 0);
        button(SDL_SCANCODE_BACKSPACE, false);
        // Cosmic inherits A=ENTER, B=BREAK, SELECT=keyboard, no START key.
        button(SDL_SCANCODE_RETURN, true);
        flushHostKeys();
        EXPECT(trs_kb_mem_read(0x40) == 0);
        button(SDL_SCANCODE_RETURN, false);
        button(SDL_SCANCODE_F6, true);
        flushHostKeys();
        EXPECT(trs_kb_mem_read(0x40) == 0x01);
        button(SDL_SCANCODE_F6, false);
        button(SDL_SCANCODE_SPACE, true);
        flushHostKeys();
        EXPECT(trs_kb_mem_read(0x40) == 0x04);
        button(SDL_SCANCODE_SPACE, false);
        button(SDL_SCANCODE_TAB, true);
        flushHostKeys();
        EXPECT(trs_kb_mem_read(0x40) == 0x02); // Inherited X holds CLEAR.
        button(SDL_SCANCODE_TAB, false);
        button(SDL_SCANCODE_LEFT, true);
        flushHostKeys();
        EXPECT(trs_kb_mem_read(0x40) == 0x20);
        button(SDL_SCANCODE_LEFT, false);
        flushHostKeys();
        EXPECT(trs_kb_mem_read(0x40) == 0);
        button(SDL_SCANCODE_ESCAPE, true);
        flushHostKeys();
        EXPECT(trs_kb_mem_read(0x40) == 0); // SELECT does not assert CLEAR.
        button(SDL_SCANCODE_ESCAPE, false); // Tap opens the keyboard.
        button(SDL_SCANCODE_RETURN, true);
        flushHostKeys();
        EXPECT(trs_kb_mem_read(0x40) == 0); // START stays unassigned in overlay.
        button(SDL_SCANCODE_RETURN, false);
        button(SDL_SCANCODE_F6, true);
        flushHostKeys();
        EXPECT(trs_kb_mem_read(0x40) == 0x01); // A sends ENTER in overlay too.
        button(SDL_SCANCODE_F6, false);
        button(SDL_SCANCODE_SPACE, true);
        flushHostKeys();
        EXPECT(trs_kb_mem_read(0x04) == 0x02); // B types selected Q in overlay.
        EXPECT(trs_kb_mem_read(0x40) == 0);
        button(SDL_SCANCODE_SPACE, false);
        button(SDL_SCANCODE_ESCAPE, true);
        button(SDL_SCANCODE_ESCAPE, false); // Close without typing or CLEAR.
        flushHostKeys();
        EXPECT(trs_kb_mem_read(0xFF) == 0);

        mapping("START:SPACE,L1:A,SELECT+R1:SHIFT+BREAK");
        button(SDL_SCANCODE_RETURN, true);
        flushHostKeys();
        EXPECT(trs_kb_mem_read(0x40) == 0x80); // Custom START assigns SPACE.
        button(SDL_SCANCODE_V, true); // Default right-stick click also owns SPACE.
        button(SDL_SCANCODE_RETURN, false);
        flushHostKeys();
        EXPECT(trs_kb_mem_read(0x40) == 0x80);
        button(SDL_SCANCODE_V, false);
        flushHostKeys();
        EXPECT(trs_kb_mem_read(0x40) == 0);
        button(SDL_SCANCODE_Q, true);
        flushHostKeys();
        EXPECT(trs_kb_mem_read(0x01) == 0x02);
        EXPECT(trs_kb_mem_read(0x10) == 0); // Custom L1 sends only A.
        button(SDL_SCANCODE_Q, false);
        button(SDL_SCANCODE_ESCAPE, true);
        flushHostKeys();
        EXPECT(trs_kb_mem_read(0x40) == 0);
        button(SDL_SCANCODE_E, true); // SELECT+R1 consumes the keyboard toggle.
        flushHostKeys();
        EXPECT(trs_kb_mem_read(0x40) == 0x04);
        EXPECT(trs_kb_mem_read(0x80) == 0x01);
        button(SDL_SCANCODE_E, false);
        flushHostKeys();
        EXPECT(trs_kb_mem_read(0x40) == 0); // SELECT remains a consumed modifier.
        EXPECT(trs_kb_mem_read(0x80) == 0);
        button(SDL_SCANCODE_ESCAPE, false);
        flushHostKeys();
        mapping("");
        testMultiKeyActions();
    }
    rg35xx_bridge_shutdown();
    if (failures) return EXIT_FAILURE;
    std::puts("Custom mapping real keyboard matrix integration tests passed");
    return EXIT_SUCCESS;
}

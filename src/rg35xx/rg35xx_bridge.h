#ifndef RG35XX_BRIDGE_H
#define RG35XX_BRIDGE_H

#include <SDL.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int rg35xx_bridge_init(void);
int rg35xx_bridge_enabled(void);
/* Claims port 41 only while the RG35XX backend is enabled. */
int rg35xx_bridge_mapping_out(int value);
void rg35xx_bridge_shutdown(void);
int rg35xx_bridge_handle_event(const SDL_Event *event, uint32_t now_ms);
int rg35xx_bridge_menu_key(void);
void rg35xx_bridge_text_entry(int enabled, uint32_t now_ms);
void rg35xx_bridge_update(uint32_t now_ms);
int rg35xx_bridge_volume_event(const SDL_Event *event, uint32_t now_ms);
void rg35xx_bridge_volume_update(uint32_t now_ms);
int rg35xx_bridge_dirty(void);
void rg35xx_bridge_draw(SDL_Renderer *renderer);
void rg35xx_bridge_modal_begin(uint32_t now_ms);
void rg35xx_bridge_modal_end(uint32_t now_ms);
void rg35xx_bridge_lifecycle_cleanup(uint32_t now_ms);
int rg35xx_bridge_guest_key_event(int owner, int symbol, int down);
int rg35xx_bridge_physical_owner(int scancode);

#ifdef __cplusplus
}
#endif

#endif /* RG35XX_BRIDGE_H */

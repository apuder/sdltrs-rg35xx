#ifndef _TRS_MEMORY_H
#define _TRS_MEMORY_H

#include <SDL_types.h>

/* Locations for Model I, Model III, and Model 4 map 0 */
#define VIDEO_START     (0x3C00)
#define KEYBOARD_START  (0x3800)
#define RAM_START       (0x4000)

/* Memory Expansion Cards */
#define GENIEPLUS       (1) /* EACA EG 3200 Genie III 384 KB */
#define HUFFMAN         (2) /* Dave Huffman (and other) 2 MB (4/4P) */
#define HYPERMEM        (3) /* Anitek HyperMem 1 MB (4/4P) */
#define LUBOMIR         (4) /* Lubomir Soft Banker TRS-80 Model I */
#define MEGAMEM         (5) /* Anitek MegaMem  3 MB (III/4/4P) */
#define RAM192B         (6) /* TCS Genie IIs/SpeedMaster 768 KB */
#define SELECTOR        (7) /* Selector Card for TRS-80 Model I */
#define SUPERMEM        (8) /* Alpha Technology SuperMem 1024 KB */
#define TCS_GENIE3S     (9) /* TCS Genie IIIs 1024 KB */

extern int    trs80_model3_mem_read(int address);
extern void   trs80_model3_mem_write(int address, int value);
extern Uint8 *trs80_model3_mem_addr(int address, int writing);

extern int    mem_peek(int address);
extern void   mem_poke(int address, int value);
extern int    mem_read(int address);
extern void   mem_write(int address, int value);
extern void   rom_write(int address, int value);
extern int    mem_read_word(int address);
extern void   mem_write_word(int address, int value);
extern Uint8 *mem_pointer(int address, int writing);

extern void   mem_video_page(int offset);
extern Uint8  mem_video_page_read(int vaddr);
extern int    mem_video_page_write(int vaddr, Uint8 value);
extern Uint8 *mem_video_page_addr(int vaddr);

extern int    get_mem_map(void);
extern void   mem_bank(int which);
extern void   mem_map(int which);
extern void   mem_card_out(int card, int bits);
extern int    mem_card_in(int card);
extern void   mem_romin(int state);

extern void   megamem_out(int mem_slot, Uint8 value);

extern void   ct80_ramdisk_out(int port, Uint8 byte);
extern Uint8  ct80_ramdisk_in(int port);
extern void   ct80_video_addr(int address);

extern void   eg64_mba_out(int value);
extern void   lnw80_bank_out(int value);
extern void   s80z_out(int value);
extern void   sys_byte_out(int value);
extern int    sys_byte_in(void);
extern int    sys_byte_2_in(void);

#endif /* _TRS_MEMORY_H */

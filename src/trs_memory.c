/*
 * Copyright (C) 1992 Clarendon Hill Software.
 *
 * Permission is granted to any individual or institution to use, copy,
 * or redistribute this software, provided this copyright notice is retained.
 *
 * This software is provided "as is" without any expressed or implied
 * warranty.  If this software brings on any sort of damage -- physical,
 * monetary, emotional, or brain -- too bad.  You've got no one to blame
 * but yourself.
 *
 * The software may be modified for your own purposes, but modified versions
 * must retain this notice.
 */

/*
 * Copyright (c) 1996-2020, Timothy P. Mann
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/*
 * trs_memory.c -- memory emulation functions for the TRS-80 emulator
 *
 * Routines in this file perform operations such as mem_read and mem_write,
 * and are the top level emulation points for memory-mapped devices such
 * as the screen and keyboard.
 */

/*#define MEMDEBUG 1*/

#include <stdlib.h>
#include <string.h>
#include "error.h"
#include "trs.h"
#include "trs_clones.h"
#include "trs_cp500.h"
#include "trs_disk.h"
#include "trs_memory.h"
#include "trs_imp_exp.h"
#include "trs_state_save.h"
#include "trs_uart.h"

#define MAX_ROM_SIZE       (16384)  /* 16K for CP-300/500 */
#define MAX_VIDEO_SIZE     (3072)   /* CP-500 M80 has 3K */
#define MAX_MEMORY_SIZE    (4 * 1024 * 1024) + 65536
/* Start MegaMem > 1MB to leave space for SuperMem or HyperMem */
#define MEGAMEM_START      (1 * 1024 * 1024) + 65536

/* Model I printer addresses 0x37E8 to 0x37EB */
#define PRINTER_1(addr)    (((addr) &~ 3) == 0x37E8)
/* Model III/4/4P printer addresses 0x37E8 and 0x37E9: Read-Only */
#define PRINTER_3(addr)    (((addr) &~ 1) == 0x37E8)

/* Interrupt latch register in EI (Model I) */
#define TRS_INTLATCH(addr) (((addr) &~ 3) == 0x37E0)

/* Check address in video memory */
#define VIDEO_ADDR(vaddr)  (Uint16)vaddr < MAX_VIDEO_SIZE

int lowercase = 1;
int trs_model = 1;
int trs_rom_size;
int huffman;  /* Dave Huffman (4/4P) */
int hypermem; /* Anitek HyperMem (4/4P) */
int lubomir;  /* Lubomir Soft Banker */
int megamem;  /* Anitek MegaMem (III/4/4P) */
int selector; /* Selector (TRS-80 Model I) */
int supermem; /* Alpha Technology SuperMem */

/* private data */

/* +1 so strings from mem_pointer are NUL-terminated */
static Uint8 memory[MAX_MEMORY_SIZE + 1];
static Uint8 video[MAX_VIDEO_SIZE + 1];
static Uint8 rom[MAX_ROM_SIZE + 1];
static int memory_map;
static int video_addr = -VIDEO_START;
static int bank_offset[2];
static Uint32 bank_base = 0x10000;
static int megamem_addr;
static Uint32 megamem_base;
static Uint8 mem_command;
static int romin; /* Model 4p */
static int supermem_base;
static Uint32 supermem_high;
static int selector_reg;
static int sys_byte;
static int sys_byte_2; /* LNW80 II and TCS Genie IIIs */

static Uint8 mem_video_read(int vaddr)
{
  if (VIDEO_ADDR(vaddr)) {
    return video[vaddr];
  } else { /* emulator bug, should never happen */
#if MEMDEBUG
    error("Reading video address %04X out of range [%04X]", vaddr, MAX_VIDEO_SIZE);
#endif
    return 0xFF;
  }
}

static int mem_video_write(int vaddr, Uint8 value)
{
  if (VIDEO_ADDR(vaddr)) {
    if (video[vaddr] != value) {
      video[vaddr] = value;
      return 1;
    } else {
      return 0;
    }
  } else { /* emulator bug, should never happen */
#if MEMDEBUG
    error("Writing %02X to video address %04X out of range [%04X]",
        value, vaddr, MAX_VIDEO_SIZE);
#endif
    return 0;
  }
}

void mem_video_page(int offset)
{
  video_addr = -VIDEO_START + offset;
}

Uint8 mem_video_page_read(int vaddr)
{
  return mem_video_read(vaddr + video_addr);
}

int mem_video_page_write(int vaddr, Uint8 value)
{
  return mem_video_write(vaddr + video_addr, value);
}

Uint8 *mem_video_page_addr(int vaddr)
{
  vaddr = vaddr + video_addr;
  if (VIDEO_ADDR(vaddr)) {
    return video + vaddr;
  } else { /* emulator bug, should never happen */
#if MEMDEBUG
    error("Video page address %04X out of range [%04X]", vaddr, MAX_VIDEO_SIZE);
#endif
    return NULL;
  }
}

void mem_bank(int command)
{
  switch (command) {
    case 0:
      /* L64 Lower / Upper */
      bank_offset[0] = 0 << 15;
      bank_offset[1] = 0 << 15;
      break;
    case 2:
      /* L64 Lower / H64 Lower */
      bank_offset[0] = 0 << 15;
      bank_offset[1] = bank_base - (1 << 15);
      break;
    case 3:
      /* L64 Lower / H64 upper */
      bank_offset[0] =  0 << 15;
      bank_offset[1] = (0 << 15) + bank_base;
      break;
    case 6:
      /* H64 Lower / L64 upper */
      bank_offset[0] = (0 << 15) + bank_base;
      bank_offset[1] =  0 << 15;
      break;
    case 7:
      /* H64 Upper / L64 Upper */
      bank_offset[0] = (1 << 15) + bank_base;
      bank_offset[1] =  0 << 15;
      break;
    default:
      error("unknown mem_bank command %d", command);
      break;
  }
  mem_command = command;
}

/*
 * Dave Huffman (and some other) memory expansions. These decode
 * port 0x94 off U50 as follows
 *
 * 7: only used with Z180 board (not emulated - would need Z180 emulation!)
 * 6: write protect - not emulated
 * 5: sometimes used for > 4MHz turbo mod
 * 4-0: Bits A20-A16 of the alt bank
 *
 * Set to 1 on a reset so that you get the 'classic' memory map
 * This port is read-write and the drivers depend upon it
 * (See RAMDV364.ASM)
 *
 * The Hypermem is very similar and also changes the upper
 * 64K bank between multiple banks. However the values
 * are on port 0x90 (sound) bits 4-1, which is a much poorer
 * design IMHO as sound using apps can randomly change the
 * upper bank. Fine for a ramdisc but means other software
 * must take great care.
 */

void mem_card_out(int card, int bits)
{
  switch (card) {
    case GENIEPLUS:
      /* Genieplus Memory Card */
      bank_base = (bits & 0x07) << 16;
      if (bank_base) {
        /* Select upper 32K of bank */
        if (bits & (1 << 3))
          bank_base += 32768;
      }
      break;
    case HUFFMAN:
      bank_base = (bits & 0x1F) << 16;
      mem_bank(mem_command);
      break;
    case HYPERMEM:
      /* HyperMem replaces the upper 64K bank with multiple
         banks according to port 0x90 bits 4-1.
         0 base is upper bank of 64K */
      bank_base = ((bits & 0x1E) + 2) << 15;
      mem_bank(mem_command);
      break;
    case LUBOMIR:
      memory_map = bits ? 0x22 : 0x10;
      sys_byte = bits;
      break;
    case RAM192B:
      /* TCS Genie IIs/SpeedMaster RAM 192 B */
      bank_base = ((bits & 0x0C) * 192) /* card */
                + ((bits & 0x30) *  48) /* block */
                * 1024 + 65536;
      mem_command = bits;
      break;
    case SELECTOR:
      /* Not all bits are necessarily really present but hey what
         you can't read back you can't tell */
      selector_reg = bits;
      /* Always Model I */
      memory_map = 0x10 + (selector_reg & 7);
      /* 0x10 is already the default tandy map we add 11-17 in the style
         of the model 4 approach */
      if (selector_reg & 0x8) {
        /* External RAM enabled */
        /* Effectively the selector bits << 15 */
        /* Low 64K is the base memory */
        bank_base = 32768 + ((selector_reg & 0xF0) << 11);
        /* Now are we mapping it high or low */
        if (selector_reg & 1) /* Low */
          bank_base += 32768;
      } else
        bank_base = 0;
      break;
    case SUPERMEM:
      /* Support 1024 KB which is the full range of the mapping. */
      supermem_base = (bits & 0x1F) << 15;
      /* The supermem can flip the low or high 32K.
         Set bit 5 to map low */
      supermem_high = (bits & 0x20) ? 0x0000 : 0x8000;
      break;
    case TCS_GENIE3S:
      if (bits == mem_command)
        return;

      mem_command = bits;

      /* D0/D1: 256K block */
      bank_offset[0] = (bits & 0x03) << 18;
      /* D7: Common area */
      bank_offset[1] = (bits & 0x80) ? bank_offset[0] : 0;
      break;
    default:
      break;
  }
}

int mem_card_in(int card)
{
  switch (card) {
    case HUFFMAN:
      return (bank_base >> 16) & 0x1F;
    case RAM192B:
    case TCS_GENIE3S:
      return (mem_command);
    case SUPERMEM:
      return (supermem_base >> 15) |
             ((supermem_high == 0) ? 0x20 : 0);
    default:
      /* And the HyperMem appears to be write-only */
      return 0xFF;
  }
}

void megamem_out(int mem_slot, Uint8 value)
{
  if (mem_slot == 0 && value == 0) {
    megamem_addr = 0;
    megamem_base = 0;
  } else {
    megamem_addr = (value & 0xC0) * 256;
    megamem_base = (((value - (value & 0xC0)) * 16)
                 + (mem_slot * 1024)) * 1024
                 + MEGAMEM_START;
  }
}

void eg3200_bank_out(int value)
{
  sys_byte = value & 0x0F;
}

void eg3200_init_out(int value)
{
  memory_map = 0x23;
  sys_byte = value;
  trs_clones_model(EG3200);
  trs_timer_init();
}

void eg64_mba_out(int value)
{
  /* Disable EG-64 MBA */
  if (value == 7) {
    memory_map = 0x10;
    sys_byte = 0;
    return;
  }

  if (value & (1 << 4))
    /* ROM access */
    sys_byte &= ~(1 << (value & 7));
  else
    /* RAM access */
    sys_byte |=  (1 << (value & 7));

  memory_map = 0x21;
}

void genie3s_bank_out(int value)
{
  if (value == sys_byte_2)
    return;

  /* Redraw if Font-SRAM is disabled */
  if ((value & (1 << 1)) == 0 && (sys_byte_2 & (1 << 1)))
    trs_screen_refresh();

  sys_byte_2 = value;

  /* Bit 7 : Bit 6 : Bank# : Memory
   *   0   :   0   :   0   :    64K
   *   0   :   1   :   1   :   128K
   *   1   :   0   :   2   :   192K
   *   1   :   1   :   3   :   256K
   */
  bank_base = (value & 0xC0) << 10;
}

void genie3s_init_out(int value)
{
  genie3s_bank_out(value);
  mem_video_page(0);
  memory_map = 0x24;
  trs_clones_model(GENIE3S);
  trs_timer_init();
}

void genie3s_sys_out(int value)
{
  if (value == sys_byte)
    return;

  /* HRG page */
  if ((value & (1 << 1)) != (sys_byte & (1 << 1)))
    genie3s_hrg((value & (1 << 1)) != 0);

  /* Slow-Down on ROM access */
  if ((value & (1 << 2)) != (sys_byte & (1 << 2)))
    trs_timer_speed((value & (1 << 2)) != 0);

  sys_byte = value;
}

void lnw80_bank_out(int value)
{
  if (value == sys_byte_2)
    return;

  /* LNW80 II 80x24 text mode video page */
  if (sys_byte & (1 << 7))
    mem_video_page(value & (1 << 3) ? 1024 : 0);

  /* Disable or enable interrupts */
  if ((value & (1 << 2)) != (sys_byte_2 & (1 << 2))) {
    if (value & (1 << 2))
      z80_state.iff1 = z80_state.iff2 = 0;
    else
      z80_state.iff1 = z80_state.iff2 = 1;
  }

  /* LNW80 II Expander RAM */
  bank_base = (value & (1 << 6)) << 9;

  sys_byte_2 = value;
}

void sys_byte_out(int value)
{
  /* Hack for Schmidtke-CP/M with Doppelbauer banking */
  if (memory_map == 0x20) {
    if (value & (1 << 4))
      sys_byte |=  (1 << 3);
    else
      sys_byte &= ~(1 << 3);
    return;
  }

  if (value == sys_byte)
    return;

  switch (speedup) {
    case 4: /* Banking-Modification from Martin Doppelbauer */
      memory_map = (value & (1 << 4)) ? 0x14 : 0x10;
      break;
    case 5: /* Aster CT-80 */
      if ((value & (1 << 0)) != (sys_byte & (1 << 0)))
        trs_timer_speed(value & (1 << 0));
      break;
    case 6: /* LNW80/II */
      if ((value & (1 << 7)) != (sys_byte & (1 << 7)))
        trs_screen_80x24(value & (1 << 7));
      if ((value & (1 << 0)) != (sys_byte & (1 << 0)))
        trs_screen_inverse(value & 1);
      /* Fall through - HRG on/off bit is identical */
    case 7: /* TCS Genie IIs/SpeedMaster */
      if ((value & (1 << 1)) != (sys_byte & (1 << 1)))
        hrg_onoff((value & (1 << 1)) ? 2 : 0);
      break;
  }

  sys_byte = value;
}

int sys_byte_in(void)
{
  return sys_byte;
}

int sys_byte_2_in(void)
{
  return sys_byte_2;
}

void s80z_out(int value)
{
  if (value & (1 << 2)) {
    if (speedup == 4)
      video_addr = 0x3900; /* Homebrew 80*22 SYS80.SYS */
    else
      video_addr = (value & (1 << 1)) ? 0xB000 : 0xF000;
  } else {
    video_addr = VIDEO_START;
  }

  if ((value & (1 << 6)) != (sys_byte & (1 << 6)))
    trs_screen_inverse((value & (1 << 6)) != 0);

  sys_byte = value;
  memory_map = 0x20;
}

static void mem_init(void)
{
  /* Initialize RAM & ROM */
  Uint8 const fill = trs_model >= 4 ? 0xFF : 0xC9;

  memset(&memory, fill, MAX_MEMORY_SIZE);
  memset(&rom, 0xFF, MAX_ROM_SIZE);

  mem_map(0);
  mem_bank(0);
  mem_video_page(0);

  clear_key_queue(); /* init the key queue */
}

/* Handle reset button if poweron=0;
   handle hard reset or initial poweron if poweron=1 */
void trs_reset(int poweron)
{
  bank_base = 0x10000;
  bank_offset[0] = 0;
  bank_offset[1] = 0;
  megamem_addr = 0;
  megamem_base = 0;
  mem_command = 0;
  romin = 0;
  supermem_base = 0;
  supermem_high = 0x8000;
  trs_emu_mouse = 0;

  /* Close disks opened by Z80 programs */
  do_emt_resetdisk();
  /* Reset devices (Model I SYSRES, Model III/4 RESET) */
  trs_cassette_reset();
  trs_disk_init(poweron); /* also inits trs_hard and trs_stringy */
  trs_uart_init();

  trs_kb_reset(); /* Part of keyboard stretch kludge */
  trs_cancel_event();
  trs_timer_interrupt(0);

  if (poweron || trs_model >= 4 || trs_clones.model == GENIE3S) {
    if (poweron || trs_model >= 4)
      mem_init();
    trs_rom_init();
    /* Blank Video */
    memset(&video, ' ', MAX_VIDEO_SIZE);
    m6845_crtc_reset();
    /* No quirks */
    trs_clones_model(0);
    trs_screen_reset();
    trs_screen_init(1);
    trs_timer_init();

    if (trs_show_led) {
      trs_disk_led(-1, -1);
      trs_hard_led(-1, -1);
    }

    /* Reset processor */
    z80_reset();
  } else {
    /* Signal a nonmaskable interrupt. */
    trs_reset_button_interrupt(1);
    trs_schedule_event(trs_reset_button_interrupt, 0, 2000);
    trs_screen_refresh();
  }

  if (trs_model == 5) {
    /* Switch in boot ROM */
    z80_out(0x9C, 1);
  }

  if (trs_model >= 4) {
    /* Turn off various memory map and video mode bits */
    z80_out(0x84, 0);
    if (huffman)
      z80_out(0x94, 0);
    if (hypermem)
      z80_out(0x90, 0);
  }

  if (trs_model >= 3) {
    grafyx_write_mode(0);
    trs_interrupt_mask_write(0);
    trs_nmi_mask_write(0);
    trs_screen_80x24(0);
  }

  if (trs_model == 3) {
    grafyx_m3_reset();
    mem_map(0);
    trs_clones_model(0);
  }

  if (trs_model == 1) {
    hrg_onoff(0); /* Switch off HRG1B hi-res graphics. */
    bank_base = 0;
    selector_reg = 0;
    sys_byte = sys_byte_2 = 0;
    trs_interrupt_latch_clear();
    trs_screen_inverse(0);
    trs_screen_80x24(0);

    switch (speedup) {
      case 5: /* Aster CT-80 */
        memory_map = 0x25;
        trs_clones_model(CT80);
        break;
      case 6: /* LNW80/II */
        memory_map = 0x26;
        trs_clones_model(LNW80);
        break;
      case 7: /* TCS SpeedMaster 5.3 */
        memory_map = 0x27;
        trs_clones_model(SPEEDMASTER);
        break;
    }
  }
}

int get_mem_map(void)
{
  return memory_map;
}

void mem_map(int which)
{
  memory_map = which + (trs_model << 4) + (romin << 2);
}

void mem_romin(int state)
{
  romin = (state & 1);
  memory_map = (memory_map & ~4) + (romin << 2);
}

/*
 * hack to let us initialize the ROM memory
 */
void rom_write(int address, int value)
{
  if (address < MAX_ROM_SIZE)
    rom[address] = value;
}

static int trs80_model1_ram(int address)
{
  if (selector) {
    int const addr = address;
    /* Bank low on odd modes */
    int const bank = (selector_reg & 1) == 1 ? 0 : 0x8000;

    /* Selector mode 6 remaps RAM from 0000-3FFF to C000-FFFF while keeping
       the ROMs visible */
    if ((selector_reg & 7) == 6 && address >= 0xC000) {
      /* Use the low 16K, and then bank it. I'm not 100% sure how the
         PAL orders the two */
      address &= 0x3FFF;
    }
    /* Deal with 32K banking from selector */
    if ((addr & 0x8000) == bank)
      address += bank_base;
  }

  return memory[address];
}

static int trs80_model1_mmio(int address)
{
  if (address >= VIDEO_START) return video[address - VIDEO_START];
  if (address < trs_rom_size) return rom[address];
  if (TRS_INTLATCH(address)) return trs_interrupt_latch_read();
  if (address == TRSDISK_DATA) return trs_disk_data_read();
  if (address == TRSDISK_STATUS) return trs_disk_status_read();
  if (address == TRSDISK_TRACK) return trs_disk_track_read();
  if (address == TRSDISK_SECTOR) return trs_disk_sector_read();
  /* With a selector 768 bytes poke through the hole */
  if (address >= 0x3900 && selector) return trs80_model1_ram(address);
  if (address >= KEYBOARD_START) return trs_kb_mem_read(address);
  if (PRINTER_1(address)) return trs_printer_read();
#if MEMDEBUG
  error("Invalid read of address %04X, returning FF [PC=%04X, mem_map=%02X]",
      address, Z80_PC, memory_map);
#endif
  return 0xFF;
}

int trs80_model3_mem_read(int address)
{
  if (address >= RAM_START) return memory[address];
  if (address >= VIDEO_START) return grafyx_m3_read_byte(address - VIDEO_START);
  if (address >= KEYBOARD_START) return trs_kb_mem_read(address);
  if (PRINTER_3(address)) return trs_printer_read();
  if (address < trs_rom_size) return rom[address];
#if MEMDEBUG
  error("Invalid read of address %04X, returning FF [PC=%04X, mem_map=%02X]",
      address, Z80_PC, memory_map);
#endif
  return 0xFF;
}

int mem_read(int address)
{
  address &= 0xFFFF; /* allow callers to be sloppy */

  /* There are some adapters that sit above the system and
     either intercept before the hardware proper, or adjust
     the address. Deal with these first so that we take their
     output and feed it into the memory map */

  /* Anitek MegaMem */
  if (megamem_addr) {
    if (address >= megamem_addr && address <= megamem_addr + 0x3FFF)
      return memory[megamem_base + (address & 0x3FFF)];
  }

  /* The SuperMem sits between the system and the Z80 */
  if (supermem && supermem_base) {
    if (!((address ^ supermem_high) & 0x8000))
      return memory[supermem_base + (address & 0x7FFF)];
    /* Otherwise the request comes from the system */
  }

  switch (memory_map) {
    case 0x10: /* Model I */
      if (address < RAM_START)
        return trs80_model1_mmio(address);
      else
        return trs80_model1_ram(address);

    case 0x11: /* Model I: selector mode 1 (all RAM except I/O high) */
      if (address >= 0xF7E0 && address <= 0xF7FF)
        return trs80_model1_mmio(address & 0x3FFF);
      else
        return trs80_model1_ram(address);

    case 0x12: /* Model I: selector mode 2 (ROM disabled) */
      if (address >= 0x37E0 && address < RAM_START)
        return trs80_model1_mmio(address);
      else
        return trs80_model1_ram(address);

    case 0x13: /* Model I: selector mode 3 (CP/M mode) */
      if (address >= 0xF7E0)
        return trs80_model1_mmio(address & 0x3FFF);
      /* Fall through */

    case 0x14: /* Model I: All RAM banking high */
    case 0x15: /* Model I: All RAM banking low */
      return trs80_model1_ram(address);

    case 0x16: /* Model I: Low 16K in top 16K */
      if (address < RAM_START)
        return trs80_model1_mmio(address);
      else
        return trs80_model1_ram(address);

    case 0x17: /* Model I: Described in the selector doc as 'not useful' */
      break;   /* Not clear what really happens */

    case 0x20: /* Schmidtke 80-Z Video Card */
      if (sys_byte & (1 << 0)) {
        if (address >= video_addr && address <= video_addr + 0xFFF)
          return video[((address - video_addr) & 0x7FF)];
      }
      if ((sys_byte & (1 << 3)) || address >= RAM_START)
        return memory[address];
      else
        return trs80_model1_mmio(address);

    case 0x21: /* EG-64 Memory-Banking-Adaptor */
      if (address < RAM_START) {
        if (((sys_byte & (1 << 0)) && address <= 0x2FFF) ||
            ((sys_byte & (1 << 2)) && address >= 0x3000 && address <= 0x35FF) ||
            ((sys_byte & (1 << 4)) && address >= 0x3600 && address <= 0x37FF) ||
            ((sys_byte & (1 << 5)) && address >= 0x3800 && address <= 0x3BFF) ||
            ((sys_byte & (1 << 6)) && address >= 0x3C00))
          return memory[address];
        else
          return trs80_model1_mmio(address);
      }
      return memory[address];

    case 0x22: /* Lubomir Soft Banker */
      if (address < RAM_START) {
        if (((sys_byte & (1 << 6)) && address <= 0x37DF) ||
            ((sys_byte & (1 << 5)) && address >= 0x37E0))
          return memory[address];
        else
          return trs80_model1_mmio(address);
      }
      if ((sys_byte & (1 << 4)) && address >= 0x8000)
        /* Read from "Expander RAM" */
        return memory[address + 0x8000];
      else
        return memory[address];

    case 0x23: /* EG 3200: bit set to 0 => bank enabled */
      /* Bit 0 - Bank 1: ROM/EPROM */
      if ((sys_byte & (1 << 0)) == 0 && address < trs_rom_size)
        return rom[address];
      /* Bit 1 - Bank 2: Video Memory 0 (1k, 64x16, TRS-80 M1 compatible) */
      if ((sys_byte & (1 << 1)) == 0) {
        if (address >= VIDEO_START && address <= 0x3FFF)
          return video[address - VIDEO_START];
      }
      /* Bit 2 - Bank 3: Video Memory 1 (additional 1k for 80x24 video mode) */
      if ((sys_byte & (1 << 2)) == 0) {
        if (address >= 0x4000 && address <= 0x43FF)
          return video[address - VIDEO_START];
      }
      /* Bit 3 - Bank 4: Disk I/O and Keyboard */
      if ((sys_byte & (1 << 3)) == 0) {
        if (address >= 0x37E0 && address <= 0x37EF)
          return trs80_model1_mmio(address);
        if (address >= KEYBOARD_START && address <= 0x3BFF)
          return trs_kb_mem_read(address);
      }
      /* Bank 0: RAM */
      if (address <= 0x7FFF) /* Low 32 KB for Genieplus Banking */
        return memory[address + bank_base];
      else
        return memory[address];

    case 0x24: /* TCS Genie IIIs */
      if ((sys_byte & (1 << 0)) == 0) {
        if (sys_byte & (1 << 4)) {
          /* 2K Video RAM */
          if (address >= KEYBOARD_START && address <= 0x3FFF)
            return video[(address + video_addr) & 0x7FF];
        } else {
          if (address >= VIDEO_START && address <= 0x3FFF)
            return video[address + video_addr];
          if (address >= KEYBOARD_START && address <= 0x38FF)
            return trs_kb_mem_read(address);
        }
        /* Disk I/O */
        if (address >= 0x37E0 && address <= 0x37EF)
          return trs80_model1_mmio(address);
      }
      /* ROM/EPROM */
      if ((sys_byte & (1 << 2)) == 0 && address <= 0x2FFF)
        return rom[address];
      /* HRG */
      if ((sys_byte & (1 << 3)) && address >= 0x8000)
        return genie3s_hrg_read(address - 0x8000);
      /* "Constant bit" points to Bank 0 */
      if ((address <= 0x3FFF && (sys_byte_2 & (1 << 0)) == 0) ||
          (address >= 0xE000 && (sys_byte_2 & (1 << 0))))
        return memory[address + bank_offset[1]];
      else
        return memory[address + bank_offset[0] + bank_base];

    case 0x25: /* Aster CT-80 */
      if ((sys_byte & (1 << 5)) == 0) { /* device bank */
        /* Boot-ROM */
        if ((sys_byte & (1 << 1)) && address <= 0x7FF)
          return rom[address | 0x3000];
        if ((sys_byte & (1 << 2)) == 0 && address <= 0x2FFF)
          return rom[address];
        if ((sys_byte & (1 << 3)) == 0) {
          /* TRS-80 mode */
          if (address >= 0x3000 && address <= 0x3FFF)
            return trs80_model1_mmio(address);
        } else {
          /* CP/M mode */
          /* 2K Video RAM */
          if (address >= 0xF800)
            return video[address - 0xF800];
          /* Keyboard */
          if (address >= 0xF400 && address <= 0xF7FF)
            return trs_kb_mem_read(address - 0xBC00);
          /* Disk MMIO */
          if (address >= 0xEFE0 && address <= 0xEFEF)
            return trs80_model1_mmio(address - 0xB800);
          /* Boot-ROM copy */
          if (address >= 0xEC00 && address <= 0xF3FF)
            return rom[address - 0xBC00];
        }
      }
      return memory[address];

    case 0x26: /* LNW80/II */
      if ((sys_byte_2 & (1 << 0)) == 0) {
        if (address < RAM_START) {
          if (sys_byte & (1 << 3)) {
            /* HRG in low 16K */
            hrg_write_addr(address, 0x3FFF);
            return hrg_read_data();
          }
          if (sys_byte_2 & (1 << 1) && address <= 0x3700)
            return memory[address];
          if (address >= VIDEO_START)
            return mem_video_page_read(address);
          if (address >= 0x37E0 && address <= 0x37EF)
            return trs80_model1_mmio(address);
          if (address >= KEYBOARD_START && address <= 0x38FF)
            return trs_kb_mem_read(address);
          if (PRINTER_1(address))
            return trs_printer_read();
          if (address <= 0x2FFF)
            return rom[address];
        }
      } else {
        if (address >= 0xF7E0 && address <= 0xF8FF)
          return trs80_model1_mmio(address & 0x3FFF);
      }
      /* LNW80 II 32K Expander-RAM without CP/M MMIO area */
      if (address >= 0x8000 && address <= 0xF6FF)
        return memory[address + bank_base];
      else
        return memory[address];

    case 0x27: /* TCS Genie IIs/SpeedMaster */
      /* Expansions bit (RAM 192 B) */
      if ((sys_byte & (1 << 7)) && address <= 0xBFFF)
        return memory[address + bank_base];
      /* HRG in low 16K */
      if ((sys_byte & (1 << 3)) && address <= 0x3FFF) {
        hrg_write_addr(address, 0x3FFF);
        return hrg_read_data();
      }
      /* ROM and MMIO */
      if ((sys_byte & (1 << 0)) == 0) {
        if ((sys_byte & (1 << 2)) == 0 && address <= 0x2FFF)
          return rom[address];
        if (address >= 0x3400 && address <= 0x3FFF)
          return trs80_model1_mmio(address);
      }
      return memory[address];

    case 0x30: /* Model III */
      return trs80_model3_mem_read(address);

    case 0x31: /* CP-500 */
    case 0x32: /* CP-500 64K RAM */
    case 0x33: /* CP-500 80x24 video */
      return cp500_mem_read(address, memory_map, rom, memory);

    case 0x40: /* Model 4 map 0 */
      if (address >= RAM_START) {
        return memory[address + bank_offset[address >> 15]];
      }
      if (address >= VIDEO_START) return mem_video_page_read(address);
      if (address >= KEYBOARD_START) return trs_kb_mem_read(address);
      if (PRINTER_3(address)) return trs_printer_read();
      if (address < trs_rom_size) return rom[address];
      break;

    case 0x54: /* Model 4P map 0, boot ROM in */
    case 0x55: /* Model 4P map 1, boot ROM in */
      if (address < trs_rom_size) return rom[address];
      /* else fall thru */
    case 0x41: /* Model 4 map 1 */
    case 0x50: /* Model 4P map 0, boot ROM out */
    case 0x51: /* Model 4P map 1, boot ROM out */
      if (address >= RAM_START || address < KEYBOARD_START) {
        return memory[address + bank_offset[address >> 15]];
      }
      if (address >= VIDEO_START) return mem_video_page_read(address);
      if (address >= KEYBOARD_START) return trs_kb_mem_read(address);
      break;

    case 0x42: /* Model 4 map 2 */
    case 0x52: /* Model 4P map 2, boot ROM out */
    case 0x56: /* Model 4P map 2, boot ROM in */
      if (address < 0xF400) {
        return memory[address + bank_offset[address >> 15]];
      }
      if (address >= 0xF800) return video[address - 0xF800];
      return trs_kb_mem_read(address);

    case 0x43: /* Model 4 map 3 */
    case 0x53: /* Model 4P map 3, boot ROM out */
    case 0x57: /* Model 4P map 3, boot ROM in */
      return memory[address + bank_offset[address >> 15]];

  }

  return 0xFF;
}

static void lnw80_screen_write_char(int vaddr, int value)
{
  if (mem_video_page_write(vaddr, value)) {
    vaddr += video_addr;
    if (vaddr >= 1536)
      trs_screen_write_char((vaddr & 15) + 64 +
         (((vaddr - 1536) / 64 + ((vaddr >> 4) & 3) * 8) * 80), value);
    else
      trs_screen_write_char((vaddr % 64) + ((vaddr / 64) * 80), value);
  }
}

static void trs80_screen_write_char(int vaddr, int value)
{
  if (mem_video_write(vaddr, value))
    trs_screen_write_char(vaddr, value);
}

static void trs80_model1_write_mem(int address, int value)
{
  if (selector) {
    int const addr = address;
    /* Bank low on odd modes */
    int const bank = (selector_reg & 1) == 1 ? 0 : 0x8000;

    /* Selector mode 6 remaps RAM from 0000-3FFF to C000-FFFF while keeping
       the ROMs visible */
    if ((selector_reg & 7) == 6 && address >= 0xC000) {
      /* We have no low 16K of RAM. This is for the LNW80 really */
      if (!(selector_reg & 8))
        return;
      /* Use the low 16K, and then bank it. I'm not 100% sure how the
         PAL orders the two */
      address &= 0x3FFF;
    }
    /* Deal with 32K banking from selector */
    if ((addr & 0x8000) == bank)
      address += bank_base;
  }

  memory[address] = value;
}

static void trs80_model1_write_mmio(int address, int value)
{
  if (address >= VIDEO_START) {
    if (lowercase == 0) {
      /*
       * Video write.  Hack here to make up for the missing bit 6
       * video ram, emulating the gate in Z30.
       */
      if (value & 0xA0)
        value &= 0xBF;
      else
        value |= 0x40;
    }
    trs80_screen_write_char(address - VIDEO_START, value);
  } else if (address == TRSDISK_DATA) {
    trs_disk_data_write(value);
  } else if (address == TRSDISK_COMMAND) {
    trs_disk_command_write(value);
  } else if (address == TRSDISK_TRACK) {
    trs_disk_track_write(value);
  } else if (address == TRSDISK_SECTOR) {
    trs_disk_sector_write(value);
  } else if (TRSDISK_SELECT(address)) {
    trs_disk_select_write(value);
  } else if (address >= 0x3900 && selector) {
    trs80_model1_write_mem(address, value);
  } else if (PRINTER_1(address)) {
    trs_printer_write(value);
#if MEMDEBUG
  } else {
    error("Invalid write of %02X to address %04X [PC=%04X, mem_map=%02X]",
        value, address, Z80_PC, memory_map);
#endif
  }
}

void trs80_model3_mem_write(int address, int value)
{
  if (address >= RAM_START) {
    memory[address] = value;
  } else if (address >= VIDEO_START) {
    if (grafyx_m3_write_byte(address - VIDEO_START, value))
      return;
    else
      trs80_screen_write_char(address - VIDEO_START, value);
#if MEMDEBUG
  } else {
    error("Invalid write of %02X to address %04X [PC=%04X, mem_map=%02X]",
        value, address, Z80_PC, memory_map);
#endif
  }
}

void mem_write(int address, int value)
{
  address &= 0xFFFF;

  /* Anitek MegaMem */
  if (megamem_addr) {
    if (address >= megamem_addr && address <= megamem_addr + 0x3FFF) {
      memory[megamem_base + (address & 0x3FFF)] = value;
      return;
    }
  }

  /* The SuperMem sits between the system and the Z80 */
  if (supermem && supermem_base) {
    if (!((address ^ supermem_high) & 0x8000)) {
      memory[supermem_base + (address & 0x7FFF)] = value;
      return;
    }
    /* Otherwise the request comes from the system */
  }

  switch (memory_map) {
    case 0x10: /* Model I */
      if (address >= RAM_START)
        trs80_model1_write_mem(address, value);
      else
        trs80_model1_write_mmio(address, value);
      break;

    case 0x11: /* Model I: selector mode 1 (all RAM except I/O high) */
      if (address >= 0xF7E0 && address <= 0xF7FF)
        trs80_model1_write_mmio(address & 0x3FFF, value);
      else
        trs80_model1_write_mem(address, value);
      break;

    case 0x12: /* Model I: selector mode 2 (ROM disabled) */
      if (address >= 0x37E0 && address < RAM_START)
        trs80_model1_write_mmio(address, value);
      else
        trs80_model1_write_mem(address, value);
      break;

    case 0x13: /* Model I: selector mode 3 (CP/M mode) */
      if (address >= 0xF7E0)
        trs80_model1_write_mmio(address & 0x3FFF, value);
      else
        /* Fall through */

    case 0x14: /* Model I: All RAM banking high */
    case 0x15: /* Model I: All RAM banking low */
      trs80_model1_write_mem(address, value);
      break;

    case 0x16: /* Model I: Low 16K in top 16K */
      if (address < RAM_START)
        trs80_model1_write_mmio(address, value);
      else
        trs80_model1_write_mem(address, value);
      break;

    case 0x17: /* Model I: Described in the selector doc as 'not useful' */
      break;   /* Not clear what really happens */

    case 0x20: /* Schmidtke 80-Z Video Card */
      if (sys_byte & (1 << 0)) {
        if (address >= video_addr && address <= video_addr + 0xFFF) {
          trs80_screen_write_char(((address - video_addr) & 0x7FF), value);
          return;
        }
      }
      if ((sys_byte & (1 << 3)) || address >= RAM_START)
        memory[address] = value;
      else
        trs80_model1_write_mmio(address, value);
      break;

    case 0x21: /* EG-64 Memory-Banking-Adaptor */
      if (address < RAM_START) {
        if (((sys_byte & (1 << 1)) && address <= 0x2FFF) ||
            ((sys_byte & (1 << 3)) && address >= 0x3000 && address <= 0x35FF) ||
            ((sys_byte & (1 << 4)) && address >= 0x3600 && address <= 0x37FF) ||
            ((sys_byte & (1 << 5)) && address >= 0x3800 && address <= 0x3BFF) ||
            ((sys_byte & (1 << 6)) && address >= 0x3C00))
          memory[address] = value;
        else
          trs80_model1_write_mmio(address, value);
        return;
      }
      memory[address] = value;
      break;

    case 0x22: /* Lubomir Soft Banker */
      if (address < RAM_START) {
        if (((sys_byte & (1 << 7)) && address <= 0x37DF) ||
            ((sys_byte & (1 << 5)) && address >= 0x37E0))
          memory[address] = value;
        else
          trs80_model1_write_mmio(address, value);
        return;
      }
      if ((sys_byte & (1 << 4)) && address >= 0x8000)
        /* Write to "Expander RAM" */
        memory[address + 0x8000] = value;
      else
        memory[address] = value;
      break;

    case 0x23: /* EG 3200: bit set to 0 => bank enabled */
      /* Bit 1 - Bank 2: Video Memory 0 (1k, 64x16, TRS-80 M1 compatible) */
      if ((sys_byte & (1 << 1)) == 0) {
        if (address >= VIDEO_START && address <= 0x3FFF) {
          trs80_screen_write_char(address - VIDEO_START, value);
          return;
        }
      }
      /* Bit 2 - Bank 3: Video Memory 1 (additional 1k for 80x24 video mode)
       *                 Video Memory 2 (EG 3210: Programmable Graphics Adaptor) */
      if ((sys_byte & (1 << 2)) == 0) {
        if (address >= 0x4000 && address <= 0x43FF) {
          trs80_screen_write_char(address - VIDEO_START, value);
          return;
        }
        if (address >= 0x4400 && address <= 0x47FF) {
          address = address - 0x4400;
          genie3s_char((address / 16) + 192, address & 0x0F, value);
          return;
        }
      }
      /* Bit 3 - Bank 4: Disk I/O */
      if ((sys_byte & (1 << 3)) == 0) {
        if (address >= 0x37E0 && address <= 0x37EF) {
          trs80_model1_write_mmio(address, value);
          return;
        }
      }
      /* Bank 0: RAM */
      if (address <= 0x7FFF) /* Low 32 KB for Genieplus Banking */
        memory[address + bank_base] = value;
      else
        memory[address] = value;
      break;

    case 0x24: /* TCS Genie IIIs */
      if ((sys_byte & (1 << 0)) == 0) {
        if (sys_byte & (1 << 4)) {
          /* 2K Video RAM */
          if (address >= KEYBOARD_START && address <= 0x3FFF) {
            trs80_screen_write_char((address + video_addr) & 0x7FF, value);
            return;
          }
        } else {
          if (address >= VIDEO_START && address <= 0x3FFF) {
            trs80_screen_write_char(address + video_addr, value);
            return;
          }
        }
        /* Disk I/O */
        if (address >= 0x37E0 && address <= 0x37EF) {
          trs80_model1_write_mmio(address, value);
          return;
        }
      }
      /* HRG */
      if ((sys_byte & (1 << 3)) && address >= 0x8000) {
        genie3s_hrg_write(address - 0x8000, value);
        return;
      }
      /* Write protect "Pseudo-ROM" */
      if ((sys_byte & (1 << 5)) && address <= 0x2FFF)
        return;
      /* Write to Font-SRAM */
      if (sys_byte_2 & (1 << 1) && address >= 0x8000) {
        genie3s_char(video[(VIDEO_START + video_addr)],
                     (address - 0x8000) >> 11, value);
        return;
      }
      /* "Constant bit" points to Bank 0 */
      if ((address <= 0x3FFF && (sys_byte_2 & (1 << 0)) == 0) ||
          (address >= 0xE000 && (sys_byte_2 & (1 << 0))))
        memory[address + bank_offset[1]] = value;
      else
        memory[address + bank_offset[0] + bank_base] = value;
      break;

    case 0x25: /* Aster CT-80 */
      if ((sys_byte & (1 << 5)) == 0) { /* device bank */
        if ((sys_byte & (1 << 3)) == 0) {
          /* TRS-80 mode */
          if (address >= 0x37E0 && address <= 0x3FFF) {
            trs80_model1_write_mmio(address, value);
            return;
          }
        } else {
          /* CP/M mode */
          /* 2K Video RAM */
          if (address >= 0xF800) {
            trs80_screen_write_char(address - 0xF800, value);
            return;
          }
          /* Disk MMIO */
          if (address >= 0xEFE0 && address <= 0xEFEF) {
            trs80_model1_write_mmio(address - 0xB800, value);
            return;
          }
        }
      }
      memory[address] = value;
      break;

    case 0x26: /* LNW80/II */
      if ((sys_byte_2 & (1 << 0)) == 0) {
        if (address < RAM_START) {
          if (sys_byte & (1 << 3)) {
            /* HRG in low 16K */
            hrg_write_addr(address, 0x3FFF);
            hrg_write_data(value);
            return;
          }
          if (sys_byte_2 & (1 << 7) && address <= 0x2FFF)
            return;
          if (address >= VIDEO_START) {
            if (sys_byte & (1 << 7))
              /* LNW80 II 80x24 text mode */
              lnw80_screen_write_char(address, value);
            else
              trs80_model1_write_mmio(address, value);
            return;
          }
          if (address >= 0x37E0 && address <= 0x37EF) {
            trs80_model1_write_mmio(address, value);
            return;
          }
          if (PRINTER_1(address)) {
            trs_printer_write(value);
            return;
          }
        }
      } else {
        if (address >= 0xF7E0 && address <= 0xF7EF) {
          trs80_model1_write_mmio(address & 0x3FFF, value);
          return;
        }
      }
      /* LNW80 II 32K Expander-RAM without CP/M MMIO area */
      if (address >= 0x8000 && address <= 0xF6FF)
        memory[address + bank_base] = value;
      else
        memory[address] = value;
      break;

    case 0x27: /* TCS Genie IIs/SpeedMaster */
      /* Expansions bit (RAM 192 B) */
      if ((sys_byte & (1 << 7)) && address <= 0xBFFF) {
        memory[address + bank_base] = value;
        return;
      }
      /* HRG in low 16K */
      if ((sys_byte & (1 << 3)) && address <= 0x3FFF) {
        hrg_write_addr(address, 0x3FFF);
        hrg_write_data(value);
        return;
      }
      /* Write protect "Pseudo-ROM" */
      if ((sys_byte & (1 << 5)) && address <= 0x2FFF)
        return;
      /* MMIO */
      if ((sys_byte & (1 << 0)) == 0) {
        if (address >= 0x3400 && address <= 0x3FFF) {
          trs80_model1_write_mmio(address, value);
          return;
        }
      }
      memory[address] = value;
      break;

    case 0x30: /* Model III */
      trs80_model3_mem_write(address, value);
      break;

    case 0x31: /* CP-500 */
    case 0x32: /* CP-500 64K RAM */
    case 0x33: /* CP-500 80x24 video */
      cp500_mem_write(address, value, memory_map, memory);
      break;

    case 0x40: /* Model 4 map 0 */
    case 0x50: /* Model 4P map 0, boot ROM out */
    case 0x54: /* Model 4P map 0, boot ROM in */
      if (address >= RAM_START) {
        memory[address + bank_offset[address >> 15]] = value;
      } else if (address >= VIDEO_START) {
        if (mem_video_page_write(address, value))
          trs_screen_write_char(address + video_addr, value);
      }
      break;

    case 0x41: /* Model 4 map 1 */
    case 0x51: /* Model 4P map 1, boot ROM out */
    case 0x55: /* Model 4P map 1, boot ROM in */
      if (address >= RAM_START || address < KEYBOARD_START) {
        memory[address + bank_offset[address >> 15]] = value;
      } else if (address >= VIDEO_START) {
        if (mem_video_page_write(address, value))
          trs_screen_write_char(address + video_addr, value);
      }
      break;

    case 0x42: /* Model 4 map 2 */
    case 0x52: /* Model 4P map 2, boot ROM out */
    case 0x56: /* Model 4P map 2, boot ROM in */
      if (address < 0xF400) {
        memory[address + bank_offset[address >> 15]] = value;
      } else if (address >= 0xF800) {
        trs80_screen_write_char(address - 0xF800, value);
      }
      break;

    case 0x43: /* Model 4 map 3 */
    case 0x53: /* Model 4P map 3, boot ROM out */
    case 0x57: /* Model 4P map 3, boot ROM in */
      memory[address + bank_offset[address >> 15]] = value;
      break;
  }
}

/*
 * Words are stored with the low-order byte in the lower address.
 */
int mem_read_word(int address)
{
  int rval = mem_read(address++);

  rval |= mem_read(address & 0xFFFF) << 8;
  return rval;
}

void mem_write_word(int address, int value)
{
  mem_write(address++, value & 0xFF);
  mem_write(address, value >> 8);
}

static Uint8 *trs80_model1_ram_addr(int address)
{
  if (selector) {
    int const addr = address;
    /* Bank low on odd modes */
    int const bank = (selector_reg & 1) == 1 ? 0 : 0x8000;

    /* Selector mode 6 remaps RAM from 0000-3FFF to C000-FFFF while keeping
       the ROMs visible */
    if ((selector_reg & 7) == 6 && address >= 0xC000) {
      /* Use the low 16K, and then bank it. I'm not 100% sure how the
         PAL orders the two */
      address &= 0x3FFF;
    }
    /* Deal with 32K banking from selector */
    if ((addr & 0x8000) == bank)
      address += bank_base;
  }

  return memory + address;
}

static Uint8 *trs80_model1_mmio_addr(int address, int writing)
{
  if (address >= VIDEO_START) return &video[address - VIDEO_START];
  if (address < trs_rom_size && writing == 0) return &rom[address];
  /* With a selector 768 bytes poke through the hole */
  if (address >= 0x3900 && selector) return trs80_model1_ram_addr(address);
  return NULL;
}

Uint8 *trs80_model3_mem_addr(int address, int writing)
{
  if (address >= RAM_START) return &memory[address];
  if (address >= VIDEO_START) return &video[address - VIDEO_START];
  if (address < trs_rom_size && writing == 0) return &rom[address];
  return NULL;
}

/*
 * Get a pointer to the given address.  Note that there is no checking
 * whether the next virtual address is physically contiguous.  The
 * caller is responsible for making sure his strings don't span
 * memory map boundaries.
 */
Uint8 *mem_pointer(int address, int writing)
{
  address &= 0xFFFF;

  /* Anitek MegaMem */
  if (megamem_addr) {
    if (address >= megamem_addr && address <= megamem_addr + 0x3FFF)
      return &memory[megamem_base + (address & 0x3FFF)];
  }

  /* The SuperMem sits between the system and the Z80 */
  if (supermem && supermem_base) {
    if (!((address ^ supermem_high) & 0x8000))
      return &memory[supermem_base + (address & 0x7FFF)];
    /* Otherwise the request comes from the system */
  }

  switch (memory_map) {
    case 0x10: /* Model I */
      if (address < RAM_START)
        return trs80_model1_mmio_addr(address, writing);
      else
        return trs80_model1_ram_addr(address);

    case 0x11: /* Model I: selector mode 1 (all RAM except I/O high) */
      return trs80_model1_ram_addr(address);

    case 0x12: /* Model I: selector mode 2 (ROM disabled) */
      if (address >= 0x37E0 && address < RAM_START)
        return trs80_model1_mmio_addr(address, writing);
      else
        return trs80_model1_ram_addr(address);

    case 0x13: /* Model I: selector mode 3 (CP/M mode) */
      if (address >= 0xF7E0)
        return trs80_model1_mmio_addr(address & 0x3FFF, writing);
      /* Fall through */

    case 0x14: /* Model I: All RAM banking high */
    case 0x15: /* Model I: All RAM banking low */
      return trs80_model1_ram_addr(address);

    case 0x16: /* Model I: Low 16K in top 16K */
      if (address < RAM_START)
        return trs80_model1_mmio_addr(address, writing);
      else
        return trs80_model1_ram_addr(address);

    case 0x17: /* Model I: Described in the selector doc as 'not useful' */
      break;   /* Not clear what really happens */

    case 0x20: /* Schmidtke 80-Z Video Card */
      if (sys_byte & (1 << 0)) {
        if (address >= video_addr && address <= video_addr + 0xFFF)
          return &video[((address - video_addr) & 0x7FF)];
      }
      if ((sys_byte & (1 << 3)) || address >= RAM_START)
        return &memory[address];
      else
        return trs80_model1_mmio_addr(address, writing);

    case 0x21: /* EG-64 Memory-Banking-Adaptor */
      if (address < RAM_START) {
        if (((sys_byte & (1 << 0)) && address <= 0x2FFF) ||
            ((sys_byte & (1 << 2)) && address >= 0x3000 && address <= 0x35FF) ||
            ((sys_byte & (1 << 4)) && address >= 0x3600 && address <= 0x37FF) ||
            ((sys_byte & (1 << 5)) && address >= 0x3800 && address <= 0x3BFF) ||
            ((sys_byte & (1 << 6)) && address >= 0x3C00))
          return &memory[address];
        else
          return trs80_model1_mmio_addr(address, writing);
      }
      return &memory[address];

    case 0x22: /* Lubomir Soft Banker */
      if (address < RAM_START) {
        if (((sys_byte & (1 << 6)) && address <= 0x37DF) ||
            ((sys_byte & (1 << 5)) && address >= 0x37E0))
          return &memory[address];
        else
          return trs80_model1_mmio_addr(address, writing);
      }
      if ((sys_byte & (1 << 4)) && address >= 0x8000)
        /* Read from "Expander RAM" */
        return &memory[address + 0x8000];
      else
        return &memory[address];

    case 0x23: /* EG 3200: bit set to 0 => bank enabled */
      /* Bit 0 - Bank 1: ROM/EPROM */
      if ((sys_byte & (1 << 0)) == 0 && address < trs_rom_size)
        return writing ? NULL : &rom[address];
      /* Bit 1 - Bank 2: Video Memory 0 (1k, 64x16, TRS-80 M1 compatible) */
      if ((sys_byte & (1 << 1)) == 0) {
        if (address >= VIDEO_START && address <= 0x3FFF)
          return &video[address - VIDEO_START];
      }
      /* Bit 2 - Bank 3: Video Memory 1 (additional 1k for 80x24 video mode) */
      if ((sys_byte & (1 << 2)) == 0) {
        if (address >= 0x4000 && address <= 0x43FF)
          return &video[address - VIDEO_START];
      }
      /* Bank 0: RAM */
      if (address <= 0x7FFF) /* Low 32 KB for Genieplus Banking */
        return &memory[address + bank_base];
      else
        return &memory[address];

    case 0x24: /* TCS Genie IIIs */
      if ((sys_byte & (1 << 0)) == 0) {
        if (sys_byte & (1 << 4)) {
          /* 2K Video RAM */
          if (address >= KEYBOARD_START && address <= 0x3FFF)
            return &video[(address + video_addr) & 0x7FF];
        } else {
          if (address >= VIDEO_START && address <= 0x3FFF)
            return &video[address + video_addr];
        }
      }
      /* ROM */
      if ((sys_byte & (1 << 2)) == 0 && address <= 0x2FFF)
        return writing ? NULL : &rom[address];
      /* "Constant bit" points to Bank 0 */
      if ((address <= 0x3FFF && (sys_byte_2 & (1 << 0)) == 0) ||
          (address >= 0xE000 && (sys_byte_2 & (1 << 0))))
        return &memory[address + bank_offset[1]];
      else
        return &memory[address + bank_offset[0] + bank_base];

    case 0x25: /* Aster CT-80 */
      if ((sys_byte & (1 << 5)) == 0) { /* device bank */
        /* Boot-ROM */
        if ((sys_byte & (1 << 1)) && address <= 0x7FF)
          return writing ? NULL : &rom[address | 0x3000];
        if ((sys_byte & (1 << 2)) == 0 && address <= 0x2FFF)
          return writing ? NULL : &rom[address];
        if ((sys_byte & (1 << 3)) == 0) {
          /* TRS-80 mode */
          if (address >= 0x3000 && address <= 0x3FFF)
            return trs80_model1_mmio_addr(address, writing);
        } else {
          /* CP/M mode */
          /* 2K Video RAM */
          if (address >= 0xF800)
            return &video[address - 0xF800];
          /* Boot-ROM */
          if (address >= 0xEC00 && address <= 0xF3FF)
            return writing ? NULL : &rom[address - 0xBC00];
        }
      }
      return &memory[address];

    case 0x26: /* LNW80/II */
      if ((sys_byte_2 & (1 << 0)) == 0) {
        if (address < RAM_START) {
          if (sys_byte & (1 << 3))
            /* HRG in low 16K */
            return NULL;
          if (sys_byte_2 & (1 << 1) && address <= 0x3700)
            return &memory[address];
          if (address >= VIDEO_START)
            return mem_video_page_addr(address);
          if (address >= 0x37E0 && address <= 0x37EF)
            return trs80_model1_mmio_addr(address, writing);
          if (address <= 0x2FFF && writing == 0)
            return &rom[address];
        }
      } else {
        if (address >= 0xF7E0 && address <= 0xF7EF)
          return trs80_model1_mmio_addr(address & 0x3FFF, writing);
      }
      /* LNW80 II 32K Expander-RAM without CP/M MMIO area */
      if (address >= 0x8000 && address <= 0xF6FF)
        return &memory[address + bank_base];
      else
        return &memory[address];

    case 0x27: /* TCS Genie IIs/SpeedMaster */
      /* Expansions bit (RAM 192 B) */
      if ((sys_byte & (1 << 7)) && address <= 0xBFFF)
        return &memory[address + bank_base];
      /* ROM and MMIO */
      if ((sys_byte & (1 << 0)) == 0) {
        if ((sys_byte & (1 << 2)) == 0 && address <= 0x2FFF)
          return writing ? NULL : &rom[address];
        if (address >= 0x3400 && address <= 0x3FFF)
          return trs80_model1_mmio_addr(address, writing);
      }
      return &memory[address];

    case 0x30: /* Model III reading */
      return trs80_model3_mem_addr(address, writing);

    case 0x31: /* CP-500 */
    case 0x32: /* CP-500 64K RAM */
    case 0x33: /* CP-500 80x24 video */
      return cp500_mem_addr(address, memory_map, rom, memory, writing);

    case 0x40: /* Model 4 map 0 */
      if (address >= RAM_START) {
        return &memory[address + bank_offset[address >> 15]];
      }
      if (address >= VIDEO_START) return mem_video_page_addr(address);
      if (address < trs_rom_size && writing == 0) return &rom[address];
      break;

    case 0x54: /* Model 4P map 0, boot ROM in */
    case 0x55: /* Model 4P map 1, boot ROM in */
      if (address < trs_rom_size && writing == 0) return &rom[address];
      /* else fall thru */
    case 0x41: /* Model 4 map 1 */
    case 0x50: /* Model 4P map 0, boot ROM out */
    case 0x51: /* Model 4P map 1, boot ROM out */
      if (address >= RAM_START || address < KEYBOARD_START) {
        return &memory[address + bank_offset[address >> 15]];
      }
      if (address >= VIDEO_START) return mem_video_page_addr(address);
      break;

    case 0x42: /* Model 4 map 2 */
    case 0x52: /* Model 4P map 2, boot ROM out */
    case 0x56: /* Model 4P map 2, boot ROM in */
      if (address < 0xF400) {
        return &memory[address + bank_offset[address >> 15]];
      }
      if (address >= 0xF800) return &video[address - 0xF800];
      break;

    case 0x43: /* Model 4 map 3 */
    case 0x53: /* Model 4P map 3, boot ROM out */
    case 0x57: /* Model 4P map 3, boot ROM in */
      return &memory[address + bank_offset[address >> 15]];
  }

  return NULL;
}

void trs_mem_save(FILE *file)
{
  trs_save_uint8(file, memory, MAX_MEMORY_SIZE + 1);
  trs_save_uint8(file, rom, MAX_ROM_SIZE + 1);
  trs_save_uint8(file, video, MAX_VIDEO_SIZE + 1);
  trs_save_int(file, &trs_model, 1);
  trs_save_int(file, &trs_rom_size, 1);
  trs_save_int(file, &lowercase, 1);
  trs_save_int(file, &memory_map, 1);
  trs_save_int(file, bank_offset, 2);
  trs_save_int(file, &video_addr, 1);
  trs_save_int(file, &romin, 1);
  trs_save_uint32(file, &bank_base, 1);
  trs_save_uint8(file, &mem_command, 1);
  trs_save_int(file, &huffman, 1);
  trs_save_int(file, &hypermem, 1);
  trs_save_int(file, &supermem, 1);
  trs_save_int(file, &supermem_base, 1);
  trs_save_uint32(file, &supermem_high, 1);
  trs_save_int(file, &selector, 1);
  trs_save_int(file, &selector_reg, 1);
  trs_save_int(file, &lubomir, 1);
  trs_save_int(file, &megamem, 1);
  trs_save_int(file, &megamem_addr, 1);
  trs_save_uint32(file, &megamem_base, 1);
  trs_save_int(file, &sys_byte, 1);
  trs_save_int(file, &sys_byte_2, 1);
}

void trs_mem_load(FILE *file)
{
  trs_load_uint8(file, memory, MAX_MEMORY_SIZE + 1);
  trs_load_uint8(file, rom, MAX_ROM_SIZE + 1);
  trs_load_uint8(file, video, MAX_VIDEO_SIZE + 1);
  trs_load_int(file, &trs_model, 1);
  trs_load_int(file, &trs_rom_size, 1);
  trs_load_int(file, &lowercase, 1);
  trs_load_int(file, &memory_map, 1);
  trs_load_int(file, bank_offset, 2);
  trs_load_int(file, &video_addr, 1);
  trs_load_int(file, &romin, 1);
  trs_load_uint32(file, &bank_base, 1);
  trs_load_uint8(file, &mem_command, 1);
  trs_load_int(file, &huffman, 1);
  trs_load_int(file, &hypermem, 1);
  trs_load_int(file, &supermem, 1);
  trs_load_int(file, &supermem_base, 1);
  trs_load_uint32(file, &supermem_high, 1);
  trs_load_int(file, &selector, 1);
  trs_load_int(file, &selector_reg, 1);
  trs_load_int(file, &lubomir, 1);
  trs_load_int(file, &megamem, 1);
  trs_load_int(file, &megamem_addr, 1);
  trs_load_uint32(file, &megamem_base, 1);
  trs_load_int(file, &sys_byte, 1);
  trs_load_int(file, &sys_byte_2, 1);
}

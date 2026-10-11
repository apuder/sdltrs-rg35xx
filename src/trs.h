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
 * Portions copyright (c) 1996-2020, Timothy P. Mann
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
 * trs.h
 */
#ifndef _TRS_H
#define _TRS_H

#include <time.h>
#include "z80.h"

/* currentmode values */
#ifdef _WIN32
#undef  ALTERNATE
#endif
#define NORMAL    0
#define EXPANDED  1
#define INVERSE   2
#define ALTERNATE 4
#define REVERSE   8

#define TOUPPER(a)   ((a >= 'a' && a <= 'z') ? a - ' ' : a)
#define TOLOWER(a)   ((a >= 'A' && a <= 'Z') ? a + ' ' : a)
#define TOPRINT(a)   ((a >= ' ' && a <= '~') ? a : '.')

#if defined(__OS2__) || defined(_WIN32)
#define DIR_SLASH '\\'
#else
#define DIR_SLASH '/'
#endif

#ifdef ZBX
extern int  trs_disk_debug_flags;
extern int  trs_io_debug_flags;

extern void trs_debug(void);
extern void trs_disk_debug(void);
extern void trs_hard_debug(void);
extern void trs_timer(int on_off);
#endif

/* trs_cassette.c: */
extern int  cassette_default_sample_rate;
extern int  trs_sound;

extern void assert_state_void(int dummy);
extern int  get_fourbyte(Uint32 *n, FILE* f);
extern int  get_twobyte(Uint16 *n, FILE* f);
extern int  put_fourbyte(Uint32 n, FILE* f);
extern int  put_twobyte(Uint16 n, FILE* f);
extern void transition_out(int dummy);
extern int  trs_cassette_in(void);
extern void trs_cassette_kickoff(int dummy);
extern void trs_cassette_motor(int value);
extern void trs_cassette_out(int value);
extern void trs_cassette_reset(int poweron);
extern void trs_cassette_update(int dummy);
extern void trs_orch90_flush(int dummy);
extern void trs_orch90_out(int chan, int value);
extern void trs_sound_out(int value);

/* trs_cmd_rom.c: */
extern int  trs_hd_boot;

extern int  trs_load_cmd(const char *filename);
extern void trs_rom_init(void);

/* trs_disk.c: */
extern Uint8 genie3s_latch_read(Uint8 byte);
extern void trs_disk_done(int dummy);
extern void trs_disk_firstdrq(int dummy);
extern void trs_disk_lostdata(int dummy);
extern int  trs_disk_motoroff(void);

/* trs_keyboard.c: */
extern int  stretch_amount;
extern int  trs_joystick;
extern int  trs_kb_bracket_state;
extern int  trs_keypad_joystick;

extern void clear_key_queue(void);
extern int  trs_joystick_in(void);
extern void trs_kb_bracket(int shifted);
extern void trs_kb_heartbeat(void);
extern int  trs_kb_mem_read(int address);
extern void trs_kb_reset(void);
extern void trs_xlate_keysym(int keysym);

/* trs_imp_exp.c: */
extern int  trs_emtsafe;

/* trs_interrupt.c: */
typedef void (*trs_event_func)(int arg);

extern float clock_mhz_1;
extern float clock_mhz_3;
extern float clock_mhz_4;
extern int   speedup;
extern int   timer_hz;
extern time_t trs_timeoffset;
extern int   trs_year;
extern int   turbo_mode;
extern int   turbo_rate;

extern void  trs_cancel_event(void);
extern void  trs_cassette_clear_interrupts(void);
extern void  trs_cassette_fall_interrupt(int dummy);
extern int   trs_cassette_interrupts_enabled(void);
extern void  trs_cassette_rise_interrupt(int dummy);
extern void  trs_disk_drq_interrupt(void);
extern void  trs_disk_intrq_interrupt(int state);
extern void  trs_disk_motoroff_interrupt(int state);
extern void  trs_do_event(void);
trs_event_func trs_event_scheduled(void);
extern void  trs_get_event(int wait);
extern void  trs_interrupt_latch_clear(void);
extern Uint8 trs_interrupt_latch_read(void);
extern void  trs_interrupt_mask_write(Uint8);
extern Uint8 trs_nmi_latch_read(void);
extern void  trs_nmi_mask_write(Uint8);
extern void  trs_reset_button_interrupt(int state);
extern void  trs_schedule_event(trs_event_func f, int arg, int tstates);
extern void  trs_schedule_event_us(trs_event_func f, int arg, int us);
extern void  trs_timer_init(void);
extern void  trs_timer_interrupt(int state);
extern void  trs_timer_mode(int mode);
extern void  trs_timer_speed(int flag);
extern void  trs_timer_sync_with_host(void);
/* extern void trs_uart_err_interrupt(int state); */
extern void  trs_uart_rcv_interrupt(int state);
extern void  trs_uart_snd_interrupt(int state);

/* trs_memory.c: */
extern int  trs_model; /* 1, 3, 4, 5(=4P) */
extern int  trs_mem_size;
extern int  trs_rom_size;

extern void trs_reset(int poweron);

/* trs_options.c: */
extern int  aspect_ratio;
extern int  background;
extern int  border_width;
extern int  foreground;
extern int  fullscreen;
extern int  grafyx_microlabs;
extern int  gui_background;
extern int  gui_foreground;
extern int  lowe_le18;
extern int  lowercase;
extern int  resize3;
extern int  resize4;
extern char romfile1[];
extern char romfile3[];
extern char romfile4p[];
extern int  scale;
extern char scale_quality;
extern int  text80x24;
extern char trs_cass_dir[];
extern int  trs_charset1;
extern int  trs_charset3;
extern int  trs_charset4;
extern char trs_cmd_file[];
extern char trs_config_file[];
extern char trs_disk_dir[];
extern char trs_disk_set_dir[];
extern int  trs_emu_mouse;
extern char trs_hard_dir[];
extern int  trs_paused;
extern char trs_printer_dir[];
extern int  trs_show_led;
extern char trs_state_dir[];
extern char trs_state_file[];

extern int  trs_load_config_file(void);
extern int  trs_parse_command_line(int argc, char **argv);
extern int  trs_write_config_file(const char *filename);

/* trs_printer.c: */
extern int  trs_printer;

extern int  trs_printer_read(void);
extern int  trs_printer_reset(void);
extern void trs_printer_write(int value);

/* trs_sdl_interface.c: */
extern void  font_char(int character, int scanline, int byte, int mirror);
extern void  genie3s_hrg(int value);
extern Uint8 genie3s_hrg_read(unsigned int position);
extern void  genie3s_hrg_write(unsigned int position, int byte);
extern int   grafyx_read_data(void);
extern int   grafyx_read_mode(void);
extern Uint8 grafyx_m3_read_byte(unsigned int position);
extern void  grafyx_m3_reset(void);
extern int   grafyx_m3_write_byte(unsigned int position, int value);
extern void  grafyx_m3_write_mode(int value);
extern void  grafyx_write_data(int value);
extern void  grafyx_write_mode(int value);
extern void  grafyx_write_overlay(int value);
extern void  grafyx_write_x(int value);
extern void  grafyx_write_xoffset(int value);
extern void  grafyx_write_y(int value);
extern void  grafyx_write_yoffset(int value);
extern void  hrg_onoff(int enable);
extern int   hrg_read_data(int address);
extern void  hrg_write_data(int address, int data);
extern int   lowe_le18_read(void);
extern void  lowe_le18_write_control(int);
extern void  lowe_le18_write_data(int);
extern void  m6845_crtc_reset(void);
extern void  m6845_cursor(int unsigned position, int start, int end, int visible);
extern void  m6845_screen(int chars, int lines, int raster, int factor);
extern void  m6845_text(int onoff);
extern void  trs_disk_led(int drive, int on_off);
extern void  trs_exit(int confirm);
extern void  trs_get_mouse_max(int *x, int *y, unsigned int *sens);
extern void  trs_get_mouse_pos(int *x, int *y, unsigned int *buttons);
extern void  trs_get_window(int *x, int *y, int *w, int *h);
extern void  trs_hard_led(int drive, int on_off);
extern void  trs_screen_80x24(int flag);
extern void  trs_screen_caption(void);
extern void  trs_screen_init(int resize);
extern void  trs_screen_mode(int mode, int flag);
extern void  trs_screen_refresh(void);
extern void  trs_screen_reset(void);
extern void  trs_screen_update(void);
extern void  trs_screen_write_char(unsigned int position, Uint8 character);
extern void  trs_sdl_init(void);
extern void  trs_set_mouse_max(int x, int y, unsigned int sens);
extern void  trs_set_mouse_pos(int x, int y);
extern void  trs_set_window(int x, int y, int w, int h);
extern void  trs_turbo_led(void);

/* z80.c: */
extern int trs_continuous; /* 1= run continuously,
			      0= enter debugger after instruction,
			     -1= suppress interrupt and enter debugger */

#endif /*_TRS_H*/

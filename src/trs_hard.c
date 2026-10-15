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
 *
 * Portions of the OMTI/SASI hard disk controller emulation:
 * Copyright (c) 2026 Egbert H. Schroeer (BSD-2 license, see LICENSE)
 */

/*
 * Emulation of the Radio Shack TRS-80 Model I/III/4/4P
 * hard disk controller.  This is a Western Digital WD1000/WD1010
 * mapped at ports 0xc8-0xcf, plus control registers at 0xc0-0xc1.
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "error.h"
#include "reed.h"
#include "trs.h"
#include "trs_hard.h"
#include "trs_imp_exp.h"
#include "trs_omti_sasi.h"
#include "trs_state_save.h"

#define HARDDEBUG1 (1 << 2)  /* show detail on all port i/o */
#define HARDDEBUG2 (1 << 3)  /* show all commands */
#define HARDDEBUG3 (1 << 4)  /* show sector dump */
#define SECTORSIZE (1024)    /* maximum sector size */

/* Private types and data */

/* Structure describing one drive */
typedef struct {
  FILE* file;
  char filename[FILENAME_MAX];
  /* Values decoded from rhh */
  int writeprot;
  int cyls;  /* cyls per drive */
  int heads; /* tracks per cyl */
  int secs;  /* secs per track */
} Drive;

/* Structure describing controller state */
typedef struct {
  /* Drive geometries and files */
  Drive d[TRS_HARD_MAXDRIVES];

  /* Controller present?  Yes if we have any drives, no if none */
  int present;

  /* Number of bytes already done in current read/write */
  int bytesdone;

  /* Controller register images */
  Uint8  cdb[CDB_LEN];
  Uint8  command;
  Uint8  control; /* OMTI/SASI CDB index */
  Uint32 cyl;     /* SASI LBA */
  Uint8  drive;
  Uint8  error;
  Uint8  head;
  Uint8  sdh;     /* SASI status switch */
  Uint8  secbuf[SECTORSIZE];
  Uint8  seccnt;
  Uint8  secnum;
  Uint16 secsize;
  Uint32 size;
  Uint8  status;
} State;

static State state;

/* Forward */
static int  hard_data_in(void);
static void hard_data_out(int value);
static void hard_error(int error);
static int  hard_fail(void);
static void hard_format(void);
static void hard_init(int cmd);
static int  hard_open(int drive);
static void hard_read(int cmd);
static void hard_restore(void);
static int  hard_sector(int newstatus);
static void hard_seek(void);
static void hard_write(int cmd);

static void omti_command(void);
static int  omti_data_in(void);
static void omti_data_out(Uint8 value);
static int  omti_format(int cmd);
static void omti_reset(void);
static int  omti_status(int error);

static void sasi_command(void);
static int  sasi_data_in(void);
static void sasi_data_out(Uint8 value);
static int  sasi_format(int cmd);
static void sasi_reset(void);
static int  sasi_seek(void);
static int  sasi_status(int error);

#ifdef ZBX
static const char *hard_cmd(int port)
{
  switch (port) {
    case TRS_HARD_WP:
      return "c0:WP";
    case TRS_HARD_CONTROL:
      return "c1:CONTROL";
    case TRS_HARD_DATA:
      return "c8:DATA";
    case TRS_HARD_PRECOMP:
      return "c9:PRECOMP/ERROR";
    case TRS_HARD_SECCNT:
      return "ca:SECCNT";
    case TRS_HARD_SECNUM:
      return "cb:SECNUM";
    case TRS_HARD_CYLLO:
      return "cc:CYLLO";
    case TRS_HARD_CYLHI:
      return "cd:CYLHI";
    case TRS_HARD_SDH:
      return "ce:SDH";
    case TRS_HARD_COMMAND:
      return "cf:COMMAND/STATUS";
    default:
      return "";
  }
}

static void hard_debug(const char *cmd)
{
  debug("hard_%s: drive:%d cyl:%d head:%d secnum:%d seccnt:%d\n",
    cmd, state.drive, state.cyl, state.head, state.secnum, state.seccnt);
}

void trs_hard_debug(void)
{
  int i;

  if (state.present == 0) {
    puts("DISABLED");
    return;
  }

  printf("drive:%d cyl|lba:%d/%d head:%d secnum:%d seccnt:%d secsize:%d\n",
      state.drive, state.cyl, state.size, state.head,
      state.secnum, state.seccnt, state.secsize);
  printf("command:0x%02X control:0x%02X error:0x%02X status:0x%02X\n",
      state.command, state.control, state.error, state.status);

  for (i = 0; i < TRS_HARD_MAXDRIVES; i++) {
    if (state.d[i].file) {
      printf("\nhard%d: '%s'\n", i, state.d[i].filename);
      printf("\tcyls:%d heads:%d secs:%d%s\n",
          state.d[i].cyls, state.d[i].heads, state.d[i].secs,
          state.d[i].writeprot ? "\tWrite-Protect" : "");
    }
  }
}

static void sector_dump(const char *cmd, int sector_size)
{
  int byte[16];
  int i;

  for (i = 0; i < sector_size; i += 16) {
    int j;

    debug("hard_%s: %04x: ", cmd, i);
    for (j = 0; j < 16; j++)
      fprintf(stderr, "%02x ", (byte[j] = state.secbuf[i + j]));

    fprintf(stderr, "   ");
    for (j = 0; j < 16; j++)
      putc(TOPRINT(byte[j]), stderr);

    putc('\n', stderr);
  }
}
#endif

static int sector_read(size_t sector_size)
{
  FILE *f = state.d[state.drive].file;

  if (f && fread(state.secbuf, 1, sector_size, f) != sector_size) {
    if (ferror(f)) {
      file_error("reading hard%d", state.drive);
      return ERR_DATA_ERROR | 0x80;
    }
  }
#if ZBX
  if (trs_io_debug_flags & HARDDEBUG3)
    sector_dump("read", sector_size);
#endif

  return 0;
}

static int sector_write(size_t sector_size)
{
  FILE *f = state.d[state.drive].file;

  if (f && fwrite(state.secbuf, 1, sector_size, f) != sector_size) {
    if (errno) {
      if (state.d[state.drive].writeprot)
        error("hard%d: write-protected", state.drive);
      else
        file_error("writing hard%d", state.drive);
      return ERR_WRITE_FAULT | 0x80;
    }
  }
#if ZBX
  if (trs_io_debug_flags & HARDDEBUG3)
    sector_dump("write", sector_size);
#endif

  return 0;
}

static int sector_seek(size_t sector_size)
{
  const Drive *d = &state.d[state.drive];

  if (d->file == NULL)
    return hard_fail();

  /* Check sector number overflow and adjust head */
  if (state.secnum >= d->secs && d->secs) {
    if (state.secnum > d->secs)
      state.head += state.secnum / d->secs;

    state.secnum %= d->secs;
  }

  if (/**state.cyl >= d->cyls ||**/ /* ignore this limit */
      state.head >= d->heads) {
    error("hard%d: requested cyl:%d head:%d sec:%d (cyls:%d heads:%d secs:%d)",
        state.drive, state.cyl, state.head, state.secnum, d->cyls, d->heads, d->secs);
    return ERR_ILLEGAL_ADDRESS | 0x80;
  }

  if (fseek(d->file,
      sizeof(ReedHardHeader) +
      sector_size * (state.cyl * d->heads * d->secs
                  +  state.head * d->secs + state.secnum), 0) != 0) {
    file_error("hard%d: fseek '%s'", state.drive, d->filename);
    return ERR_SECTOR_NOT_FOUND | 0x80;
  }

  if (trs_show_led)
    trs_hard_led(state.drive, 1);

  return 0;
}

/* Powerup or reset button */
void trs_hard_init(int poweron)
{
  state.command = 0;
  state.control = 0;
  state.cyl = 0;
  state.drive = 0;
  state.error = 0;
  state.head = 0;
  state.sdh = 0;
  state.secnum = 0;
  state.secsize = 0;

  if (poweron) {
    int i;

    state.present = 0;
    state.seccnt = 0;
    state.size = 0;
    state.status = 0;
    memset(state.cdb, 0, CDB_LEN);
    memset(state.secbuf, 0, SECTORSIZE);

    for (i = 0; i < TRS_HARD_MAXDRIVES; i++) {
      state.d[i].file = NULL;
      state.d[i].writeprot = 0;
      state.d[i].cyls = 0;
      state.d[i].heads = 0;
      state.d[i].secs = 0;

      if (hard_open(i) == 0) state.present = 1;
    }
  }
}

void trs_hard_attach(int drive, const char *diskname)
{
  snprintf(state.d[drive].filename, FILENAME_MAX, "%s", diskname);

  if (hard_open(drive) == 0)
    trs_impexp_xtrshard_attach(drive);
  else
    trs_hard_remove(drive);

}

void trs_hard_remove(int drive)
{
  if (state.d[drive].file != NULL)
    fclose(state.d[drive].file);

  trs_impexp_xtrshard_remove(drive);

  state.d[drive].filename[0] = 0;
  state.d[drive].file = NULL;
  state.d[drive].writeprot = 0;
  state.d[drive].cyls = 0;
  state.d[drive].heads = 0;
  state.d[drive].secs = 0;
}

const char*
trs_hard_getfilename(int unit)
{
  return state.d[unit].filename;
}

int
trs_hard_getwriteprotect(int unit)
{
  return state.d[unit].writeprot;
}

void
trs_hard_getgeometry(int unit, int *cyls, int *head, int *secs)
{
  if (state.d[unit].file) {
    *cyls = state.d[unit].cyls;
    *head = state.d[unit].heads;
    *secs = state.d[unit].secs;
  }
}

/* Read from an I/O port mapped to the controller */
int trs_hard_in(int port)
{
  int v = 0xff;

  if (state.present) {
    switch (port) {
    case TRS_HARD_WP: {
      int i;

      v = 0;
      for (i = 0; i < TRS_HARD_MAXDRIVES; i++) {
        if (hard_open(i) == 0) {
          if (state.d[i].writeprot) {
            v |= TRS_HARD_WPBIT(i) | TRS_HARD_WPSOME;
          }
        }
      }
      break; }
    case TRS_HARD_CONTROL:
      v = state.control;
      break;
    case TRS_HARD_DATA:
      v = hard_data_in();
      break;
    case TRS_HARD_ERROR:
      v = state.error;
      break;
    case TRS_HARD_SECCNT:
      v = state.seccnt;
      break;
    case TRS_HARD_SECNUM:
      v = state.secnum;
      break;
    case TRS_HARD_CYLLO:
      v = state.cyl & 0xff;
      break;
    case TRS_HARD_CYLHI:
      v = (state.cyl >> 8) & 0xff;
      break;
    case TRS_HARD_SDH:
      v = state.sdh;
      break;
    case TRS_HARD_STATUS:
      v = state.status;
      break;
    }
  }
#if ZBX
  if (trs_io_debug_flags & HARDDEBUG1)
    debug("[PC=%04X] trs_hard_in(%s) = %02X %c\n",
        Z80_PC, hard_cmd(port), v, TOPRINT(v));
#endif
  return v;
}

/* Write to an I/O port mapped to the controller */
void trs_hard_out(int port, int value)
{
#if ZBX
  if (trs_io_debug_flags & HARDDEBUG1)
    debug("[PC=%04X] trs_hard_out(%s), %02X %c\n",
        Z80_PC, hard_cmd(port), value, TOPRINT(value));
#endif
  switch (port) {
  case TRS_HARD_WP:
    break;
  case TRS_HARD_CONTROL:
    if (value & TRS_HARD_SOFTWARE_RESET)
      trs_hard_init(0);

    if (value & TRS_HARD_DEVICE_ENABLE)
      trs_hard_init(1);

    state.control = value;
    break;
  case TRS_HARD_DATA:
    hard_data_out(value);
    break;
  case TRS_HARD_PRECOMP:
    break;
  case TRS_HARD_SECCNT:
    state.seccnt = value;
    break;
  case TRS_HARD_SECNUM:
    state.secnum = value;
    break;
  case TRS_HARD_CYLLO:
    state.cyl = (state.cyl & 0xff00) | (value & 0x00ff);
    break;
  case TRS_HARD_CYLHI:
    state.cyl = (state.cyl & 0x00ff) | ((value << 8) & 0xff00);
    break;
  case TRS_HARD_SDH:
    state.sdh = value;
    state.secsize = 256 << ((value & TRS_HARD_SIZEMASK) >> TRS_HARD_SIZESHIFT);
    if (state.secsize > SECTORSIZE) state.secsize = 128;
    state.drive = (value & TRS_HARD_DRIVEMASK) >> TRS_HARD_DRIVESHIFT;
    state.head = (value & TRS_HARD_HEADMASK) >> TRS_HARD_HEADSHIFT;
#if 0
    if (hard_open(state.drive) == 0) state.status &= ~TRS_HARD_READY;
#else
    /* Ready, but perhaps not able!  This way seems to work better; it
     * avoids a long delay in the Model 4P boot ROM when there is no
     * unit 0. */
    state.status = TRS_HARD_READY | TRS_HARD_SEEKDONE;
#endif
    break;

  case TRS_HARD_COMMAND:
    state.bytesdone = 0;
    state.command = value;
    switch (value & TRS_HARD_CMDMASK) {
    case TRS_HARD_RESTORE:
      hard_restore();
      break;
    case TRS_HARD_READ:
      hard_read(value);
      break;
    case TRS_HARD_WRITE:
      hard_write(value);
      break;
    case TRS_HARD_FORMAT:
      hard_format();
      break;
    case TRS_HARD_INIT:
      hard_init(value);
      break;
    case TRS_HARD_SEEK:
    case TRS_HARD_VERIFY:
      hard_seek();
      break;
    default:
      error("trs_hard: unknown command 0x%02X", value);
      break;
    }
    break;

  }
}

static int hard_data_in(void)
{
  if ((state.command & TRS_HARD_CMDMASK) == TRS_HARD_READ &&
      (state.status & TRS_HARD_ERR) == 0) {
    if (state.bytesdone < state.secsize) {
      state.status &= ~TRS_HARD_DRQ; /* Drop DRQ */
      return state.secbuf[state.bytesdone++];
    }
  }
  return 0;
}

static void hard_data_out(int value)
{
  if (state.bytesdone < state.secsize) {
    state.secbuf[state.bytesdone] = value;
    state.bytesdone++;
    if ((state.bytesdone == state.secsize) &&
        (state.command & TRS_HARD_CMDMASK) == TRS_HARD_WRITE &&
        (state.status & TRS_HARD_ERR) == 0) {
      state.status &= ~TRS_HARD_DRQ;  /* Drop DRQ */
      if (sector_write(state.secsize) != 0) {
        hard_error(TRS_HARD_DATAERR); /* arbitrary choice */
      }
    }
  }
}

static void hard_error(int error)
{
  state.error  = error;
  state.status = TRS_HARD_READY | TRS_HARD_SEEKDONE | TRS_HARD_ERR;
}

static int hard_fail(void)
{
  error("hard%d access failed", state.drive);
  return ERR_DRIVE_NOT_READY;
}

static void hard_format(void)
{
  int cnt = state.seccnt;

#if ZBX
  if (trs_io_debug_flags & HARDDEBUG2)
    hard_debug("format");
#endif

  memset(state.secbuf, 0, SECTORSIZE);

  while (cnt--) {
    if (sector_write(state.secsize) != 0) {
      hard_error(TRS_HARD_DATAERR); /* arbitrary choice */
      return;
    }
  }
  /* !!should probably set up to read skew table here */
  state.seccnt = 0;
  state.status = TRS_HARD_READY | TRS_HARD_SEEKDONE;
}

static void hard_init(int cmd)
{
#if ZBX
  if (trs_io_debug_flags & HARDDEBUG2)
    hard_debug("init");
#endif
  /* I don't know what this command does */
  error("trs_hard: init command (0x%02X) not implemented", cmd);
  state.status = TRS_HARD_READY | TRS_HARD_SEEKDONE;
}

/*
 * 1) Make sure the file for the current drive is open.  If the file
 * cannot be opened, return -1 and set the controller error status.
 *
 * 2) If newly opening the file, establish the hardware write protect
 * status and geometry in the Drive structure.
 *
 * 3) Set the hardware write protect status and geometry in the Drive
 * structure.
 *
 * 4) Return 0 if OK, -1 if invalid header, errno value otherwise.
 */
static int hard_open(int drive)
{
  Drive *d = &state.d[drive];
  ReedHardHeader rhh;
  size_t res;
  int err = -1;
  int secs;

  if (d->filename[0] == 0)
    goto fail;

  if (d->file != NULL)
    return 0;

  /* First try opening for reading and writing */
  d->file = fopen(d->filename, "rb+");
  if (d->file == NULL) {
    if (errno == EACCES || errno == EROFS) {
    /* No luck, try for reading only */
      d->file = fopen(d->filename, "rb");
    }
    if (d->file == NULL) {
      file_error("open hard%d: '%s'", drive, d->filename);
      err = errno;
      goto fail;
    }
    d->writeprot = 1;
  } else {
    d->writeprot = 0;
  }

  /* Read in the Reed header and check some basic magic numbers (not all) */
  res = fread(&rhh, sizeof(rhh), 1, d->file);
  if (res != 1 || rhh.id1 != 0x56 || rhh.id2 != 0xcb || rhh.ver >= 0x20) {
    error("unrecognized hard%d drive image: '%s'", drive, d->filename);
    goto fail;
  }

  if (rhh.flag1 & 0x80 && d->writeprot == 0) {
    fclose(d->file);
    d->file = fopen(d->filename, "rb");
    d->writeprot = 1;
  }

  /* Use the number of cylinders specified in the header */
  d->cyls = (rhh.cylhi << 8)    /* MSB */
          | (rhh.cyllo & 0xFF); /* LSB */
  if (d->cyls == 0) d->cyls = 256; /* Set default cylinders */

  secs = rhh.sec ? rhh.sec : 256;
  if (rhh.heads == 0) {
    /* header gives only secs/cyl. Compute number of heads from
       this and the assumed number of secs/track */

    /* use the number of secs/track that RSHARD requires */
    d->secs  = TRS_HARD_SEC_PER_TRK;
    d->heads = secs / TRS_HARD_SEC_PER_TRK;
  } else {
    /* header includes head count */
    d->heads = rhh.heads;
    d->secs  = secs / d->heads;
  }

#if ZBX
  if (trs_io_debug_flags & HARDDEBUG2)
    debug("hard_open: drive:%d cyls:%d heads:%d secs:%d\n",
          drive, d->cyls, d->heads, d->secs);
#endif

  if ((rhh.sec % d->secs) != 0 ||
      d->heads <= 0 || d->heads > TRS_HARD_MAXHEADS) {
    error("unusable geometry (%d heads/%d secs) in hard%d image: '%s'",
          d->heads, d->secs, drive, d->filename);
    goto fail;
  }

  state.size   = d->cyls * d->heads * d->secs;
  state.status = TRS_HARD_READY | TRS_HARD_SEEKDONE;
  return 0;

fail:
  if (d->file) fclose(d->file);
  d->file = NULL;
  d->filename[0] = 0;
  hard_error(TRS_HARD_NFERR);
  return err;
}

static void hard_read(int cmd)
{
#if ZBX
  if (trs_io_debug_flags & HARDDEBUG2)
    hard_debug("read");
#endif
  if (cmd & TRS_HARD_MULTI) {
    error("trs_hard: multi-sector read not supported (0x%02X)", cmd);
    hard_error(TRS_HARD_ABRTERR);
    return;
  }
  if (hard_sector(TRS_HARD_READY | TRS_HARD_SEEKDONE | TRS_HARD_DRQ) == 0) {
    if (sector_read(state.secsize) != 0) {
      hard_error(TRS_HARD_DATAERR); /* arbitrary choice */
    }
  }
}

static void hard_restore(void)
{
#if ZBX
  if (trs_io_debug_flags & HARDDEBUG2)
    debug("hard_restore drive:%d\n", state.drive);
#endif
  state.cyl = 0;
  /*!! should anything else be zeroed? */
  state.status = TRS_HARD_READY | TRS_HARD_SEEKDONE;
}

/*
 * Check whether the current position is in bounds for the geometry.
 * If not, set the controller error status and return -1.  If so, fseek
 * the file to the start of the current sector and set the controller
 * status to newstatus and return 0.
 */
static int hard_sector(int newstatus)
{
  if (state.d[state.drive].file == NULL)
    hard_open(state.drive);

  if (sector_seek(state.secsize) != 0) {
    hard_error(TRS_HARD_NFERR);
    return -1;
  } else {
    state.status = newstatus;
    return 0;
  }
}

static void hard_seek(void)
{
#if ZBX
  if (trs_io_debug_flags & HARDDEBUG2)
    hard_debug("seek/verify");
#endif
  hard_sector(TRS_HARD_READY | TRS_HARD_SEEKDONE);
}

static void hard_write(int cmd)
{
#if ZBX
  if (trs_io_debug_flags & HARDDEBUG2)
    hard_debug("write");
#endif
  if (cmd & TRS_HARD_MULTI) {
    error("trs_hard: multi-sector write not supported (0x%02X)", cmd);
    hard_error(TRS_HARD_ABRTERR);
    return;
  }
  hard_sector(TRS_HARD_READY | TRS_HARD_SEEKDONE | TRS_HARD_DRQ);
}

int trs_omti_in(int port)
{
  int v = 0xff;

  if (state.present) {
    switch (port & 3) {
    case 0:
      v = omti_data_in();
      break;
    case 1:
      v = state.status;
      break;
    case 2:
      /*
       * Read inverted drive configuration
       * W1 - W4 jumper: IN = 0 / OUT = 1.
       *
       * |   |   |   |   |W 4|W 3|W 2|W 1|
       * +---+---+---+---+---+---+---+---+
       * | 1 | 1 | 1 | 1 | 1 | 0 | 1 | 0 |
       */
      v = 0xfa; /* 612 cylinders, 2 heads, 10.2 MB */
      break;
    }
  }
#if ZBX
  if (trs_io_debug_flags & HARDDEBUG1)
    debug("[PC=%04X] trs_omti_in(%02X) = %02X %c\n",
        Z80_PC, port, v, TOPRINT(v));
#endif
  return v;
}

void trs_omti_out(int port, int value)
{
#if ZBX
  if (trs_io_debug_flags & HARDDEBUG1)
    debug("[PC=%04X] trs_omti_out(%02X), %02X %c\n",
        Z80_PC, port, value, TOPRINT(value));
#endif

  switch (port & 3) {
  case 0:
    omti_data_out(value);
    break;
  case 1:
    omti_reset();
    break;
  case 2:
    if (state.status == OMTI_IDLE) {
      state.control = 0;
      state.status  = OMTI_CMD_OUT;
    }
    break;
  }
}

static void omti_command(void)
{
  state.bytesdone = 0;
  state.command   = state.cdb[0];

  if (state.command != CMD_REQUEST_SENSE) {
    state.cyl    = ((state.cdb[2] & 0xC0) << 2) | state.cdb[3];
    state.drive  =  (state.cdb[1] & 0x20) >> 5;
    state.head   =   state.cdb[1] & 0x1F;
    state.secnum =   state.cdb[2] & 0x3F;
    state.seccnt =   state.cdb[4];
    state.error  = 0;
  }

#if ZBX
  if (trs_io_debug_flags & HARDDEBUG2)
    debug("OMTI command 0x%02X drive:%d error:0x%02X"
          " cyl:%d head:%d secnum:%d seccnt:%d\n",
        state.command, state.drive, state.error,
        state.cyl, state.head, state.secnum, state.seccnt);
#endif

  switch (state.command) {
  case CMD_TEST_DRIVE:
    if (omti_status(hard_open(state.drive)) == 0)
      state.secsize = OMTI_SECSIZE;
    else
      omti_status(hard_fail());
    break;

  case CMD_RECALIBRATE:
    state.cyl = state.head = state.secnum = 0;
    omti_status(sector_seek(OMTI_SECSIZE));
    break;

  case CMD_REQUEST_SENSE:
    state.secbuf[0] = state.error;
    state.secbuf[1] = (state.drive << 5) | (state.head & 0x1F);
    state.secbuf[2] = ((state.cyl >> 2) & 0xC0) | (state.secnum & 0x3F);
    state.secbuf[3] = state.cyl & 0xFF;
    state.secsize   = SENSE_BUF_LEN;
    state.status    = OMTI_DATA_IN;
    break;

  case CMD_FORMAT:
  case CMD_FORMAT_TRACK:
  case CMD_FORMAT_BADTRACK:
    if (omti_status(sector_seek(OMTI_SECSIZE)) == 0)
      omti_status(omti_format(state.command));
    break;

  case CMD_READ:
    if (omti_status(sector_seek(OMTI_SECSIZE)) == 0 &&
        omti_status(sector_read(OMTI_SECSIZE)) == 0) {
      state.secsize = OMTI_SECSIZE;
      state.status  = OMTI_DATA_IN;
    }
    break;

  case CMD_WRITE:
    if (omti_status(sector_seek(OMTI_SECSIZE)) == 0) {
      state.secsize = OMTI_SECSIZE;
      state.status  = OMTI_DATA_OUT;
    }
    break;

  case CMD_FORMAT_CHECK: /* Read Verify */
  case CMD_SEEK:
    omti_status(sector_seek(OMTI_SECSIZE));
    break;

  case CMD_INIT_DRIVE:
    state.secsize = DRIVE_CHAR_LEN;
    state.status  = OMTI_DATA_OUT;
    break;

  case CMD_WRITE_SEC_BUF:
    state.secsize = OMTI_SECSIZE;
    state.status  = OMTI_DATA_OUT;
    break;

  case CMD_READ_SEC_BUF:
    state.secsize = OMTI_SECSIZE;
    state.status  = OMTI_DATA_IN;
    break;

  case CMD_RAM_DIAGNOSTIC:
  case CMD_DRIVE_DIAGNOSTIC:
  case CMD_CTRL_DIAGNOSTIC:
    omti_status(0);
    break;

  default:
    error("OMTI unknown command 0x%02X", state.command);
    omti_status(ERR_INVALID_COMMAND);
  }
}

static int omti_data_in(void)
{
  switch (state.status) {
  case OMTI_DATA_IN:
    if (state.bytesdone < state.secsize) {
      int const v = state.secbuf[state.bytesdone++];

      if (state.bytesdone == state.secsize) {
        if (state.command == CMD_READ && --state.seccnt) {
          if (sector_read(OMTI_SECSIZE) != 0) {
            omti_status(ERR_DATA_ERROR | 0x80);
            return v;
          }
          state.bytesdone = 0;
        } else {
          omti_status(0);
        }
      }
      return v;
    }
    break;

  case OMTI_STATUS:
    omti_reset();
    return (state.error ? ERROR_CODE : 0) | (state.drive << 5);
  }

  return 0xff;
}

static void omti_data_out(Uint8 value)
{
  switch (state.status) {
  case OMTI_CMD_OUT:
    state.cdb[state.control++] = value;
    if (state.control == CDB_LEN)
      omti_command();
    break;

  case OMTI_DATA_OUT:
    if (state.bytesdone < state.secsize) {
      state.secbuf[state.bytesdone++] = value;
      if (state.bytesdone == state.secsize) {
        if (state.command == CMD_WRITE) {
          if (sector_write(OMTI_SECSIZE) != 0) {
            omti_status(ERR_WRITE_FAULT | 0x80);
            break;
          }
          if (--state.seccnt) {
            state.bytesdone = 0;
            break;
          }
        }
#if ZBX
        else if (state.command == CMD_INIT_DRIVE &&
            trs_io_debug_flags & HARDDEBUG2) {
          debug("OMTI init drive:%d cyls:%d heads:%d\n", state.drive,
             (state.secbuf[0] << 8) | state.secbuf[1], state.secbuf[2]);
        }
#endif
        omti_status(0);
      }
    }
    break;
  }
}

static int omti_format(int cmd)
{
  const Drive *d = &state.d[state.drive];
  Uint32 sect = state.cyl * state.head * state.secnum;
  Uint32 const last = cmd == CMD_FORMAT ? state.size : sect + d->secs;

  if (last > state.size)
    return ERR_ILLEGAL_ADDRESS | 0x80;

  if (state.cdb[5] & 0x20)
    /* Fill sector buffer with default byte */
    memset(state.secbuf, 0x6C, OMTI_SECSIZE);

  while (sect < last) {
    sect++;
    if (sector_write(OMTI_SECSIZE) != 0) {
      return ERR_FORMAT_ERROR | 0x80;
    }
  }

  return 0;
}

static void omti_reset(void)
{
  state.control = 0;
  state.status  = OMTI_IDLE;
}

static int omti_status(int error)
{
  state.error  = error;
  state.status = OMTI_STATUS;
  return error;
}

int trs_sasi_in(int port)
{
  int v = 0xff;

  if (state.present) {
    switch (port) {
    case 0:
      v = sasi_data_in();
      break;
    case 1:
      v = state.status;
      if (v == 0x51) /* Kludge needed for GENIE-DOS 2.4 */
        state.status = SASI_IDLE;
      break;
    }
  }
#if ZBX
  if (trs_io_debug_flags & HARDDEBUG1)
    debug("[PC=%04X] trs_sasi_in(%02X) = %02X %c\n",
        Z80_PC, port, v, TOPRINT(v));
#endif
  return v;
}

void trs_sasi_out(int port, int value)
{
#if ZBX
  if (trs_io_debug_flags & HARDDEBUG1)
    debug("[PC=%04X] trs_sasi_out(%02X), %02X %c\n",
        Z80_PC, port, value, TOPRINT(value));
#endif

  switch (port) {
  case 0:
    sasi_data_out(value);
    break;
  case 1:
    sasi_reset();
    break;
  case 2:
    if (state.status == SASI_IDLE) {
      state.control = 0;
      state.status  = SASI_CMD_OUT;
    }
    break;
  }
}

static void sasi_command(void)
{
  state.bytesdone = 0;
  state.command   = state.cdb[0];

  if (state.command != CMD_REQUEST_SENSE) {
    state.cyl    = ((state.cdb[1] & 0x1F) << 16)
                 |  (state.cdb[2] << 8)
                 |   state.cdb[3];
    state.drive  =  (state.cdb[1] & 0x20) >> 5;
    state.seccnt =   state.cdb[4];
    state.error  = 0;
  }

#if ZBX
  if (trs_io_debug_flags & HARDDEBUG2)
    debug("SASI command 0x%02X drive:%d error:0x%02X lba:%d/%d seccnt:%d\n",
        state.command, state.drive, state.error,
        state.cyl, state.size, state.seccnt);
#endif

  switch (state.command) {
  case CMD_TEST_DRIVE:
    if (sasi_status(hard_open(state.drive)) == 0)
      state.secsize = SASI_SECSIZE;
    else
      sasi_status(hard_fail());
    break;

  case CMD_RECALIBRATE:
    state.cyl = 0;
    sasi_status(sasi_seek());
    break;

  case CMD_REQUEST_SENSE:
    state.secbuf[0] = state.error;
    state.secbuf[1] = ((state.cyl >> 16) & 0x1F) | (state.drive << 5);
    state.secbuf[2] = state.cyl >> 8;
    state.secbuf[3] = state.cyl;
    state.secsize   = SENSE_BUF_LEN;
    state.status    = SASI_DATA_IN;
    break;

  case CMD_FORMAT:
  case CMD_FORMAT_TRACK:
  case CMD_FORMAT_BADTRACK:
    if (sasi_status(sasi_seek()) == 0)
      sasi_status(sasi_format(state.command));
    break;

  case CMD_READ:
    if (sasi_status(sasi_seek()) == 0 &&
        sasi_status(sector_read(SASI_SECSIZE)) == 0) {
      state.secsize = SASI_SECSIZE;
      state.status  = SASI_DATA_IN;
    }
    break;

  case CMD_WRITE:
    if (sasi_status(sasi_seek()) == 0) {
      state.secsize = SASI_SECSIZE;
      state.status  = SASI_DATA_OUT;
    }
    break;

  case CMD_FORMAT_CHECK:
  case CMD_READ_VERIFY:
  case CMD_SEEK:
    sasi_status(sasi_seek());
    break;

  case CMD_INIT_DRIVE:
    state.secsize = DRIVE_CHAR_LEN;
    state.status  = SASI_DATA_OUT;
    break;

  case CMD_WRITE_SEC_BUF:
    state.secsize = SASI_SECSIZE;
    state.status  = SASI_DATA_OUT;
    break;

  case CMD_READ_SEC_BUF:
    state.secsize = SASI_SECSIZE;
    state.status  = SASI_DATA_IN;
    break;

  case CMD_RAM_DIAGNOSTIC:
  case CMD_DRIVE_DIAGNOSTIC:
  case CMD_CTRL_DIAGNOSTIC:
    sasi_status(0);
    break;

  default:
    error("SASI unknown command 0x%02X", state.command);
    sasi_status(ERR_INVALID_COMMAND);
  }
}

static int sasi_data_in(void)
{
  switch (state.status) {
  case SASI_DATA_IN:
    if (state.bytesdone < state.secsize) {
      int const v = state.secbuf[state.bytesdone++];

      if (state.bytesdone == state.secsize) {
        if (state.command == CMD_READ && --state.seccnt) {
          if (sector_read(SASI_SECSIZE) != 0) {
            sasi_status(ERR_DATA_ERROR | 0x80);
            return v;
          }
          state.bytesdone = 0;
        } else {
          sasi_status(0);
        }
      }
      return v;
    }
    break;

  case SASI_IDLE:
    return state.secbuf[0];

  case SASI_STATUS:
    if (state.sdh == 0) {
      state.sdh = 1;
      return (state.error ? ERROR_CODE : 0) | (state.drive << 5);
    } else {
      sasi_reset();
      return 0;
    }
  }

  return 0xff;
}

static void sasi_data_out(Uint8 value)
{
  switch (state.status) {
  case SASI_CMD_OUT:
    state.cdb[state.control++] = value;
    if (state.control == CDB_LEN)
      sasi_command();
    break;

  case SASI_DATA_OUT:
    if (state.bytesdone < state.secsize) {
      state.secbuf[state.bytesdone++] = value;
      if (state.bytesdone == state.secsize) {
        if (state.command == CMD_WRITE) {
          if (sector_write(SASI_SECSIZE) != 0) {
            sasi_status(ERR_WRITE_FAULT | 0x80);
            break;
          }
          if (--state.seccnt) {
            state.bytesdone = 0;
            break;
          }
        }
#if ZBX
        else if (state.command == CMD_INIT_DRIVE &&
            trs_io_debug_flags & HARDDEBUG2) {
          debug("SASI init drive:%d cyls:%d heads:%d\n", state.drive,
             (state.secbuf[0] << 8) | state.secbuf[1], state.secbuf[2]);
        }
#endif
        sasi_status(0);
      }
    }
    break;

  case SASI_IDLE:
    state.secbuf[0] = value;
  }
}

static int sasi_format(int cmd)
{
  const Drive *d = &state.d[state.drive];
  Uint32 const last = cmd == CMD_FORMAT ? state.size : state.cyl + d->secs;

  if (last > state.size)
    return ERR_ILLEGAL_ADDRESS | 0x80;

  if ((state.cdb[5] & 0x20) == 0)
    /* Fill sector buffer with default byte */
    memset(state.secbuf, 0x6C, SASI_SECSIZE);

  while (state.cyl < last) {
    state.cyl++;
    if (sector_write(SASI_SECSIZE) != 0) {
      return ERR_FORMAT_ERROR | 0x80;
    }
  }

  fflush(d->file);
  return 0;
}

static void sasi_reset(void)
{
  state.control = 0;
  state.sdh     = 0;
  state.status  = SASI_IDLE;
}

static int sasi_seek(void)
{
  const Drive *d = &state.d[state.drive];

  if (d->file == NULL)
    return hard_fail();

  if (state.cyl > state.size) {
    error("SASI hard%d command 0x%02X: requested lba %d/%d",
        state.drive, state.command, state.cyl, state.size);
    return ERR_ILLEGAL_ADDRESS | 0x80;
  }

  if (fseek(d->file,
      sizeof(ReedHardHeader) +
      (state.cyl * SASI_SECSIZE), 0) != 0) {
    file_error("hard%d: fseek '%s'", state.drive, d->filename);
    return ERR_SECTOR_NOT_FOUND | 0x80;
  }

  if (trs_show_led)
    trs_hard_led(state.drive, 1);

  return 0;
}

static int sasi_status(int error)
{
  state.error  = error;
  state.sdh    = 0;
  state.status = SASI_STATUS;
  return error;
}

static void trs_save_harddrive(FILE *file, Drive *d)
{
  int file_not_null = (d->file != NULL);

  trs_save_int(file, &file_not_null, 1);
  trs_save_filename(file, d->filename);
  trs_save_int(file, &d->writeprot, 1);
  trs_save_int(file, &d->cyls, 1);
  trs_save_int(file, &d->heads, 1);
  trs_save_int(file, &d->secs, 1);
}

static void trs_load_harddrive(FILE *file, Drive *d)
{
  int file_not_null;

  trs_load_int(file, &file_not_null, 1);

  if (file_not_null)
    d->file = (FILE *) 1;
  else
    d->file = NULL;

  trs_load_filename(file, d->filename);
  trs_load_int(file, &d->writeprot, 1);
  trs_load_int(file, &d->cyls, 1);
  trs_load_int(file, &d->heads, 1);
  trs_load_int(file, &d->secs, 1);
}

void trs_hard_save(FILE *file)
{
  int i;

  trs_save_int(file, &state.present, 1);
  trs_save_int(file, &state.bytesdone, 1);
  trs_save_uint8(file, state.cdb, CDB_LEN);
  trs_save_uint8(file, &state.command, 1);
  trs_save_uint8(file, &state.control, 1);
  trs_save_uint32(file, &state.cyl);
  trs_save_uint8(file, &state.drive, 1);
  trs_save_uint8(file, &state.error, 1);
  trs_save_uint8(file, &state.head, 1);
  trs_save_uint8(file, &state.sdh, 1);
  trs_save_uint8(file, state.secbuf, SECTORSIZE);
  trs_save_uint8(file, &state.seccnt, 1);
  trs_save_uint8(file, &state.secnum, 1);
  trs_save_uint16(file, &state.secsize);
  trs_save_uint32(file, &state.size);
  trs_save_uint8(file, &state.status, 1);

  for (i = 0; i < TRS_HARD_MAXDRIVES; i++)
    trs_save_harddrive(file, &state.d[i]);
}

void trs_hard_load(FILE *file)
{
  int i;

  for (i = 0; i < TRS_HARD_MAXDRIVES; i++) {
    if (state.d[i].file != NULL)
      fclose(state.d[i].file);
  }

  trs_load_int(file, &state.present, 1);
  trs_load_int(file, &state.bytesdone, 1);
  trs_load_uint8(file, state.cdb, CDB_LEN);
  trs_load_uint8(file, &state.command, 1);
  trs_load_uint8(file, &state.control, 1);
  trs_load_uint32(file, &state.cyl);
  trs_load_uint8(file, &state.drive, 1);
  trs_load_uint8(file, &state.error, 1);
  trs_load_uint8(file, &state.head, 1);
  trs_load_uint8(file, &state.sdh, 1);
  trs_load_uint8(file, state.secbuf, SECTORSIZE);
  trs_load_uint8(file, &state.seccnt, 1);
  trs_load_uint8(file, &state.secnum, 1);
  trs_load_uint16(file, &state.secsize);
  trs_load_uint32(file, &state.size);
  trs_load_uint8(file, &state.status, 1);

  for (i = 0; i < TRS_HARD_MAXDRIVES; i++) {
    trs_load_harddrive(file, &state.d[i]);

    if (state.d[i].file != NULL) {
      state.d[i].file = fopen(state.d[i].filename, "rb+");
      if (state.d[i].file == NULL) {
        state.d[i].file = fopen(state.d[i].filename, "rb");
        if (state.d[i].file == NULL) {
          file_error("load hard%d: '%s'", i, state.d[i].filename);
          state.d[i].filename[0] = 0;
          state.d[i].writeprot = 0;
          continue;
        }
        state.d[i].writeprot = 1;
      } else {
        state.d[i].writeprot = 0;
      }
    }
  }
}

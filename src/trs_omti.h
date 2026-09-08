#ifndef _TRS_OMTI_H
#define _TRS_OMTI_H

#define OMTI_SECSIZE   (512) /* Default sector size for OMTI controller */
/*
 * OMTI status                  |   |   |IRQ|DRQ|BSY|C/D|I/O|REQ|
 *                              +---+---+---+---+---+---+---+---+ */
#define OMTI_IDLE       0xC0 /* | 1 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | */
#define OMTI_DATA_OUT   0xC9 /* | 1 | 1 | 0 | 0 | 1 | 0 | 0 | 1 | */
#define OMTI_DATA_IN    0xCB /* | 1 | 1 | 0 | 0 | 1 | 0 | 1 | 1 | */
#define OMTI_CMD_OUT    0xCD /* | 1 | 1 | 0 | 0 | 1 | 1 | 0 | 1 | */
#define OMTI_READY      0xCF /* | 1 | 1 | 0 | 0 | 1 | 1 | 1 | 1 | */

#define OMTI_ERROR      0x02 /* error value */

/*
 * OMTI commands
 */
#define OMTI_TEST_DRIVE      0x00
#define OMTI_RECALIBRATE     0x01
#define OMTI_REQUEST_SENSE   0x03
#define OMTI_FORMAT          0x04
#define OMTI_READ_VERIFY     0x05
#define OMTI_READ            0x08
#define OMTI_WRITE           0x0a
#define OMTI_SEEK            0x0b
#define OMTI_INIT_DRIVE      0x0c
#define OMTI_WRITE_SEC_BUF   0x0f
#define OMTI_RAM_DIAGNOSTIC  0xe0
#define OMTI_CTRL_DIAGNOSTIC 0xe4

/* Drive characteristics parameter block length */
#define OMTI_CHAR_LEN   8

#endif /* _TRS_OMTI_H */

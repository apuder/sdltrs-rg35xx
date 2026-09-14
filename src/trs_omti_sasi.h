#ifndef _TRS_OMTI_SASI_H
#define _TRS_OMTI_SASI_H

#define OMTI_SECSIZE  (512) /* Default sector size for OMTI */
#define SASI_SECSIZE  (256) /* Default sector size for SASI */

#define DRIVE_CHAR_LEN (8)  /* Drive characteristics length */
#define SENSE_BUF_LEN  (4)  /* Sense buffer length */

/*
 * OMTI status                 |   |   |IRQ|DRQ|BSY|C/D|I/O|REQ|
 *                             +---+---+---+---+---+---+---+---+ */
#define OMTI_IDLE      0xC0 /* | 1 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | */
#define OMTI_DATA_OUT  0xC9 /* | 1 | 1 | 0 | 0 | 1 | 0 | 0 | 1 | */
#define OMTI_DATA_IN   0xCB /* | 1 | 1 | 0 | 0 | 1 | 0 | 1 | 1 | */
#define OMTI_CMD_OUT   0xCD /* | 1 | 1 | 0 | 0 | 1 | 1 | 0 | 1 | */
#define OMTI_STATUS    0xCF /* | 1 | 1 | 0 | 0 | 1 | 1 | 1 | 1 | */

/*
 * SASI status                 |   |   |   |I/O|C/D|   |BSY|REQ|
 *                             +---+---+---+---+---+---+---+---+ */
#define SASI_IDLE      0x00 /* | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | */
#define SASI_DATA_OUT  0x03 /* | 0 | 0 | 0 | 0 | 0 | 0 | 1 | 1 | */
#define SASI_DATA_IN   0x13 /* | 0 | 0 | 0 | 1 | 0 | 0 | 1 | 1 | */
#define SASI_CMD_OUT   0x0B /* | 0 | 0 | 0 | 0 | 1 | 0 | 1 | 1 | */
#define SASI_STATUS    0x1B /* | 0 | 0 | 0 | 1 | 1 | 0 | 1 | 1 | */

/*
 * OMTI/SASI commands
 */
#define CMD_TEST_DRIVE      0x00
#define CMD_RECALIBRATE     0x01
#define CMD_REQUEST_SENSE   0x03
#define CMD_FORMAT          0x04
#define CMD_READ_VERIFY     0x05
#define CMD_READ            0x08
#define CMD_WRITE           0x0A
#define CMD_SEEK            0x0B
#define CMD_INIT_DRIVE      0x0C
#define CMD_WRITE_SEC_BUF   0x0F
#define CMD_RAM_DIAGNOSTIC  0xE0
#define CMD_CTRL_DIAGNOSTIC 0xE4

#endif /* _TRS_OMTI_SASI_H */

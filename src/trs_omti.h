#ifndef _TRS_OMTI_H
#define _TRS_OMTI_H

#define OMTI_SECSIZE   (512) /* Default sector size for OMTI controller */
/*
 * OMTI status
 */
#define OMTI_IDLE       0xc0 /* idle, ready to accept SELECT */
#define OMTI_DATA_OUT   0xc9 /* data phase: byte wanted at OMTI port */
#define OMTI_DATA_IN    0xcb /* data phase: byte available at OMTI port */
#define OMTI_CMD_OUT    0xcd /* command output phase */
#define OMTI_READY      0xcf /* command complete, status ready */
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

/* SET DRIVE CHARACTERISTICS parameter block */
#define OMTI_CHAR_LEN   8
#define OMTI_CHAR_CYLHI 0
#define OMTI_CHAR_CYLLO 1
#define OMTI_CHAR_HEADS 2

#endif /* _TRS_OMTI_H */

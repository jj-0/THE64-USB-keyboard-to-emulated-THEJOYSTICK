/*
 * keyboard2thejoystick - USB Keyboard to Virtual THEJOYSTICK Translator
 *
 * Creates a virtual THEJOYSTICK via Linux uinput, translating USB keyboard
 * input to joystick events. This lets users play games on THEC64 Mini/Maxi
 * with a keyboard as if a THEJOYSTICK were connected.
 *
 * The virtual device matches real THEJOYSTICK hardware:
 *   Name: "Retro Games LTD THEC64 Joystick"
 *   ID: bustype=0x0003, vendor=0x1c59, product=0x0023, version=0x0110
 *
 * Cross-compile:
 *   arm-linux-gnueabihf-gcc -static -O2 -o keyboard2thejoystick keyboard2thejoystick.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <linux/uinput.h>

/* ================================================================
 * Constants
 * ================================================================ */

#define MAX_KEYBOARDS     8
#define MAX_PATH_LEN      512
#define MAX_NAME_LEN      256
#define MAX_DIR_ENTRIES   256
#define NUM_DIRECTIONS    8
#define NUM_BUTTONS       8
#define NUM_MAPPINGS      16  /* 8 directions + 8 buttons */

#define FONT_W            8
#define FONT_H            16

#define FRAME_MS          16
#define BLINK_MS          400
#define DEBOUNCE_MS       200

#define BITS_PER_LONG     (sizeof(long) * 8)
#define NBITS(x)          ((((x) - 1) / BITS_PER_LONG) + 1)
#define TEST_BIT(bit, a)  ((a[(bit) / BITS_PER_LONG] >> ((bit) % BITS_PER_LONG)) & 1)

/* Virtual THEJOYSTICK device identity */
#define VDEV_NAME         "Retro Games LTD THEC64 Joystick"
#define VDEV_BUSTYPE      0x0003
#define VDEV_VENDOR       0x1c59
#define VDEV_PRODUCT      0x0023
#define VDEV_VERSION      0x0110

/* Axis parameters */
#define AXIS_MIN          0
#define AXIS_MAX          255
#define AXIS_CENTER       127
#define AXIS_FLAT         15

/* Colours (0xAARRGGBB) - for guimap mode */
#define COL_BG            0xFF101828
#define COL_BODY          0xFF4A4A6A
#define COL_BODY_DARK     0xFF36364E
#define COL_STICK_BASE    0xFF5A5A7A
#define COL_STICK         0xFF6E6E90
#define COL_STICK_TOP     0xFF8888AA
#define COL_BTN           0xFF505078
#define COL_BTN_FIRE      0xFF6E4444
#define COL_HIGHLIGHT     0xFFFFCC00
#define COL_MAPPED        0xFF22BB66
#define COL_TEXT          0xFFD0D0E0
#define COL_TEXT_DIM      0xFF707088
#define COL_TEXT_TITLE    0xFFFFFFFF
#define COL_SELECTED      0xFF2A4488
#define COL_BORDER        0xFF5566AA
#define COL_ERROR         0xFFFF4444
#define COL_SUCCESS       0xFF44FF88
#define COL_HEADER_BG     0xFF182040

/* ================================================================
 * Built-in 8x16 VGA bitmap font (printable ASCII 0x20..0x7E)
 * ================================================================ */

static const uint8_t font8x16[95][16] = {
    /* 0x20 ' ' */ {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    /* 0x21 '!' */ {0x00,0x00,0x18,0x3C,0x3C,0x3C,0x18,0x18,0x18,0x00,0x18,0x18,0x00,0x00,0x00,0x00},
    /* 0x22 '"' */ {0x00,0x66,0x66,0x66,0x24,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    /* 0x23 '#' */ {0x00,0x00,0x00,0x6C,0x6C,0xFE,0x6C,0x6C,0xFE,0x6C,0x6C,0x00,0x00,0x00,0x00,0x00},
    /* 0x24 '$' */ {0x18,0x18,0x7C,0xC6,0xC2,0xC0,0x7C,0x06,0x06,0x86,0xC6,0x7C,0x18,0x18,0x00,0x00},
    /* 0x25 '%' */ {0x00,0x00,0x00,0x00,0xC2,0xC6,0x0C,0x18,0x30,0x60,0xC6,0x86,0x00,0x00,0x00,0x00},
    /* 0x26 '&' */ {0x00,0x00,0x38,0x6C,0x6C,0x38,0x76,0xDC,0xCC,0xCC,0xCC,0x76,0x00,0x00,0x00,0x00},
    /* 0x27 ''' */ {0x00,0x30,0x30,0x30,0x60,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    /* 0x28 '(' */ {0x00,0x00,0x0C,0x18,0x30,0x30,0x30,0x30,0x30,0x30,0x18,0x0C,0x00,0x00,0x00,0x00},
    /* 0x29 ')' */ {0x00,0x00,0x30,0x18,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x18,0x30,0x00,0x00,0x00,0x00},
    /* 0x2A '*' */ {0x00,0x00,0x00,0x00,0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00,0x00,0x00,0x00,0x00},
    /* 0x2B '+' */ {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x7E,0x18,0x18,0x00,0x00,0x00,0x00,0x00,0x00},
    /* 0x2C ',' */ {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x18,0x30,0x00,0x00,0x00},
    /* 0x2D '-' */ {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFE,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    /* 0x2E '.' */ {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x00},
    /* 0x2F '/' */ {0x00,0x00,0x00,0x00,0x02,0x06,0x0C,0x18,0x30,0x60,0xC0,0x80,0x00,0x00,0x00,0x00},
    /* 0x30 '0' */ {0x00,0x00,0x7C,0xC6,0xC6,0xCE,0xDE,0xF6,0xE6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00},
    /* 0x31 '1' */ {0x00,0x00,0x18,0x38,0x78,0x18,0x18,0x18,0x18,0x18,0x18,0x7E,0x00,0x00,0x00,0x00},
    /* 0x32 '2' */ {0x00,0x00,0x7C,0xC6,0x06,0x0C,0x18,0x30,0x60,0xC0,0xC6,0xFE,0x00,0x00,0x00,0x00},
    /* 0x33 '3' */ {0x00,0x00,0x7C,0xC6,0x06,0x06,0x3C,0x06,0x06,0x06,0xC6,0x7C,0x00,0x00,0x00,0x00},
    /* 0x34 '4' */ {0x00,0x00,0x0C,0x1C,0x3C,0x6C,0xCC,0xFE,0x0C,0x0C,0x0C,0x1E,0x00,0x00,0x00,0x00},
    /* 0x35 '5' */ {0x00,0x00,0xFE,0xC0,0xC0,0xC0,0xFC,0x06,0x06,0x06,0xC6,0x7C,0x00,0x00,0x00,0x00},
    /* 0x36 '6' */ {0x00,0x00,0x38,0x60,0xC0,0xC0,0xFC,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00},
    /* 0x37 '7' */ {0x00,0x00,0xFE,0xC6,0x06,0x06,0x0C,0x18,0x30,0x30,0x30,0x30,0x00,0x00,0x00,0x00},
    /* 0x38 '8' */ {0x00,0x00,0x7C,0xC6,0xC6,0xC6,0x7C,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00},
    /* 0x39 '9' */ {0x00,0x00,0x7C,0xC6,0xC6,0xC6,0x7E,0x06,0x06,0x06,0x0C,0x78,0x00,0x00,0x00,0x00},
    /* 0x3A ':' */ {0x00,0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x00,0x00},
    /* 0x3B ';' */ {0x00,0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x18,0x18,0x30,0x00,0x00,0x00,0x00},
    /* 0x3C '<' */ {0x00,0x00,0x00,0x06,0x0C,0x18,0x30,0x60,0x30,0x18,0x0C,0x06,0x00,0x00,0x00,0x00},
    /* 0x3D '=' */ {0x00,0x00,0x00,0x00,0x00,0x7E,0x00,0x00,0x7E,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    /* 0x3E '>' */ {0x00,0x00,0x00,0x60,0x30,0x18,0x0C,0x06,0x0C,0x18,0x30,0x60,0x00,0x00,0x00,0x00},
    /* 0x3F '?' */ {0x00,0x00,0x7C,0xC6,0xC6,0x0C,0x18,0x18,0x18,0x00,0x18,0x18,0x00,0x00,0x00,0x00},
    /* 0x40 '@' */ {0x00,0x00,0x00,0x7C,0xC6,0xC6,0xDE,0xDE,0xDE,0xDC,0xC0,0x7C,0x00,0x00,0x00,0x00},
    /* 0x41 'A' */ {0x00,0x00,0x10,0x38,0x6C,0xC6,0xC6,0xFE,0xC6,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00},
    /* 0x42 'B' */ {0x00,0x00,0xFC,0x66,0x66,0x66,0x7C,0x66,0x66,0x66,0x66,0xFC,0x00,0x00,0x00,0x00},
    /* 0x43 'C' */ {0x00,0x00,0x3C,0x66,0xC2,0xC0,0xC0,0xC0,0xC0,0xC2,0x66,0x3C,0x00,0x00,0x00,0x00},
    /* 0x44 'D' */ {0x00,0x00,0xF8,0x6C,0x66,0x66,0x66,0x66,0x66,0x66,0x6C,0xF8,0x00,0x00,0x00,0x00},
    /* 0x45 'E' */ {0x00,0x00,0xFE,0x66,0x62,0x68,0x78,0x68,0x60,0x62,0x66,0xFE,0x00,0x00,0x00,0x00},
    /* 0x46 'F' */ {0x00,0x00,0xFE,0x66,0x62,0x68,0x78,0x68,0x60,0x60,0x60,0xF0,0x00,0x00,0x00,0x00},
    /* 0x47 'G' */ {0x00,0x00,0x3C,0x66,0xC2,0xC0,0xC0,0xDE,0xC6,0xC6,0x66,0x3A,0x00,0x00,0x00,0x00},
    /* 0x48 'H' */ {0x00,0x00,0xC6,0xC6,0xC6,0xC6,0xFE,0xC6,0xC6,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00},
    /* 0x49 'I' */ {0x00,0x00,0x3C,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00},
    /* 0x4A 'J' */ {0x00,0x00,0x1E,0x0C,0x0C,0x0C,0x0C,0x0C,0xCC,0xCC,0xCC,0x78,0x00,0x00,0x00,0x00},
    /* 0x4B 'K' */ {0x00,0x00,0xE6,0x66,0x66,0x6C,0x78,0x78,0x6C,0x66,0x66,0xE6,0x00,0x00,0x00,0x00},
    /* 0x4C 'L' */ {0x00,0x00,0xF0,0x60,0x60,0x60,0x60,0x60,0x60,0x62,0x66,0xFE,0x00,0x00,0x00,0x00},
    /* 0x4D 'M' */ {0x00,0x00,0xC6,0xEE,0xFE,0xFE,0xD6,0xC6,0xC6,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00},
    /* 0x4E 'N' */ {0x00,0x00,0xC6,0xE6,0xF6,0xFE,0xDE,0xCE,0xC6,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00},
    /* 0x4F 'O' */ {0x00,0x00,0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00},
    /* 0x50 'P' */ {0x00,0x00,0xFC,0x66,0x66,0x66,0x7C,0x60,0x60,0x60,0x60,0xF0,0x00,0x00,0x00,0x00},
    /* 0x51 'Q' */ {0x00,0x00,0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xD6,0xDE,0x7C,0x0C,0x0E,0x00,0x00},
    /* 0x52 'R' */ {0x00,0x00,0xFC,0x66,0x66,0x66,0x7C,0x6C,0x66,0x66,0x66,0xE6,0x00,0x00,0x00,0x00},
    /* 0x53 'S' */ {0x00,0x00,0x7C,0xC6,0xC6,0x60,0x38,0x0C,0x06,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00},
    /* 0x54 'T' */ {0x00,0x00,0xFF,0xDB,0x99,0x18,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00},
    /* 0x55 'U' */ {0x00,0x00,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00},
    /* 0x56 'V' */ {0x00,0x00,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x6C,0x38,0x10,0x00,0x00,0x00,0x00},
    /* 0x57 'W' */ {0x00,0x00,0xC6,0xC6,0xC6,0xC6,0xC6,0xD6,0xD6,0xFE,0x6C,0x6C,0x00,0x00,0x00,0x00},
    /* 0x58 'X' */ {0x00,0x00,0xC6,0xC6,0x6C,0x7C,0x38,0x38,0x7C,0x6C,0xC6,0xC6,0x00,0x00,0x00,0x00},
    /* 0x59 'Y' */ {0x00,0x00,0xC3,0xC3,0x66,0x3C,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00},
    /* 0x5A 'Z' */ {0x00,0x00,0xFE,0xC6,0x86,0x0C,0x18,0x30,0x60,0xC2,0xC6,0xFE,0x00,0x00,0x00,0x00},
    /* 0x5B '[' */ {0x00,0x00,0x3C,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x3C,0x00,0x00,0x00,0x00},
    /* 0x5C '\' */ {0x00,0x00,0x00,0x80,0xC0,0xE0,0x70,0x38,0x1C,0x0E,0x06,0x02,0x00,0x00,0x00,0x00},
    /* 0x5D ']' */ {0x00,0x00,0x3C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x3C,0x00,0x00,0x00,0x00},
    /* 0x5E '^' */ {0x10,0x38,0x6C,0xC6,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    /* 0x5F '_' */ {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0x00,0x00,0x00},
    /* 0x60 '`' */ {0x30,0x30,0x18,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    /* 0x61 'a' */ {0x00,0x00,0x00,0x00,0x00,0x78,0x0C,0x7C,0xCC,0xCC,0xCC,0x76,0x00,0x00,0x00,0x00},
    /* 0x62 'b' */ {0x00,0x00,0xE0,0x60,0x60,0x78,0x6C,0x66,0x66,0x66,0x66,0x7C,0x00,0x00,0x00,0x00},
    /* 0x63 'c' */ {0x00,0x00,0x00,0x00,0x00,0x7C,0xC6,0xC0,0xC0,0xC0,0xC6,0x7C,0x00,0x00,0x00,0x00},
    /* 0x64 'd' */ {0x00,0x00,0x1C,0x0C,0x0C,0x3C,0x6C,0xCC,0xCC,0xCC,0xCC,0x76,0x00,0x00,0x00,0x00},
    /* 0x65 'e' */ {0x00,0x00,0x00,0x00,0x00,0x7C,0xC6,0xFE,0xC0,0xC0,0xC6,0x7C,0x00,0x00,0x00,0x00},
    /* 0x66 'f' */ {0x00,0x00,0x1C,0x36,0x32,0x30,0x78,0x30,0x30,0x30,0x30,0x78,0x00,0x00,0x00,0x00},
    /* 0x67 'g' */ {0x00,0x00,0x00,0x00,0x00,0x76,0xCC,0xCC,0xCC,0xCC,0x7C,0x0C,0xCC,0x78,0x00,0x00},
    /* 0x68 'h' */ {0x00,0x00,0xE0,0x60,0x60,0x6C,0x76,0x66,0x66,0x66,0x66,0xE6,0x00,0x00,0x00,0x00},
    /* 0x69 'i' */ {0x00,0x00,0x18,0x18,0x00,0x38,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00},
    /* 0x6A 'j' */ {0x00,0x00,0x06,0x06,0x00,0x0E,0x06,0x06,0x06,0x06,0x06,0x06,0x66,0x3C,0x00,0x00},
    /* 0x6B 'k' */ {0x00,0x00,0xE0,0x60,0x60,0x66,0x6C,0x78,0x78,0x6C,0x66,0xE6,0x00,0x00,0x00,0x00},
    /* 0x6C 'l' */ {0x00,0x00,0x38,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00},
    /* 0x6D 'm' */ {0x00,0x00,0x00,0x00,0x00,0xEC,0xFE,0xD6,0xD6,0xD6,0xD6,0xC6,0x00,0x00,0x00,0x00},
    /* 0x6E 'n' */ {0x00,0x00,0x00,0x00,0x00,0xDC,0x66,0x66,0x66,0x66,0x66,0x66,0x00,0x00,0x00,0x00},
    /* 0x6F 'o' */ {0x00,0x00,0x00,0x00,0x00,0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00},
    /* 0x70 'p' */ {0x00,0x00,0x00,0x00,0x00,0xDC,0x66,0x66,0x66,0x66,0x7C,0x60,0x60,0xF0,0x00,0x00},
    /* 0x71 'q' */ {0x00,0x00,0x00,0x00,0x00,0x76,0xCC,0xCC,0xCC,0xCC,0x7C,0x0C,0x0C,0x1E,0x00,0x00},
    /* 0x72 'r' */ {0x00,0x00,0x00,0x00,0x00,0xDC,0x76,0x66,0x60,0x60,0x60,0xF0,0x00,0x00,0x00,0x00},
    /* 0x73 's' */ {0x00,0x00,0x00,0x00,0x00,0x7C,0xC6,0x60,0x38,0x0C,0xC6,0x7C,0x00,0x00,0x00,0x00},
    /* 0x74 't' */ {0x00,0x00,0x10,0x30,0x30,0xFC,0x30,0x30,0x30,0x30,0x36,0x1C,0x00,0x00,0x00,0x00},
    /* 0x75 'u' */ {0x00,0x00,0x00,0x00,0x00,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0x76,0x00,0x00,0x00,0x00},
    /* 0x76 'v' */ {0x00,0x00,0x00,0x00,0x00,0xC3,0xC3,0xC3,0xC3,0x66,0x3C,0x18,0x00,0x00,0x00,0x00},
    /* 0x77 'w' */ {0x00,0x00,0x00,0x00,0x00,0xC6,0xC6,0xD6,0xD6,0xD6,0xFE,0x6C,0x00,0x00,0x00,0x00},
    /* 0x78 'x' */ {0x00,0x00,0x00,0x00,0x00,0xC6,0x6C,0x38,0x38,0x38,0x6C,0xC6,0x00,0x00,0x00,0x00},
    /* 0x79 'y' */ {0x00,0x00,0x00,0x00,0x00,0xC6,0xC6,0xC6,0xC6,0xC6,0x7E,0x06,0x0C,0xF8,0x00,0x00},
    /* 0x7A 'z' */ {0x00,0x00,0x00,0x00,0x00,0xFE,0xCC,0x18,0x30,0x60,0xC6,0xFE,0x00,0x00,0x00,0x00},
    /* 0x7B '{' */ {0x00,0x00,0x0E,0x18,0x18,0x18,0x70,0x18,0x18,0x18,0x18,0x0E,0x00,0x00,0x00,0x00},
    /* 0x7C '|' */ {0x00,0x00,0x18,0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x18,0x18,0x00,0x00,0x00,0x00},
    /* 0x7D '}' */ {0x00,0x00,0x70,0x18,0x18,0x18,0x0E,0x18,0x18,0x18,0x18,0x70,0x00,0x00,0x00,0x00},
    /* 0x7E '~' */ {0x00,0x00,0x76,0xDC,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
};

/* ================================================================
 * Key name table
 * ================================================================ */

typedef struct { int code; const char *name; } KeyName;

static const KeyName key_names[] = {
    { KEY_ESC,          "esc" },
    { KEY_1,            "1" },
    { KEY_2,            "2" },
    { KEY_3,            "3" },
    { KEY_4,            "4" },
    { KEY_5,            "5" },
    { KEY_6,            "6" },
    { KEY_7,            "7" },
    { KEY_8,            "8" },
    { KEY_9,            "9" },
    { KEY_0,            "0" },
    { KEY_MINUS,        "minus" },
    { KEY_EQUAL,        "equal" },
    { KEY_BACKSPACE,    "backspace" },
    { KEY_TAB,          "tab" },
    { KEY_Q,            "q" },
    { KEY_W,            "w" },
    { KEY_E,            "e" },
    { KEY_R,            "r" },
    { KEY_T,            "t" },
    { KEY_Y,            "y" },
    { KEY_U,            "u" },
    { KEY_I,            "i" },
    { KEY_O,            "o" },
    { KEY_P,            "p" },
    { KEY_LEFTBRACE,    "bracketleft" },
    { KEY_RIGHTBRACE,   "bracketright" },
    { KEY_ENTER,        "enter" },
    { KEY_LEFTCTRL,     "lctrl" },
    { KEY_A,            "a" },
    { KEY_S,            "s" },
    { KEY_D,            "d" },
    { KEY_F,            "f" },
    { KEY_G,            "g" },
    { KEY_H,            "h" },
    { KEY_J,            "j" },
    { KEY_K,            "k" },
    { KEY_L,            "l" },
    { KEY_SEMICOLON,    "semicolon" },
    { KEY_APOSTROPHE,   "apostrophe" },
    { KEY_GRAVE,        "grave" },
    { KEY_LEFTSHIFT,    "lshift" },
    { KEY_BACKSLASH,    "backslash" },
    { KEY_Z,            "z" },
    { KEY_X,            "x" },
    { KEY_C,            "c" },
    { KEY_V,            "v" },
    { KEY_B,            "b" },
    { KEY_N,            "n" },
    { KEY_M,            "m" },
    { KEY_COMMA,        "comma" },
    { KEY_DOT,          "dot" },
    { KEY_SLASH,        "slash" },
    { KEY_RIGHTSHIFT,   "rshift" },
    { KEY_KPASTERISK,   "kpasterisk" },
    { KEY_LEFTALT,      "lalt" },
    { KEY_SPACE,        "space" },
    { KEY_CAPSLOCK,     "capslock" },
    { KEY_F1,           "f1" },
    { KEY_F2,           "f2" },
    { KEY_F3,           "f3" },
    { KEY_F4,           "f4" },
    { KEY_F5,           "f5" },
    { KEY_F6,           "f6" },
    { KEY_F7,           "f7" },
    { KEY_F8,           "f8" },
    { KEY_F9,           "f9" },
    { KEY_F10,          "f10" },
    { KEY_F11,          "f11" },
    { KEY_F12,          "f12" },
    { KEY_KP7,          "kp7" },
    { KEY_KP8,          "kp8" },
    { KEY_KP9,          "kp9" },
    { KEY_KPMINUS,      "kpminus" },
    { KEY_KP4,          "kp4" },
    { KEY_KP5,          "kp5" },
    { KEY_KP6,          "kp6" },
    { KEY_KPPLUS,       "kpplus" },
    { KEY_KP1,          "kp1" },
    { KEY_KP2,          "kp2" },
    { KEY_KP3,          "kp3" },
    { KEY_KP0,          "kp0" },
    { KEY_KPDOT,        "kpdot" },
    { KEY_KPENTER,      "kpenter" },
    { KEY_RIGHTCTRL,    "rctrl" },
    { KEY_RIGHTALT,     "ralt" },
    { KEY_HOME,         "home" },
    { KEY_UP,           "up" },
    { KEY_PAGEUP,       "pageup" },
    { KEY_LEFT,         "left" },
    { KEY_RIGHT,        "right" },
    { KEY_END,          "end" },
    { KEY_DOWN,         "down" },
    { KEY_PAGEDOWN,     "pagedown" },
    { KEY_INSERT,       "insert" },
    { KEY_DELETE,       "delete" },
};

#define NUM_KEY_NAMES ((int)(sizeof(key_names) / sizeof(key_names[0])))

static const char *keycode_to_name(int code)
{
    for (int i = 0; i < NUM_KEY_NAMES; i++)
        if (key_names[i].code == code)
            return key_names[i].name;
    return "?";
}

static int parse_keyname(const char *name)
{
    for (int i = 0; i < NUM_KEY_NAMES; i++)
        if (strcasecmp(key_names[i].name, name) == 0)
            return key_names[i].code;
    return -1;
}

/* ================================================================
 * Mapping data structure
 * ================================================================ */

typedef struct {
    const char *cli_name;    /* "--up", "--leftfire", etc. */
    const char *label;       /* "Up", "Left Fire", etc. */
    int         keycode;     /* current KEY_* code */
    int         default_key; /* default KEY_* code */
    int         btn_code;    /* BTN_* for buttons, -1 for directions */
    int         dx, dy;      /* direction contribution */
} Mapping;

static Mapping g_map[NUM_MAPPINGS];
static int g_dir_held[NUM_DIRECTIONS];

static void init_mappings(void)
{
    /* Directions (indices 0-7) */
    g_map[0]  = (Mapping){"--up",        "Up",         KEY_W,          KEY_W,          -1, 0, -1};
    g_map[1]  = (Mapping){"--down",      "Down",       KEY_X,          KEY_X,          -1, 0,  1};
    g_map[2]  = (Mapping){"--left",      "Left",       KEY_A,          KEY_A,          -1,-1,  0};
    g_map[3]  = (Mapping){"--right",     "Right",      KEY_D,          KEY_D,          -1, 1,  0};
    g_map[4]  = (Mapping){"--upleft",    "Up-Left",    KEY_Q,          KEY_Q,          -1,-1, -1};
    g_map[5]  = (Mapping){"--upright",   "Up-Right",   KEY_E,          KEY_E,          -1, 1, -1};
    g_map[6]  = (Mapping){"--downleft",  "Down-Left",  KEY_Z,          KEY_Z,          -1,-1,  1};
    g_map[7]  = (Mapping){"--downright", "Down-Right", KEY_C,          KEY_C,          -1, 1,  1};
    /* Buttons (indices 8-15) */
    g_map[8]  = (Mapping){"--leftfire",  "Left Fire",  KEY_SPACE,      KEY_SPACE,      BTN_TRIGGER, 0, 0};
    g_map[9]  = (Mapping){"--rightfire", "Right Fire",  KEY_LEFTALT,    KEY_LEFTALT,    BTN_THUMB,   0, 0};
    g_map[10] = (Mapping){"--lefttri",   "Left Tri",   KEY_LEFTBRACE,  KEY_LEFTBRACE,  BTN_THUMB2,     0, 0};
    g_map[11] = (Mapping){"--righttri",  "Right Tri",  KEY_RIGHTBRACE, KEY_RIGHTBRACE, BTN_TOP,  0, 0};
    g_map[12] = (Mapping){"--menu1",     "Menu 1",     KEY_7,          KEY_7,          BTN_TOP2,    0, 0};
    g_map[13] = (Mapping){"--menu2",     "Menu 2",     KEY_8,          KEY_8,          BTN_PINKIE,  0, 0};
    g_map[14] = (Mapping){"--menu3",     "Menu 3",     KEY_9,          KEY_9,          BTN_BASE,    0, 0};
    g_map[15] = (Mapping){"--menu4",     "Menu 4",     KEY_0,          KEY_0,          BTN_BASE2,   0, 0};

    memset(g_dir_held, 0, sizeof(g_dir_held));
}

/* ================================================================
 * Globals and forward declarations
 * ================================================================ */

static volatile sig_atomic_t g_quit = 0;
static int g_uinput_fd = -1;
static int g_kbd_fds[MAX_KEYBOARDS];
static int g_num_kbd_fds = 0;
static int g_kbd_grabbed[MAX_KEYBOARDS];
static int g_ctrl_held;
static int g_suspended;

static void emit_event(int fd, int type, int code, int value);
static void emit_syn(int fd);
static int guimap_run(void);

static void sig_handler(int sig)
{
    (void)sig;
    g_quit = 1;
}

/* ================================================================
 * Utility
 * ================================================================ */

static uint64_t time_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* ================================================================
 * Framebuffer
 * ================================================================ */

typedef struct {
    int       fd;
    uint32_t *pixels;
    uint32_t *backbuf;
    int       width;
    int       height;
    int       stride_px;
    size_t    size;
} Framebuffer;

static int fb_init(Framebuffer *fb)
{
    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;

    memset(fb, 0, sizeof(*fb));
    fb->fd = open("/dev/fb0", O_RDWR);
    if (fb->fd < 0) { perror("open /dev/fb0"); return -1; }

    if (ioctl(fb->fd, FBIOGET_VSCREENINFO, &vinfo) < 0) {
        perror("FBIOGET_VSCREENINFO"); close(fb->fd); return -1;
    }
    if (ioctl(fb->fd, FBIOGET_FSCREENINFO, &finfo) < 0) {
        perror("FBIOGET_FSCREENINFO"); close(fb->fd); return -1;
    }

    fb->width     = vinfo.xres;
    fb->height    = vinfo.yres;
    fb->stride_px = finfo.line_length / (vinfo.bits_per_pixel / 8);
    fb->size      = (size_t)finfo.line_length * vinfo.yres;

    /* Pan display to page 0 so we write to the visible buffer.
     * Needed after killing the64 which uses EGL double-buffering
     * and may leave yoffset pointing at a different page. */
    vinfo.yoffset = 0;
    vinfo.xoffset = 0;
    ioctl(fb->fd, FBIOPAN_DISPLAY, &vinfo);

    fb->pixels = mmap(NULL, fb->size, PROT_READ | PROT_WRITE, MAP_SHARED,
                       fb->fd, 0);
    if (fb->pixels == MAP_FAILED) {
        perror("mmap framebuffer"); close(fb->fd); return -1;
    }

    fb->backbuf = malloc(fb->size);
    if (!fb->backbuf) {
        munmap(fb->pixels, fb->size); close(fb->fd); return -1;
    }
    memset(fb->backbuf, 0, fb->size);
    return 0;
}

static void fb_flip(Framebuffer *fb)
{
    memcpy(fb->pixels, fb->backbuf, fb->size);
}

static void fb_clear(Framebuffer *fb, uint32_t color)
{
    int total = fb->stride_px * fb->height;
    for (int i = 0; i < total; i++)
        fb->backbuf[i] = color;
}

static void fb_destroy(Framebuffer *fb)
{
    if (fb->backbuf) free(fb->backbuf);
    if (fb->pixels && fb->pixels != MAP_FAILED)
        munmap(fb->pixels, fb->size);
    if (fb->fd >= 0) close(fb->fd);
}

/* ================================================================
 * Drawing primitives
 * ================================================================ */

static inline void draw_pixel(Framebuffer *fb, int x, int y, uint32_t c)
{
    if (x >= 0 && x < fb->width && y >= 0 && y < fb->height)
        fb->backbuf[y * fb->stride_px + x] = c;
}

static void draw_rect(Framebuffer *fb, int x, int y, int w, int h, uint32_t c)
{
    for (int row = y; row < y + h; row++)
        for (int col = x; col < x + w; col++)
            draw_pixel(fb, col, row, c);
}

static void draw_circle(Framebuffer *fb, int cx, int cy, int r, uint32_t c)
{
    for (int dy = -r; dy <= r; dy++) {
        int dx = 0;
        while (dx * dx + dy * dy <= r * r) dx++;
        draw_rect(fb, cx - dx + 1, cy + dy, 2 * dx - 1, 1, c);
    }
}

static void draw_rounded_rect(Framebuffer *fb, int x, int y, int w, int h,
                                int r, uint32_t c)
{
    if (r < 1) { draw_rect(fb, x, y, w, h, c); return; }
    draw_rect(fb, x + r, y, w - 2 * r, h, c);
    draw_rect(fb, x, y + r, r, h - 2 * r, c);
    draw_rect(fb, x + w - r, y + r, r, h - 2 * r, c);
    for (int dy = -r; dy <= 0; dy++) {
        int dx = 0;
        while (dx * dx + dy * dy <= r * r) dx++;
        draw_rect(fb, x + r - dx + 1, y + r + dy, dx - 1, 1, c);
        draw_rect(fb, x + w - r, y + r + dy, dx - 1, 1, c);
        draw_rect(fb, x + r - dx + 1, y + h - 1 - r - dy, dx - 1, 1, c);
        draw_rect(fb, x + w - r, y + h - 1 - r - dy, dx - 1, 1, c);
    }
}

static void draw_triangle_filled(Framebuffer *fb, int x0, int y0,
                                  int x1, int y1, int x2, int y2, uint32_t c)
{
    int tx, ty;
    if (y0 > y1) { tx=x0;ty=y0;x0=x1;y0=y1;x1=tx;y1=ty; }
    if (y0 > y2) { tx=x0;ty=y0;x0=x2;y0=y2;x2=tx;y2=ty; }
    if (y1 > y2) { tx=x1;ty=y1;x1=x2;y1=y2;x2=tx;y2=ty; }

    for (int y = y0; y <= y2; y++) {
        int xa, xb;
        if (y2 != y0) xa = x0 + (x2 - x0) * (y - y0) / (y2 - y0);
        else xa = x0;
        if (y < y1) {
            if (y1 != y0) xb = x0 + (x1 - x0) * (y - y0) / (y1 - y0);
            else xb = x0;
        } else {
            if (y2 != y1) xb = x1 + (x2 - x1) * (y - y1) / (y2 - y1);
            else xb = x1;
        }
        if (xa > xb) { tx = xa; xa = xb; xb = tx; }
        draw_rect(fb, xa, y, xb - xa + 1, 1, c);
    }
}

/* ================================================================
 * Text rendering (built-in 8x16 font)
 * ================================================================ */

static void draw_char(Framebuffer *fb, int x, int y, char ch, uint32_t c,
                       int scale)
{
    int idx = (unsigned char)ch - 0x20;
    if (idx < 0 || idx >= 95) return;
    const uint8_t *glyph = font8x16[idx];
    for (int row = 0; row < FONT_H; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < FONT_W; col++) {
            if (bits & (0x80 >> col)) {
                if (scale == 1)
                    draw_pixel(fb, x + col, y + row, c);
                else
                    draw_rect(fb, x + col * scale, y + row * scale,
                              scale, scale, c);
            }
        }
    }
}

static void draw_text(Framebuffer *fb, int x, int y, const char *text,
                       uint32_t c, int scale)
{
    while (*text) {
        draw_char(fb, x, y, *text, c, scale);
        x += FONT_W * scale;
        text++;
    }
}

static int text_width(const char *text, int scale)
{
    return (int)strlen(text) * FONT_W * scale;
}

static void draw_text_centered(Framebuffer *fb, int cx, int y, const char *text,
                                uint32_t c, int scale)
{
    draw_text(fb, cx - text_width(text, scale) / 2, y, text, c, scale);
}

/* ================================================================
 * Keyboard detection (adapted from gamepad_map.c)
 * ================================================================ */

static int is_keyboard(int fd)
{
    unsigned long evbits[NBITS(EV_MAX)];
    unsigned long keybits[NBITS(KEY_MAX)];

    memset(evbits, 0, sizeof(evbits));
    if (ioctl(fd, EVIOCGBIT(0, sizeof(evbits)), evbits) < 0)
        return 0;
    if (!TEST_BIT(EV_KEY, evbits))
        return 0;

    memset(keybits, 0, sizeof(keybits));
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keybits)), keybits) < 0)
        return 0;

    return TEST_BIT(KEY_Q, keybits) && TEST_BIT(KEY_A, keybits);
}

static int scan_keyboards(int *fds, int max_fds)
{
    DIR *dir;
    struct dirent *entry;
    char path[MAX_PATH_LEN];
    char name[MAX_NAME_LEN];
    int count = 0;

    dir = opendir("/dev/input");
    if (!dir) return 0;

    while ((entry = readdir(dir)) != NULL) {
        if (count >= max_fds) break;
        if (strlen(entry->d_name) <= 5) continue;
        if (strncmp(entry->d_name, "event", 5) != 0) continue;

        snprintf(path, sizeof(path), "/dev/input/%s", entry->d_name);
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;

        if (is_keyboard(fd)) {
            memset(name, 0, sizeof(name));
            if (ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name) < 0)
                strcpy(name, "Unknown");
            fprintf(stderr, "Found keyboard: %s (%s)\n", name, path);
            fds[count++] = fd;
        } else {
            close(fd);
        }
    }
    closedir(dir);
    return count;
}

static int read_keyboard_press(int *fds, int count)
{
    struct input_event ev;
    for (int i = 0; i < count; i++) {
        while (read(fds[i], &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
            if (ev.type == EV_KEY && ev.value == 1)
                return ev.code;
        }
    }
    return 0;
}

static void drain_keyboard_events(int *fds, int count)
{
    struct input_event ev;
    for (int i = 0; i < count; i++)
        while (read(fds[i], &ev, sizeof(ev)) == (ssize_t)sizeof(ev))
            ;
}

/* ================================================================
 * uinput virtual joystick
 * ================================================================ */

static int create_virtual_joystick(void)
{
    int fd;
    struct uinput_user_dev uidev;
    int axes[] = { ABS_X, ABS_Y, ABS_Z, ABS_RX, ABS_RY };
    int i;

    fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) {
        perror("open /dev/uinput");
        fprintf(stderr, "Hint: try 'modprobe uinput' or check permissions\n");
        return -1;
    }

    /* Enable event types */
    if (ioctl(fd, UI_SET_EVBIT, EV_KEY) < 0) goto fail;
    if (ioctl(fd, UI_SET_EVBIT, EV_ABS) < 0) goto fail;
    if (ioctl(fd, UI_SET_EVBIT, EV_SYN) < 0) goto fail;
    if (ioctl(fd, UI_SET_EVBIT, EV_MSC) < 0) goto fail;

    /* Enable 12 buttons: BTN_TRIGGER(288) through BTN_BASE6(299) */
    for (i = BTN_TRIGGER; i <= BTN_BASE6; i++)
        if (ioctl(fd, UI_SET_KEYBIT, i) < 0) goto fail;

    /* Enable 5 axes */
    for (i = 0; i < 5; i++)
        if (ioctl(fd, UI_SET_ABSBIT, axes[i]) < 0) goto fail;

    /* Enable MSC_SCAN */
    if (ioctl(fd, UI_SET_MSCBIT, MSC_SCAN) < 0) goto fail;

    /* Configure device */
    memset(&uidev, 0, sizeof(uidev));
    snprintf(uidev.name, UINPUT_MAX_NAME_SIZE, "%s", VDEV_NAME);
    uidev.id.bustype = VDEV_BUSTYPE;
    uidev.id.vendor  = VDEV_VENDOR;
    uidev.id.product = VDEV_PRODUCT;
    uidev.id.version = VDEV_VERSION;

    for (i = 0; i < 5; i++) {
        uidev.absmin[axes[i]]  = AXIS_MIN;
        uidev.absmax[axes[i]]  = AXIS_MAX;
        uidev.absflat[axes[i]] = AXIS_FLAT;
    }

    if (write(fd, &uidev, sizeof(uidev)) != sizeof(uidev)) {
        perror("write uinput_user_dev");
        goto fail;
    }

    if (ioctl(fd, UI_DEV_CREATE) < 0) {
        perror("UI_DEV_CREATE");
        goto fail;
    }

    /* Set initial axis positions to center */
    for (i = 0; i < 5; i++) {
        emit_event(fd, EV_ABS, axes[i], AXIS_CENTER);
    }
    emit_syn(fd);

    fprintf(stderr, "Virtual THEJOYSTICK created: %s\n", VDEV_NAME);
    return fd;

fail:
    perror("uinput setup");
    close(fd);
    return -1;
}

static void destroy_virtual_joystick(int fd)
{
    if (fd >= 0) {
        ioctl(fd, UI_DEV_DESTROY);
        close(fd);
    }
}

/* ================================================================
 * Event emission
 * ================================================================ */

static void emit_event(int fd, int type, int code, int value)
{
    struct input_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type  = type;
    ev.code  = code;
    ev.value = value;
    if (write(fd, &ev, sizeof(ev)) < 0)
        perror("emit_event write");
}

static void emit_syn(int fd)
{
    emit_event(fd, EV_SYN, SYN_REPORT, 0);
}

static void recalc_and_emit_axes(void)
{
    int sx = 0, sy = 0;
    int ax, ay;

    for (int d = 0; d < NUM_DIRECTIONS; d++) {
        if (g_dir_held[d]) {
            sx += g_map[d].dx;
            sy += g_map[d].dy;
        }
    }

    if (sx < -1) sx = -1;
    if (sx >  1) sx =  1;
    if (sy < -1) sy = -1;
    if (sy >  1) sy =  1;

    ax = (sx < 0) ? 0 : (sx > 0) ? 255 : 127;
    ay = (sy < 0) ? 0 : (sy > 0) ? 255 : 127;

    emit_event(g_uinput_fd, EV_ABS, ABS_X, ax);
    emit_event(g_uinput_fd, EV_ABS, ABS_Y, ay);
    emit_syn(g_uinput_fd);
}

/* ================================================================
 * Keyboard grab / ungrab
 * ================================================================ */

static void grab_keyboards(void)
{
    for (int i = 0; i < g_num_kbd_fds; i++) {
        if (ioctl(g_kbd_fds[i], EVIOCGRAB, 1) == 0) {
            g_kbd_grabbed[i] = 1;
            fprintf(stderr, "Grabbed keyboard fd %d\n", g_kbd_fds[i]);
        } else {
            g_kbd_grabbed[i] = 0;
            fprintf(stderr, "Warning: failed to grab keyboard fd %d\n",
                    g_kbd_fds[i]);
        }
    }
}

static void ungrab_keyboards(void)
{
    for (int i = 0; i < g_num_kbd_fds; i++) {
        if (g_kbd_grabbed[i]) {
            ioctl(g_kbd_fds[i], EVIOCGRAB, 0);
            g_kbd_grabbed[i] = 0;
        }
    }
}

/* ================================================================
 * Suspend translation (for live remap via Ctrl+R)
 * ================================================================ */

static void suspend_translation(void)
{
    for (int b = NUM_DIRECTIONS; b < NUM_MAPPINGS; b++)
        emit_event(g_uinput_fd, EV_KEY, g_map[b].btn_code, 0);
    emit_event(g_uinput_fd, EV_ABS, ABS_X, AXIS_CENTER);
    emit_event(g_uinput_fd, EV_ABS, ABS_Y, AXIS_CENTER);
    emit_syn(g_uinput_fd);
    ungrab_keyboards();
    for (int i = 0; i < g_num_kbd_fds; i++)
        close(g_kbd_fds[i]);
    g_num_kbd_fds = 0;
    memset(g_dir_held, 0, sizeof(g_dir_held));
    g_ctrl_held = 0;
    g_suspended = 0;
}

/* ================================================================
 * Cleanup (atexit safety net)
 * ================================================================ */

static void cleanup(void)
{
    /* Release all held buttons */
    if (g_uinput_fd >= 0) {
        for (int b = NUM_DIRECTIONS; b < NUM_MAPPINGS; b++)
            emit_event(g_uinput_fd, EV_KEY, g_map[b].btn_code, 0);

        /* Center axes */
        emit_event(g_uinput_fd, EV_ABS, ABS_X, AXIS_CENTER);
        emit_event(g_uinput_fd, EV_ABS, ABS_Y, AXIS_CENTER);
        emit_syn(g_uinput_fd);

        destroy_virtual_joystick(g_uinput_fd);
        g_uinput_fd = -1;
    }

    ungrab_keyboards();

    for (int i = 0; i < g_num_kbd_fds; i++)
        close(g_kbd_fds[i]);
    g_num_kbd_fds = 0;
}

/* ================================================================
 * CLI parsing and help
 * ================================================================ */

static void print_usage(void)
{
    printf("keyboard2thejoystick - USB Keyboard to Virtual THEJOYSTICK\n\n");
    printf("Creates a virtual THEJOYSTICK via Linux uinput, translating keyboard\n");
    printf("input to joystick events for THEC64 Mini/Maxi.\n\n");
    printf("Usage: keyboard2thejoystick [OPTIONS]\n\n");

    printf("Direction keys:\n");
    printf("  --up KEY         (current: %-14s)  --upleft KEY    (current: %s)\n",
           keycode_to_name(g_map[0].keycode), keycode_to_name(g_map[4].keycode));
    printf("  --down KEY       (current: %-14s)  --upright KEY   (current: %s)\n",
           keycode_to_name(g_map[1].keycode), keycode_to_name(g_map[5].keycode));
    printf("  --left KEY       (current: %-14s)  --downleft KEY  (current: %s)\n",
           keycode_to_name(g_map[2].keycode), keycode_to_name(g_map[6].keycode));
    printf("  --right KEY      (current: %-14s)  --downright KEY (current: %s)\n",
           keycode_to_name(g_map[3].keycode), keycode_to_name(g_map[7].keycode));
    printf("\n");

    printf("Button keys:\n");
    printf("  --leftfire KEY   (current: %-14s)  --rightfire KEY (current: %s)\n",
           keycode_to_name(g_map[8].keycode), keycode_to_name(g_map[9].keycode));
    printf("  --lefttri KEY    (current: %-14s)  --righttri KEY  (current: %s)\n",
           keycode_to_name(g_map[10].keycode), keycode_to_name(g_map[11].keycode));
    printf("  --menu1 KEY      (current: %-14s)  --menu2 KEY     (current: %s)\n",
           keycode_to_name(g_map[12].keycode), keycode_to_name(g_map[13].keycode));
    printf("  --menu3 KEY      (current: %-14s)  --menu4 KEY     (current: %s)\n",
           keycode_to_name(g_map[14].keycode), keycode_to_name(g_map[15].keycode));
    printf("\n");

    printf("Other:\n");
    printf("  --help           Show this help with current configuration\n");
    printf("  --guimap         Interactive framebuffer mapping mode\n");
    printf("\n");

    printf("Key names: single chars (a, 7), or names (space, lalt, lctrl,\n");
    printf("  lshift, rshift, tab, enter, esc, bracketleft, bracketright,\n");
    printf("  f1-f12, up, down, left, right, etc.)\n");
    printf("\n");

    printf("Direction layout (QWEASDZXC):\n");
    printf("  Q=Up-Left    W=Up      E=Up-Right\n");
    printf("  A=Left       (S=n/a)   D=Right\n");
    printf("  Z=Down-Left  X=Down    C=Down-Right\n");
}

static int parse_args(int argc, char **argv, int *help, int *guimap)
{
    *help = 0;
    *guimap = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            *help = 1;
            continue;
        }
        if (strcmp(argv[i], "--guimap") == 0) {
            *guimap = 1;
            continue;
        }

        int found = 0;
        for (int m = 0; m < NUM_MAPPINGS; m++) {
            if (strcmp(argv[i], g_map[m].cli_name) == 0) {
                if (i + 1 >= argc) {
                    fprintf(stderr, "Error: %s requires a key name\n", argv[i]);
                    return -1;
                }
                i++;
                int kc = parse_keyname(argv[i]);
                if (kc < 0) {
                    fprintf(stderr, "Error: unknown key name '%s'\n", argv[i]);
                    fprintf(stderr, "Run with --help for a list of key names\n");
                    return -1;
                }
                g_map[m].keycode = kc;
                found = 1;
                break;
            }
        }
        if (!found) {
            fprintf(stderr, "Error: unknown option '%s'\n", argv[i]);
            fprintf(stderr, "Run with --help for usage information\n");
            return -1;
        }
    }
    return 0;
}

/* ================================================================
 * Normal mode: main event loop
 * ================================================================ */

static int normal_run(void)
{
    struct input_event ev;

    /* Scan for keyboards */
    g_num_kbd_fds = scan_keyboards(g_kbd_fds, MAX_KEYBOARDS);
    if (g_num_kbd_fds == 0) {
        fprintf(stderr, "Error: no USB keyboards found\n");
        return 1;
    }
    fprintf(stderr, "Found %d keyboard(s)\n", g_num_kbd_fds);

    /* Create virtual joystick */
    g_uinput_fd = create_virtual_joystick();
    if (g_uinput_fd < 0)
        return 1;

    /* Allow time for device to be recognized */
    usleep(500000);

    /* Register cleanup */
    atexit(cleanup);
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    /* Grab keyboards */
    grab_keyboards();
    drain_keyboard_events(g_kbd_fds, g_num_kbd_fds);

    /* Print active configuration */
    fprintf(stderr, "\nActive key mappings:\n");
    for (int i = 0; i < NUM_MAPPINGS; i++) {
        fprintf(stderr, "  %-12s = %s\n",
                g_map[i].label, keycode_to_name(g_map[i].keycode));
    }
    fprintf(stderr, "\nTranslating keyboard input to THEJOYSTICK events...\n");
    fprintf(stderr, "Press Ctrl+S to pause/resume.\n");
    fprintf(stderr, "Press Ctrl+R to remap.\n");
    fprintf(stderr, "Press Ctrl+C to stop.\n\n");

    /* Outer loop: translate → remap → translate … */
    for (;;) {
        int remap_requested = 0;

        /* Inner translation loop */
        while (!g_quit) {
            int axis_dirty = 0;

            for (int k = 0; k < g_num_kbd_fds; k++) {
                while (read(g_kbd_fds[k], &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
                    if (ev.type != EV_KEY) continue;
                    if (ev.value == 2) continue;  /* skip autorepeat */

                    int pressed = (ev.value == 1);

                    /* Track Ctrl key state */
                    if (ev.code == KEY_LEFTCTRL || ev.code == KEY_RIGHTCTRL) {
                        g_ctrl_held = pressed;
                        continue;
                    }

                    /* Ctrl+S → toggle suspend/resume */
                    if (ev.code == KEY_S && pressed && g_ctrl_held) {
                        if (!g_suspended) {
                            for (int b = NUM_DIRECTIONS; b < NUM_MAPPINGS; b++)
                                emit_event(g_uinput_fd, EV_KEY, g_map[b].btn_code, 0);
                            emit_event(g_uinput_fd, EV_ABS, ABS_X, AXIS_CENTER);
                            emit_event(g_uinput_fd, EV_ABS, ABS_Y, AXIS_CENTER);
                            emit_syn(g_uinput_fd);
                            memset(g_dir_held, 0, sizeof(g_dir_held));
                            ungrab_keyboards();
                            g_suspended = 1;
                            fprintf(stderr, "\nJoystick emulation paused (Ctrl+S to resume)\n");
                        } else {
                            grab_keyboards();
                            drain_keyboard_events(g_kbd_fds, g_num_kbd_fds);
                            g_suspended = 0;
                            g_ctrl_held = 0;
                            fprintf(stderr, "\nJoystick emulation resumed (Ctrl+S to pause)\n");
                        }
                        continue;
                    }

                    /* Ctrl+R → request remap */
                    if (ev.code == KEY_R && pressed && g_ctrl_held) {
                        g_suspended = 0;
                        remap_requested = 1;
                        goto break_inner;
                    }

                    if (g_suspended) continue;

                    /* Check direction mappings */
                    for (int d = 0; d < NUM_DIRECTIONS; d++) {
                        if (ev.code == g_map[d].keycode) {
                            if (g_dir_held[d] != pressed) {
                                g_dir_held[d] = pressed;
                                axis_dirty = 1;
                            }
                        }
                    }

                    /* Check button mappings */
                    for (int b = NUM_DIRECTIONS; b < NUM_MAPPINGS; b++) {
                        if (ev.code == g_map[b].keycode) {
                            emit_event(g_uinput_fd, EV_MSC, MSC_SCAN,
                                       0x90001 + (g_map[b].btn_code - BTN_TRIGGER));
                            emit_event(g_uinput_fd, EV_KEY,
                                       g_map[b].btn_code, pressed);
                            emit_syn(g_uinput_fd);
                        }
                    }
                }
            }

            if (axis_dirty)
                recalc_and_emit_axes();

            usleep(1000);
        }
break_inner:

        if (g_quit) break;

        /* Ctrl+R was pressed — enter remap session */
        fprintf(stderr, "\nCtrl+R pressed, entering remap mode...\n");
        suspend_translation();
        system("killall -9 the64");
        system("killall -9 the64");

        /* Save current mappings so we can restore on quit-without-apply */
        Mapping saved_map[NUM_MAPPINGS];
        memcpy(saved_map, g_map, sizeof(g_map));

        int remap_result = guimap_run();
        if (remap_result != 0)
            memcpy(g_map, saved_map, sizeof(g_map));

        system("the64 &");
        //usleep(500000);

        /* Print updated configuration */
        fprintf(stderr, "\nUpdated key mappings:\n");
        for (int i = 0; i < NUM_MAPPINGS; i++) {
            fprintf(stderr, "  %-12s = %s\n",
                    g_map[i].label, keycode_to_name(g_map[i].keycode));
        }

        /* Re-scan and re-grab keyboards */
        g_num_kbd_fds = scan_keyboards(g_kbd_fds, MAX_KEYBOARDS);
        grab_keyboards();
        drain_keyboard_events(g_kbd_fds, g_num_kbd_fds);

        fprintf(stderr, "\nResuming translation...\n");
        fprintf(stderr, "Press Ctrl+S to pause/resume.\n");
        fprintf(stderr, "Press Ctrl+R to remap.\n");
        fprintf(stderr, "Press Ctrl+C to stop.\n\n");

        (void)remap_requested;
    }

    fprintf(stderr, "\nShutting down...\n");
    /* cleanup() called via atexit */
    return 0;
}

/* ================================================================
 * Directory browser (for guimap mode, from gamepad_map.c)
 * ================================================================ */

typedef struct {
    char name[MAX_NAME_LEN];
    int  is_dir;
} DirEntry;

typedef struct {
    char     path[MAX_PATH_LEN];
    DirEntry entries[MAX_DIR_ENTRIES];
    int      count;
    int      selected;
    int      scroll;
} DirBrowser;

static int dir_entry_cmp(const void *a, const void *b)
{
    const DirEntry *da = a, *db = b;
    if (da->is_dir != db->is_dir)
        return db->is_dir - da->is_dir;
    return strcasecmp(da->name, db->name);
}

static void browser_load(DirBrowser *b, const char *path)
{
    DIR *dir;
    struct dirent *entry;
    struct stat st;
    char fullpath[MAX_PATH_LEN];

    strncpy(b->path, path, MAX_PATH_LEN - 1);
    b->path[MAX_PATH_LEN - 1] = '\0';
    b->count = 0;
    b->selected = 0;
    b->scroll = 0;

    if (strcmp(b->path, "/") != 0) {
        strcpy(b->entries[0].name, "..");
        b->entries[0].is_dir = 1;
        b->count = 1;
    }

    dir = opendir(path);
    if (!dir) return;

    while ((entry = readdir(dir)) != NULL && b->count < MAX_DIR_ENTRIES) {
        if (entry->d_name[0] == '.') continue;
        snprintf(fullpath, sizeof(fullpath), "%s/%s", path, entry->d_name);
        if (stat(fullpath, &st) < 0) continue;
        if (!S_ISDIR(st.st_mode)) continue;

        snprintf(b->entries[b->count].name,
                 sizeof(b->entries[b->count].name), "%s", entry->d_name);
        b->entries[b->count].is_dir = 1;
        b->count++;
    }
    closedir(dir);

    int start = (b->count > 0 && strcmp(b->entries[0].name, "..") == 0) ? 1 : 0;
    if (b->count - start > 1)
        qsort(&b->entries[start], b->count - start, sizeof(DirEntry),
              dir_entry_cmp);

    if (b->count < MAX_DIR_ENTRIES) {
        snprintf(b->entries[b->count].name,
                 sizeof(b->entries[b->count].name), ">> Export here <<");
        b->entries[b->count].is_dir = 0;
        b->count++;
    }
}

/* ================================================================
 * Draw THEJOYSTICK graphic (adapted from gamepad_map.c)
 * ================================================================ */

#define JOY_W  600
#define JOY_H  300

static void draw_joystick_guimap(Framebuffer *fb, int ox, int oy,
                                  int highlight_idx, int blink)
{
    /* Body */
    draw_rounded_rect(fb, ox + 33, oy + 53, 540, 180, 20, COL_BODY_DARK);
    draw_rounded_rect(fb, ox + 30, oy + 50, 540, 180, 20, COL_BODY);

    /* Left fire button (index 8) */
    {
        uint32_t c = (highlight_idx == 8 && blink) ? COL_HIGHLIGHT : COL_BTN_FIRE;
        if (highlight_idx == 8) c = blink ? COL_HIGHLIGHT : COL_BTN_FIRE;
        draw_rounded_rect(fb, ox + 38, oy + 100, 108, 40, 10, c);
        draw_text_centered(fb, ox + 92, oy + 108, "L.Fire", COL_TEXT, 1);
    }

    /* Right fire button (index 9) */
    {
        uint32_t c = (highlight_idx == 9 && blink) ? COL_HIGHLIGHT : COL_BTN_FIRE;
        draw_rounded_rect(fb, ox + 454, oy + 100, 108, 40, 10, c);
        draw_text_centered(fb, ox + 508, oy + 108, "R.Fire", COL_TEXT, 1);
    }

    /* Stick base */
    draw_circle(fb, ox + 220, oy + 135, 50, COL_STICK_BASE);
    /* Stick shaft */
    draw_rect(fb, ox + 213, oy + 60, 14, 75, COL_STICK);

    /* Stick ball - highlight for any direction mapping (0-7) */
    {
        uint32_t sc = COL_STICK_TOP;
        if (highlight_idx >= 0 && highlight_idx < 8 && blink)
            sc = COL_HIGHLIGHT;
        draw_circle(fb, ox + 220, oy + 55, 22, sc);
    }

    /* Direction indicators around stick */
    if (highlight_idx >= 0 && highlight_idx < 8) {
        /* Draw direction label at appropriate position */
        static const int dir_ox[] = { 0, 0, -80, 80, -60, 60, -60, 60 };
        static const int dir_oy[] = { -40, 80, 20, 20, -30, -30, 60, 60 };
        static const char *dir_lbl[] = {
            "UP", "DOWN", "LEFT", "RIGHT",
            "U-L", "U-R", "D-L", "D-R"
        };
        int lx = ox + 220 + dir_ox[highlight_idx];
        int ly = oy + 55 + dir_oy[highlight_idx];
        draw_text_centered(fb, lx, ly, dir_lbl[highlight_idx],
                            blink ? COL_HIGHLIGHT : COL_TEXT_TITLE, 1);
    }

    /* Left triangle button (index 10) */
    {
        uint32_t tc = (highlight_idx == 10 && blink) ? COL_HIGHLIGHT : COL_BTN;
        int cx = ox + 290, cy = oy + 205;
        draw_triangle_filled(fb, cx, cy - 16, cx - 14, cy + 10,
                              cx + 14, cy + 10, tc);
        draw_text_centered(fb, cx, cy + 16, "L.Tri", COL_TEXT, 1);
    }

    /* Right triangle button (index 11) */
    {
        uint32_t tc = (highlight_idx == 11 && blink) ? COL_HIGHLIGHT : COL_BTN;
        int cx = ox + 365, cy = oy + 205;
        draw_triangle_filled(fb, cx, cy - 16, cx - 14, cy + 10,
                              cx + 14, cy + 10, tc);
        draw_text_centered(fb, cx, cy + 16, "R.Tri", COL_TEXT, 1);
    }

    /* Menu buttons 1-4 (indices 12-15) */
    {
        int mw = 50, mh = 22, gap = 10;
        int total = 4 * mw + 3 * gap;
        int sx = ox + (JOY_W - total) / 2;
        int sy = oy + 248;
        const char *labels[] = {"M1", "M2", "M3", "M4"};
        for (int i = 0; i < 4; i++) {
            int mx = sx + i * (mw + gap);
            uint32_t mc = (highlight_idx == 12 + i && blink)
                          ? COL_HIGHLIGHT : COL_BTN;
            draw_rounded_rect(fb, mx, sy, mw, mh, 6, mc);
            draw_text_centered(fb, mx + mw / 2, sy + 3, labels[i],
                                COL_TEXT, 1);
        }
    }

    draw_text_centered(fb, ox + 220, oy + 190, "Stick", COL_TEXT_DIM, 1);
}

/* ================================================================
 * Guimap mode
 * ================================================================ */

#define GUIMAP_MAP      0
#define GUIMAP_REVIEW   1
#define GUIMAP_BROWSE   2

/* Review actions after the 16 mapping rows */
#define GUIMAP_REVIEW_APPLY   NUM_MAPPINGS       /* index 16 */
#define GUIMAP_REVIEW_QUIT    (NUM_MAPPINGS + 1) /* index 17 */
#define GUIMAP_REVIEW_SAVE    (NUM_MAPPINGS + 2) /* index 18 */
#define GUIMAP_REVIEW_TOTAL   (NUM_MAPPINGS + 3) /* 19 items */

typedef struct {
    Framebuffer fb;
    int         state;
    int         cur_map;
    int         redo_single;
    int         review_sel;
    int         blink;
    uint64_t    blink_time;
    DirBrowser  browser;
    char        save_path[MAX_PATH_LEN];
    int         kbd_fds[MAX_KEYBOARDS];
    int         num_kbd_fds;
    int         mapped[NUM_MAPPINGS]; /* 1 if this mapping has been set */
    int         applied;
    int         joy_fd;       /* real joystick fd, or -1 */
    int         joy_prev_y;   /* previous Y axis state: -1/0/1 */
} GuimapApp;

/* ================================================================
 * Joystick scanning / navigation helpers (for guimap review/browse)
 * ================================================================ */

static int scan_joystick(void)
{
    DIR *dir;
    struct dirent *entry;
    char path[MAX_PATH_LEN];
    char name[MAX_NAME_LEN];

    dir = opendir("/dev/input");
    if (!dir) return -1;

    while ((entry = readdir(dir)) != NULL) {
        if (strlen(entry->d_name) <= 5) continue;
        if (strncmp(entry->d_name, "event", 5) != 0) continue;

        snprintf(path, sizeof(path), "/dev/input/%s", entry->d_name);
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;

        unsigned long evbits[NBITS(EV_MAX)];
        unsigned long absbits[NBITS(ABS_MAX)];
        unsigned long keybits[NBITS(KEY_MAX)];
        memset(evbits, 0, sizeof(evbits));
        if (ioctl(fd, EVIOCGBIT(0, sizeof(evbits)), evbits) < 0) {
            close(fd); continue;
        }
        if (!TEST_BIT(EV_ABS, evbits) || !TEST_BIT(EV_KEY, evbits)) {
            close(fd); continue;
        }
        memset(absbits, 0, sizeof(absbits));
        ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(absbits)), absbits);
        if (!TEST_BIT(ABS_X, absbits) || !TEST_BIT(ABS_Y, absbits)) {
            close(fd); continue;
        }
        memset(keybits, 0, sizeof(keybits));
        ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keybits)), keybits);
        if (!TEST_BIT(BTN_TRIGGER, keybits)) {
            close(fd); continue;
        }

        /* Skip our own virtual joystick */
        memset(name, 0, sizeof(name));
        ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name);
        if (strcmp(name, VDEV_NAME) == 0) {
            close(fd); continue;
        }

        closedir(dir);
        fprintf(stderr, "Found joystick for nav: %s (%s)\n", name, path);
        return fd;
    }
    closedir(dir);
    return -1;
}

static void read_joystick_nav(int joy_fd, int *prev_y,
                               int *nav_dy, int *nav_confirm)
{
    struct input_event ev;
    *nav_dy = 0;
    *nav_confirm = 0;

    while (read(joy_fd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
        if (ev.type == EV_ABS && ev.code == ABS_Y) {
            int delta = ev.value - AXIS_CENTER;
            int cur = 0;
            if (delta < -50) cur = -1;
            else if (delta > 50) cur = 1;
            if (cur != *prev_y) {
                *nav_dy = cur;
                *prev_y = cur;
            }
        }
        else if (ev.type == EV_KEY && ev.code == BTN_TRIGGER && ev.value == 1) {
            *nav_confirm = 1;
        }
    }
}

static void guimap_save_script(GuimapApp *gapp)
{
    char filepath[MAX_PATH_LEN];
    DirBrowser *b = &gapp->browser;
    FILE *fp;

    if (strcmp(b->path, "/") == 0)
        snprintf(filepath, sizeof(filepath), "/keyboard2thejoystick.sh");
    else
        snprintf(filepath, sizeof(filepath), "%.470s/keyboard2thejoystick.sh",
                 b->path);

    fp = fopen(filepath, "w");
    if (!fp) {
        perror("fopen save");
        return;
    }

    fprintf(fp, "#!/bin/sh\nexec ./keyboard2thejoystick");
    for (int i = 0; i < NUM_MAPPINGS; i++) {
        const char *kn = keycode_to_name(g_map[i].keycode);
        /* Put continuation backslash before each option */
        fprintf(fp, " \\\n  %s %s", g_map[i].cli_name, kn);
    }
    fprintf(fp, "\n");
    fclose(fp);
    chmod(filepath, 0755);

    snprintf(gapp->save_path, sizeof(gapp->save_path), "%s", filepath);
}

static void guimap_render_map(GuimapApp *gapp)
{
    Framebuffer *fb = &gapp->fb;
    int cx = fb->width / 2;
    char buf[256];

    /* Header */
    draw_rect(fb, 0, 0, fb->width, 36, COL_HEADER_BG);
    snprintf(buf, sizeof(buf), "Keyboard Mapping (%d/%d)",
             gapp->cur_map + 1, NUM_MAPPINGS);
    draw_text(fb, 16, 10, buf, COL_TEXT_TITLE, 1);

    /* Joystick graphic */
    int jx = cx - JOY_W / 2;
    int jy = 50;
    draw_joystick_guimap(fb, jx, jy, gapp->cur_map, gapp->blink);

    /* Prompt */
    int py = jy + JOY_H + 20;
    snprintf(buf, sizeof(buf), ">>> Press key for: %s <<<",
             g_map[gapp->cur_map].label);
    draw_text_centered(fb, cx, py, buf,
                        gapp->blink ? COL_HIGHLIGHT : COL_TEXT, 2);

    /* Already mapped summary */
    int sy = py + 50;
    draw_text(fb, 100, sy, "Mapped so far:", COL_TEXT_DIM, 1);
    sy += 20;
    for (int i = 0; i < gapp->cur_map; i++) {
        if (!gapp->mapped[i]) continue;
        snprintf(buf, sizeof(buf), "  %-12s = %s",
                 g_map[i].label, keycode_to_name(g_map[i].keycode));
        draw_text(fb, 100, sy, buf, COL_MAPPED, 1);
        sy += 18;
    }
}

static void guimap_render_review(GuimapApp *gapp)
{
    Framebuffer *fb = &gapp->fb;
    char buf[256];

    /* Header */
    draw_rect(fb, 0, 0, fb->width, 36, COL_HEADER_BG);
    draw_text(fb, 16, 10, "Review Key Mappings", COL_TEXT_TITLE, 1);

    int y = 50;

    /* Scan for duplicate keycodes */
    int has_dupes = 0;
    for (int i = 0; i < NUM_MAPPINGS && !has_dupes; i++)
        for (int j = i + 1; j < NUM_MAPPINGS; j++)
            if (g_map[j].keycode == g_map[i].keycode) { has_dupes = 1; break; }

    /* Column headers */
    draw_text(fb, 60, y, "Action", COL_TEXT_DIM, 1);
    draw_text(fb, 260, y, "Key", COL_TEXT_DIM, 1);
    draw_text(fb, 460, y, "Joystick Output", COL_TEXT_DIM, 1);
    if (has_dupes)
        draw_text(fb, 660, y, "Duplicate", COL_ERROR, 1);

    y += 24;
    draw_rect(fb, 50, y, fb->width - 100, 1, COL_BORDER);
    y += 8;

    for (int i = 0; i < NUM_MAPPINGS; i++) {
        int hl = (i == gapp->review_sel);
        if (hl)
            draw_rect(fb, 50, y - 2, fb->width - 100, 22, COL_SELECTED);

        draw_text(fb, 60, y, g_map[i].label,
                  hl ? COL_TEXT_TITLE : COL_TEXT, 1);
        draw_text(fb, 260, y, keycode_to_name(g_map[i].keycode),
                  hl ? COL_TEXT_TITLE : COL_MAPPED, 1);

        if (i < NUM_DIRECTIONS)
            snprintf(buf, sizeof(buf), "Stick %s", g_map[i].label);
        else
            snprintf(buf, sizeof(buf), "BTN_%d", g_map[i].btn_code);
        draw_text(fb, 460, y, buf, COL_TEXT_DIM, 1);

        if (has_dupes) {
            char dups[256] = "";
            for (int j = 0; j < NUM_MAPPINGS; j++) {
                if (j == i) continue;
                if (g_map[j].keycode == g_map[i].keycode) {
                    if (dups[0]) strncat(dups, ", ", sizeof(dups) - strlen(dups) - 1);
                    strncat(dups, g_map[j].label, sizeof(dups) - strlen(dups) - 1);
                }
            }
            if (dups[0]) draw_text(fb, 660, y, dups, COL_ERROR, 1);
        }

        y += 22;
    }

    /* Action buttons */
    y += 8;
    draw_rect(fb, 50, y, fb->width - 100, 1, COL_BORDER);
    y += 8;

    {
        struct { int idx; const char *label; const char *key; uint32_t col; }
        actions[] = {
            { GUIMAP_REVIEW_APPLY, "Apply",                  "A", COL_SUCCESS },
            { GUIMAP_REVIEW_QUIT,  "Quit without Applying",  "Q", COL_ERROR },
            { GUIMAP_REVIEW_SAVE,  "Save to File",           "S", COL_HIGHLIGHT },
        };
        for (int i = 0; i < 3; i++) {
            int hl = (gapp->review_sel == actions[i].idx);
            if (hl)
                draw_rect(fb, 50, y - 2, fb->width - 100, 22, COL_SELECTED);
            snprintf(buf, sizeof(buf), "[%s] %s",
                     actions[i].key, actions[i].label);
            draw_text(fb, 70, y, buf,
                      hl ? COL_TEXT_TITLE : actions[i].col, 1);
            y += 24;
        }
    }

    /* Help */
    y += 4;
    draw_rect(fb, 50, y, fb->width - 100, 1, COL_BORDER);
    y += 8;
    draw_text(fb, 60, y,
              "Arrows=Navigate  Enter=Select  1=Redo sel  "
              "A=Apply  S=Save  Q=Quit",
              COL_TEXT_DIM, 1);

    if (gapp->save_path[0] != '\0') {
        y += 20;
        snprintf(buf, sizeof(buf), "Saved to: %.200s", gapp->save_path);
        draw_text(fb, 60, y, buf, COL_SUCCESS, 1);
    }
}

static void guimap_render_browse(GuimapApp *gapp)
{
    Framebuffer *fb = &gapp->fb;
    DirBrowser *b = &gapp->browser;
    char buf[512];

    draw_rect(fb, 0, 0, fb->width, 36, COL_HEADER_BG);
    draw_text(fb, 16, 10, "Select Export Directory", COL_TEXT_TITLE, 1);

    int y = 50;
    snprintf(buf, sizeof(buf), "Current: %s/", b->path);
    draw_text(fb, 60, y, buf, COL_TEXT, 1);

    y += 30;
    draw_rect(fb, 50, y, fb->width - 100, 1, COL_BORDER);
    y += 8;

    int visible = 18;
    for (int i = b->scroll; i < b->count && i < b->scroll + visible; i++) {
        int hl = (i == b->selected);
        if (hl)
            draw_rect(fb, 50, y - 2, fb->width - 100, 22, COL_SELECTED);

        if (b->entries[i].is_dir) {
            snprintf(buf, sizeof(buf), "[%s]", b->entries[i].name);
            draw_text(fb, 70, y, buf,
                      hl ? COL_TEXT_TITLE : COL_TEXT, 1);
        } else {
            draw_text(fb, 70, y, b->entries[i].name,
                      hl ? COL_TEXT_TITLE : COL_SUCCESS, 1);
        }
        y += 24;
    }

    int hy = fb->height - 60;
    draw_rect(fb, 50, hy, fb->width - 100, 1, COL_BORDER);
    hy += 12;
    draw_text(fb, 60, hy,
              "Arrows=Navigate  Enter=Select  Left/Bksp=Go up  Q/Esc=Cancel",
              COL_TEXT_DIM, 1);

    hy += 20;
    snprintf(buf, sizeof(buf),
             "File: %s/keyboard2thejoystick.sh", b->path);
    draw_text(fb, 60, hy, buf, COL_TEXT_DIM, 1);
}

static int guimap_run(void)
{
    GuimapApp gapp;
    memset(&gapp, 0, sizeof(gapp));

    if (fb_init(&gapp.fb) < 0) {
        fprintf(stderr, "Failed to initialize framebuffer\n");
        return 1;
    }

    gapp.num_kbd_fds = scan_keyboards(gapp.kbd_fds, MAX_KEYBOARDS);
    if (gapp.num_kbd_fds == 0) {
        fprintf(stderr, "Error: no USB keyboards found\n");
        fb_destroy(&gapp.fb);
        return 1;
    }

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    gapp.state = GUIMAP_MAP;
    gapp.cur_map = 0;
    gapp.redo_single = -1;
    gapp.review_sel = 0;
    gapp.blink_time = time_ms();
    gapp.joy_fd = scan_joystick();
    gapp.joy_prev_y = 0;

    /* Main loop */
    while (!g_quit) {
        uint64_t now = time_ms();

        if (now - gapp.blink_time > BLINK_MS) {
            gapp.blink = !gapp.blink;
            gapp.blink_time = now;
        }

        /* Update logic */
        if (gapp.state == GUIMAP_MAP) {
            int key = read_keyboard_press(gapp.kbd_fds, gapp.num_kbd_fds);
            if (key > 0) {
                g_map[gapp.cur_map].keycode = key;
                gapp.mapped[gapp.cur_map] = 1;

                drain_keyboard_events(gapp.kbd_fds, gapp.num_kbd_fds);
                usleep(DEBOUNCE_MS * 1000);
                drain_keyboard_events(gapp.kbd_fds, gapp.num_kbd_fds);

                if (gapp.redo_single >= 0) {
                    gapp.redo_single = -1;
                    gapp.state = GUIMAP_REVIEW;
                } else {
                    gapp.cur_map++;
                    if (gapp.cur_map >= NUM_MAPPINGS) {
                        gapp.state = GUIMAP_REVIEW;
                        gapp.review_sel = 0;
                    }
                }
            }
        }
        else if (gapp.state == GUIMAP_REVIEW) {
            int key = read_keyboard_press(gapp.kbd_fds, gapp.num_kbd_fds);
            int jdy = 0, jconfirm = 0;
            if (gapp.joy_fd >= 0)
                read_joystick_nav(gapp.joy_fd, &gapp.joy_prev_y,
                                  &jdy, &jconfirm);

            if (key == KEY_UP || jdy < 0) {
                gapp.review_sel--;
                if (gapp.review_sel < 0) gapp.review_sel = 0;
            }
            else if (key == KEY_DOWN || jdy > 0) {
                gapp.review_sel++;
                if (gapp.review_sel >= GUIMAP_REVIEW_TOTAL)
                    gapp.review_sel = GUIMAP_REVIEW_TOTAL - 1;
            }
            else if (key == KEY_1) {
                /* redo selected mapping */
                if (gapp.review_sel >= 0 &&
                    gapp.review_sel < NUM_MAPPINGS) {
                    gapp.redo_single = gapp.review_sel;
                    gapp.cur_map = gapp.review_sel;
                    gapp.state = GUIMAP_MAP;
                    drain_keyboard_events(gapp.kbd_fds, gapp.num_kbd_fds);
                }
            }
            else if (key == KEY_A) {
                /* apply */
                gapp.applied = 1;
                break;
            }
            else if (key == KEY_Q || key == KEY_ESC) {
                /* quit without applying */
                break;
            }
            else if (key == KEY_S) {
                /* save to file */
                browser_load(&gapp.browser, "/mnt");
                gapp.state = GUIMAP_BROWSE;
                drain_keyboard_events(gapp.kbd_fds, gapp.num_kbd_fds);
            }
            else if (key == KEY_ENTER || key == KEY_SPACE || jconfirm) {
                if (gapp.review_sel >= 0 &&
                    gapp.review_sel < NUM_MAPPINGS) {
                    gapp.redo_single = gapp.review_sel;
                    gapp.cur_map = gapp.review_sel;
                    gapp.state = GUIMAP_MAP;
                    drain_keyboard_events(gapp.kbd_fds, gapp.num_kbd_fds);
                }
                else if (gapp.review_sel == GUIMAP_REVIEW_APPLY) {
                    gapp.applied = 1;
                    break;
                }
                else if (gapp.review_sel == GUIMAP_REVIEW_QUIT) {
                    break;
                }
                else if (gapp.review_sel == GUIMAP_REVIEW_SAVE) {
                    browser_load(&gapp.browser, "/mnt");
                    gapp.state = GUIMAP_BROWSE;
                    drain_keyboard_events(gapp.kbd_fds, gapp.num_kbd_fds);
                }
            }
        }
        else if (gapp.state == GUIMAP_BROWSE) {
            DirBrowser *b = &gapp.browser;
            int key = read_keyboard_press(gapp.kbd_fds, gapp.num_kbd_fds);
            int jdy = 0, jconfirm = 0;
            if (gapp.joy_fd >= 0)
                read_joystick_nav(gapp.joy_fd, &gapp.joy_prev_y,
                                  &jdy, &jconfirm);

            if (key == KEY_UP || jdy < 0) {
                b->selected--;
                if (b->selected < 0) b->selected = 0;
            }
            else if (key == KEY_DOWN || jdy > 0) {
                b->selected++;
                if (b->selected >= b->count) b->selected = b->count - 1;
            }
            else if (key == KEY_ENTER || jconfirm) {
                if (b->count > 0) {
                    DirEntry *e = &b->entries[b->selected];
                    if (strcmp(e->name, "..") == 0) {
                        char *slash = strrchr(b->path, '/');
                        if (slash && slash != b->path) *slash = '\0';
                        else strcpy(b->path, "/");
                        browser_load(b, b->path);
                    } else if (e->is_dir) {
                        char newpath[MAX_PATH_LEN];
                        if (strcmp(b->path, "/") == 0)
                            snprintf(newpath, sizeof(newpath),
                                     "/%.250s", e->name);
                        else
                            snprintf(newpath, sizeof(newpath),
                                     "%.250s/%.250s", b->path, e->name);
                        browser_load(b, newpath);
                    } else {
                        /* Export here */
                        guimap_save_script(&gapp);
                        gapp.state = GUIMAP_REVIEW;
                        drain_keyboard_events(gapp.kbd_fds,
                                              gapp.num_kbd_fds);
                    }
                }
            }
            else if (key == KEY_LEFT || key == KEY_BACKSPACE) {
                char *slash = strrchr(b->path, '/');
                if (slash && slash != b->path) *slash = '\0';
                else strcpy(b->path, "/");
                browser_load(b, b->path);
            }
            else if (key == KEY_Q || key == KEY_ESC) {
                gapp.state = GUIMAP_REVIEW;
            }

            /* Scroll */
            {
                int visible = 18;
                if (b->selected < b->scroll)
                    b->scroll = b->selected;
                if (b->selected >= b->scroll + visible)
                    b->scroll = b->selected - visible + 1;
            }
        }

        /* Render */
        fb_clear(&gapp.fb, COL_BG);

        switch (gapp.state) {
        case GUIMAP_MAP:    guimap_render_map(&gapp);    break;
        case GUIMAP_REVIEW: guimap_render_review(&gapp); break;
        case GUIMAP_BROWSE: guimap_render_browse(&gapp); break;
        }

        fb_flip(&gapp.fb);
        usleep(FRAME_MS * 1000);
    }

    /* Restore framebuffer to black */
    fb_clear(&gapp.fb, 0xFF000000);
    fb_flip(&gapp.fb);

    if (gapp.joy_fd >= 0)
        close(gapp.joy_fd);
    for (int i = 0; i < gapp.num_kbd_fds; i++)
        close(gapp.kbd_fds[i]);
    fb_destroy(&gapp.fb);
    return gapp.applied ? 0 : 1;
}

/* ================================================================
 * main
 * ================================================================ */

int main(int argc, char **argv)
{
    int help = 0, guimap = 0;

    init_mappings();

    if (parse_args(argc, argv, &help, &guimap) < 0)
        return 1;

    if (help) {
        print_usage();
        return 0;
    }

    if (guimap)
        return guimap_run();

    return normal_run();
}

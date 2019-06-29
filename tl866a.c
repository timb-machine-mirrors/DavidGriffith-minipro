/*
 * tl866a.c - Low level ops for TL866A/CS.
 *
 * This file is a part of Minipro.
 *
 * Minipro is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * Minipro is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "database.h"
#include "minipro.h"
#include "tl866a.h"
#if defined (_WIN32) || defined (__MSYS__)
#include "usb_win.h"
#else
#include "usb_nix.h"
#endif

// Commands
#define TL866A_GET_SYSTEM_INFO 0x00
#define TL866A_START_TRANSACTION 0x03
#define TL866A_END_TRANSACTION 0x04
#define TL866A_GET_CHIP_ID 0x05
#define TL866A_READ_USER 0x10
#define TL866A_WRITE_USER 0x11
#define TL866A_READ_CFG 0x12
#define TL866A_WRITE_CFG 0x13
#define TL866A_WRITE_CODE 0x20
#define TL866A_READ_CODE 0x21
#define TL866A_ERASE 0x22
#define TL866A_READ_DATA 0x30
#define TL866A_WRITE_DATA 0x31
#define TL866A_WRITE_LOCK 0x40
#define TL866A_READ_LOCK 0x41
#define TL866A_PROTECT_OFF 0x44
#define TL866A_PROTECT_ON 0x45
#define TL866A_BOOTLOADER_WRITE 0xAA
#define TL866A_BOOTLOADER_ERASE 0xCC
#define TL866A_UNLOCK_TSOP48 0xFD
#define TL866A_GET_STATUS 0xFE

// Hardware Bit Banging
#define TL866A_RESET_PIN_DRIVERS 0xD0
#define TL866A_SET_LATCH 0xD1
#define TL866A_READ_ZIF_PINS 0xD2
#define TL866A_OE_VPP 0x01
#define TL866A_OE_VCC_GND 0x02
#define TL866A_OE_ALL 0x03

// Firmware
#define TL866A_UPDATE_DAT_SIZE 312348
#define TL866A_ENC_FIRMWARE_SIZE 0x25D00
#define TL866A_UNENC_FIRMWARE_SIZE 0x1E400
#define TL866A_BOOTLOADER_SIZE 0x1800
#define TL866A_FIRMWARE_BLOCK_SIZE 0x50

typedef struct zif_pins_s {
  uint8_t pin;
  uint8_t latch;
  uint8_t oe;
  uint8_t mask;
} zif_pins_t;

// clang-format off
// 16 VPP pins. NPN trans. mask
static zif_pins_t vpp_pins[] =
{
    { .pin = 1, .latch = 1, .oe = 1, .mask = 0x04 },
    { .pin = 2, .latch = 1, .oe = 1, .mask = 0x08 },
    { .pin = 3, .latch = 0, .oe = 1, .mask = 0x04 },
    { .pin = 4, .latch = 0, .oe = 1, .mask = 0x08 },
    { .pin = 9, .latch = 0, .oe = 1, .mask = 0x20 },
    { .pin = 10, .latch = 0, .oe = 1, .mask = 0x10 },
    { .pin = 30, .latch = 1, .oe = 1, .mask = 0x01 },
    { .pin = 31, .latch = 0, .oe = 1, .mask = 0x01 },
    { .pin = 32, .latch = 1, .oe = 1, .mask = 0x80 },
    { .pin = 33, .latch = 0, .oe = 1, .mask = 0x40 },
    { .pin = 34, .latch = 0, .oe = 1, .mask = 0x02 },
    { .pin = 36, .latch = 1, .oe = 1, .mask = 0x02 },
    { .pin = 37, .latch = 0, .oe = 1, .mask = 0x80 },
    { .pin = 38, .latch = 1, .oe = 1, .mask = 0x40 },
    { .pin = 39, .latch = 1, .oe = 1, .mask = 0x20 },
    { .pin = 40, .latch = 1, .oe = 1, .mask = 0x10 }
};

// 24 VCC Pins. PNP trans. mask
static zif_pins_t vcc_pins[] =
{
    { .pin = 1, .latch = 2, .oe = 2, .mask = 0x7f },
    { .pin = 2, .latch = 2, .oe = 2, .mask = 0xef },
    { .pin = 3, .latch = 2, .oe = 2, .mask = 0xdf },
    { .pin = 4, .latch = 3, .oe = 2, .mask = 0xfe },
    { .pin = 5, .latch = 2, .oe = 2, .mask = 0xfb },
    { .pin = 6, .latch = 3, .oe = 2, .mask = 0xfb },
    { .pin = 7, .latch = 4, .oe = 2, .mask = 0xbf },
    { .pin = 8, .latch = 4, .oe = 2, .mask = 0xfd },
    { .pin = 9, .latch = 4, .oe = 2, .mask = 0xfb },
    { .pin = 10, .latch = 4, .oe = 2, .mask = 0xf7 },
    { .pin = 11, .latch = 4, .oe = 2, .mask = 0xfe },
    { .pin = 12, .latch = 4, .oe = 2, .mask = 0x7f },
    { .pin = 13, .latch = 4, .oe = 2, .mask = 0xef },
    { .pin = 21, .latch = 4, .oe = 2, .mask = 0xdf },
    { .pin = 30, .latch = 3, .oe = 2, .mask = 0xbf },
    { .pin = 32, .latch = 3, .oe = 2, .mask = 0xfd },
    { .pin = 33, .latch = 3, .oe = 2, .mask = 0xdf },
    { .pin = 34, .latch = 3, .oe = 2, .mask = 0xf7 },
    { .pin = 35, .latch = 3, .oe = 2, .mask = 0xef },
    { .pin = 36, .latch = 3, .oe = 2, .mask = 0x7f },
    { .pin = 37, .latch = 2, .oe = 2, .mask = 0xf7 },
    { .pin = 38, .latch = 2, .oe = 2, .mask = 0xbf },
    { .pin = 39, .latch = 2, .oe = 2, .mask = 0xfe },
    { .pin = 40, .latch = 2, .oe = 2, .mask = 0xfd }
};

// 25 GND Pins. NPN trans. mask
static zif_pins_t gnd_pins[] =
{
    { .pin = 1, .latch = 6, .oe = 2, .mask = 0x04 },
    { .pin = 2, .latch = 6, .oe = 2, .mask = 0x08 },
    { .pin = 3, .latch = 6, .oe = 2, .mask = 0x40 },
    { .pin = 4, .latch = 6, .oe = 2, .mask = 0x02 },
    { .pin = 5, .latch = 5, .oe = 2, .mask = 0x04 },
    { .pin = 6, .latch = 5, .oe = 2, .mask = 0x08 },
    { .pin = 7, .latch = 5, .oe = 2, .mask = 0x40 },
    { .pin = 8, .latch = 5, .oe = 2, .mask = 0x02 },
    { .pin = 9, .latch = 5, .oe = 2, .mask = 0x01 },
    { .pin = 10, .latch = 5, .oe = 2, .mask = 0x80 },
    { .pin = 11, .latch = 5, .oe = 2, .mask = 0x10 },
    { .pin = 12, .latch = 5, .oe = 2, .mask = 0x20 },
    { .pin = 14, .latch = 7, .oe = 2, .mask = 0x08 },
    { .pin = 16, .latch = 7, .oe = 2, .mask = 0x40 },
    { .pin = 20, .latch = 9, .oe = 2, .mask = 0x01 },
    { .pin = 30, .latch = 7, .oe = 2, .mask = 0x04 },
    { .pin = 31, .latch = 6, .oe = 2, .mask = 0x01 },
    { .pin = 32, .latch = 6, .oe = 2, .mask = 0x80 },
    { .pin = 34, .latch = 6, .oe = 2, .mask = 0x10 },
    { .pin = 35, .latch = 6, .oe = 2, .mask = 0x20 },
    { .pin = 36, .latch = 7, .oe = 2, .mask = 0x20 },
    { .pin = 37, .latch = 7, .oe = 2, .mask = 0x10 },
    { .pin = 38, .latch = 7, .oe = 2, .mask = 0x02 },
    { .pin = 39, .latch = 7, .oe = 2, .mask = 0x80 },
    { .pin = 40, .latch = 7, .oe = 2, .mask = 0x01 }
};

enum VPP_PINS
{
    VPP1,	VPP2,	VPP3,	VPP4,	VPP9,	VPP10,	VPP30,	VPP31,
    VPP32,	VPP33,	VPP34,	VPP36,	VPP37,	VPP38,	VPP39,	VPP40
};

enum VCC_PINS
{
    VCC1,	VCC2,	VCC3,	VCC4,	VCC5,	VCC6,	VCC7,	VCC8,
    VCC9,	VCC10,	VCC11,	VCC12,	VCC13,	VCC21,	VCC30,	VCC32,
    VCC33,	VCC34,	VCC35,	VCC36,	VCC37,	VCC38,	VCC39,	VCC40
};

enum GND_PINS
{
    GND1,	GND2,	GND3,	GND4,	GND5,	GND6,	GND7,	GND8,
    GND9,	GND10,	GND11,	GND12,	GND14,	GND16,	GND20,	GND30,
    GND31,	GND32,	GND34,	GND35,	GND36,	GND37,	GND38,	GND39,
    GND40
};

// clang-format on

typedef struct update_dat_s {
  uint8_t header[4];  // file header
  uint32_t a_crc32;   // 4 bytes
  uint8_t pad1;
  uint8_t a_erase;
  uint8_t pad2;
  uint8_t pad3;
  uint32_t cs_crc32;  // 4 bytes
  uint8_t pad4;
  uint8_t cs_erase;
  uint8_t pad5;
  uint8_t pad6;
  uint32_t a_index;            // index used in A firmware decryption
  uint8_t a_xortable1[256];    // First xortable used in A firmware decryption
  uint8_t a_xortable2[1024];   // Second xortable used in A firmware decryption
  uint32_t cs_index;           // index used in CS firmware decryption
  uint8_t cs_xortable1[256];   // First xortable used in CS firmware decryption
  uint8_t cs_xortable2[1024];  // Second xortable used in CS firmware decryption
  uint8_t a_firmware[TL866A_ENC_FIRMWARE_SIZE];   // Encrypted A firmware
  uint8_t cs_firmware[TL866A_ENC_FIRMWARE_SIZE];  // Encrypted CS firmware
} update_dat_t;

static void msg_init(minipro_handle_t *handle, uint8_t command, uint8_t *buffer,
                     size_t length) {
  memset(buffer, 0, length);
  buffer[0] = command;
  buffer[1] = handle->device->protocol_id;
  buffer[2] = handle->device->variant;
}

int tl866a_begin_transaction(minipro_handle_t *handle) {
  uint8_t msg[64];
  uint8_t ovc;
  msg_init(handle, TL866A_START_TRANSACTION, msg, sizeof(msg));

  // 16 bit data memory size (3+4)
  format_int(&(msg[3]), handle->device->data_memory_size, 2, MP_LITTLE_ENDIAN);

  // 8 bit device options (VPP voltage for example)
  msg[5] = (uint8_t)handle->device->opts1;

  // 16 bit various options (6+7)
  format_int(&(msg[6]), handle->device->opts2, 2, MP_LITTLE_ENDIAN);

  // 8 bit device options (VDD+VCC)
  msg[8] = (uint8_t)(handle->device->opts1 >> 8);

  // 16 bit device specific options (9+10)
  format_int(&(msg[9]), handle->device->opts3, 2, MP_LITTLE_ENDIAN);

  // 8 bit icsp options
  msg[11] = handle->icsp;

  // 24 bit code size (12+13+14)
  format_int(&(msg[12]), handle->device->code_memory_size, 3, MP_LITTLE_ENDIAN);

  if (msg_send(handle->usb_handle, msg, 48)) return EXIT_FAILURE;
  if (minipro_get_ovc_status(handle, NULL, &ovc)) return EXIT_FAILURE;
  if (ovc) {
    fprintf(stderr, "Overcurrent protection!\007\n");
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}

int tl866a_end_transaction(minipro_handle_t *handle) {
  uint8_t msg[64];
  msg_init(handle, TL866A_END_TRANSACTION, msg, sizeof(msg));
  msg[3] = 0x00;
  return msg_send(handle->usb_handle, msg, 4);
}

int tl866a_protect_off(minipro_handle_t *handle) {
  uint8_t msg[64];
  msg_init(handle, TL866A_PROTECT_OFF, msg, sizeof(msg));
  return msg_send(handle->usb_handle, msg, 10);
}

int tl866a_protect_on(minipro_handle_t *handle) {
  uint8_t msg[64];
  msg_init(handle, TL866A_PROTECT_ON, msg, sizeof(msg));
  return msg_send(handle->usb_handle, msg, 10);
}

int tl866a_get_ovc_status(minipro_handle_t *handle, minipro_status_t *status,
                          uint8_t *ovc) {
  uint8_t msg[64];
  msg_init(handle, TL866A_GET_STATUS, msg, sizeof(msg));
  if (msg_send(handle->usb_handle, msg, 5)) return EXIT_FAILURE;
  memset(msg, 0, sizeof(msg));
  if (msg_recv(handle->usb_handle, msg, sizeof(msg))) return EXIT_FAILURE;
  if (status)  // Check for null
  {
    // This is verify while writing feature.
    status->error = msg[0];
    status->address = load_int(&msg[6], 3, MP_LITTLE_ENDIAN);
    status->c1 = load_int(&msg[2], 2, MP_LITTLE_ENDIAN);
    status->c2 = load_int(&msg[4], 2, MP_LITTLE_ENDIAN);
  }
  *ovc = msg[9];  // return the ovc status
  return EXIT_SUCCESS;
}

int tl866a_erase(minipro_handle_t *handle) {
  uint8_t msg[64];
  msg_init(handle, TL866A_ERASE, msg, sizeof(msg));
  format_int(&(msg[2]), 0x03, 2, MP_LITTLE_ENDIAN);
  /* There's no "write unlock". This is how many fuses the controller have
   * or 1 if the device is something else.
   */
  switch (handle->device->protocol_id) {
    case PLD_PROTOCOL_16V8:
    case PLD_PROTOCOL_20V8:
    case PLD_PROTOCOL_22V10:
      break;
    default:

      if (((fuse_decl_t *)handle->device->config) == NULL ||
          ((fuse_decl_t *)handle->device->config)->num_fuses == 0)
        msg[2] = 1;
      else {
        msg[2] = ((fuse_decl_t *)handle->device->config)->erase_num_fuses;
      }
  }
  if (msg_send(handle->usb_handle, msg, 15)) return EXIT_FAILURE;
  memset(msg, 0x00, sizeof(msg));
  return msg_recv(handle->usb_handle, msg, sizeof(msg));
}

int tl866a_read_block(minipro_handle_t *handle, uint8_t type, uint32_t addr,
                      uint8_t *buffer, size_t size) {
  if (type == MP_CODE) {
    type = TL866A_READ_CODE;
  } else if (type == MP_DATA) {
    type = TL866A_READ_DATA;
  } else {
    fprintf(stderr, "Unknown type for read_block (%d)\n", type);
    return EXIT_FAILURE;
  }
  uint8_t msg[64];
  msg_init(handle, type, msg, sizeof(msg));
  format_int(&(msg[2]), size, 2, MP_LITTLE_ENDIAN);
  format_int(&(msg[4]), addr, 3, MP_LITTLE_ENDIAN);
  if (msg_send(handle->usb_handle, msg, 18)) return EXIT_FAILURE;
  return msg_recv(handle->usb_handle, buffer, size);
}

int tl866a_write_block(minipro_handle_t *handle, uint8_t type, uint32_t addr,
                       uint8_t *buffer, size_t size) {
  if (type == MP_CODE) {
    type = TL866A_WRITE_CODE;
  } else if (type == MP_DATA) {
    type = TL866A_WRITE_DATA;
  } else {
    fprintf(stderr, "Unknown type for read_block (%d)\n", type);
    return EXIT_FAILURE;
  }

  uint8_t *msg = malloc(size + 7);
  if (!msg) {
    fprintf(stderr, "Out of memory!");
    return EXIT_FAILURE;
  }
  msg_init(handle, type, msg, sizeof(size + 7));
  format_int(&(msg[2]), size, 2, MP_LITTLE_ENDIAN);
  format_int(&(msg[4]), addr, 3, MP_LITTLE_ENDIAN);
  memcpy(&(msg[7]), buffer, size);
  if (msg_send(handle->usb_handle, msg, size + 7)) {
    free(msg);
    return EXIT_FAILURE;
  }
  free(msg);
  return EXIT_SUCCESS;
}

/* Model-specific ID, e.g. AVR Device ID (not longer than 4 bytes) */
int tl866a_get_chip_id(minipro_handle_t *handle, uint8_t *type,
                       uint32_t *device_id) {
  ;
  uint8_t msg[64], format;
  msg_init(handle, TL866A_GET_CHIP_ID, msg, sizeof(msg));
  if (msg_send(handle->usb_handle, msg, 8)) return EXIT_FAILURE;
  if (msg_recv(handle->usb_handle, msg, 32)) return EXIT_FAILURE;
  *type = msg[0];  // The Chip ID type (1-5)
  format = (*type == MP_ID_TYPE3 || *type == MP_ID_TYPE4 ? MP_LITTLE_ENDIAN
                                                         : MP_BIG_ENDIAN);
  msg[1] &= 0x03;  // The length byte is always 1-4 but never know, truncate to
                   // max. 4 bytes.
  *device_id = (msg[1] ? load_int(&(msg[2]), msg[1], format)
                       : 0);  // Check for positive length.
  return EXIT_SUCCESS;
}

int tl866a_read_fuses(minipro_handle_t *handle, uint8_t type, size_t size,
                      uint8_t items_count, uint8_t *buffer) {
  if (type == MP_FUSE_USER) {
    type = TL866A_READ_USER;
  } else if (type == MP_FUSE_CFG) {
    type = TL866A_READ_CFG;
  } else if (type == MP_FUSE_LOCK) {
    type = TL866A_READ_LOCK;
  } else {
    fprintf(stderr, "Unknown type for read_fuses (%d)\n", type);
    return EXIT_FAILURE;
  }
  uint8_t msg[64];
  msg_init(handle, type, msg, sizeof(msg));
  msg[2] = items_count;
  format_int(&msg[4], handle->device->code_memory_size, 3, MP_LITTLE_ENDIAN);
  if (msg_send(handle->usb_handle, msg, 18)) return EXIT_FAILURE;
  if (msg_recv(handle->usb_handle, msg, sizeof(msg))) return EXIT_FAILURE;
  memcpy(buffer, &(msg[7]), size);
  return EXIT_SUCCESS;
}

int tl866a_write_fuses(minipro_handle_t *handle, uint8_t type, size_t size,
                       uint8_t items_count, uint8_t *buffer) {
  if (type == MP_FUSE_USER) {
    type = TL866A_WRITE_USER;
  } else if (type == MP_FUSE_CFG) {
    type = TL866A_WRITE_CFG;
  } else if (type == MP_FUSE_LOCK) {
    type = TL866A_WRITE_LOCK;
  } else {
    fprintf(stderr, "Unknown type for write_fuses (%d)\n", type);
  }
  uint8_t msg[64];
  msg_init(handle, type, msg, sizeof(msg));
  if (buffer != NULL) {
    msg[2] = items_count;
    format_int(&msg[4], handle->device->code_memory_size - 0x38, 3,
               MP_LITTLE_ENDIAN);  // 0x38, firmware bug?
    memcpy(&(msg[7]), buffer, size);
  }
  return msg_send(handle->usb_handle, msg, buffer != NULL ? 64 : 10);
}

int tl866a_write_jedec_row(minipro_handle_t *handle, uint8_t *buffer,
                           uint8_t row, size_t size) {
  uint8_t msg[64];
  memset(msg, 0, sizeof(msg));
  msg[0] = TL866A_WRITE_CODE;
  msg[1] = handle->device->protocol_id;
  msg[2] = size;
  msg[4] = row;
  memcpy(&msg[7], buffer, size / 8 + 1);
  return msg_send(handle->usb_handle, msg, 64);
}

int tl866a_read_jedec_row(minipro_handle_t *handle, uint8_t *buffer,
                          uint8_t row, size_t size) {
  uint8_t msg[64];
  memset(msg, 0, sizeof(msg));
  msg[0] = TL866A_READ_CODE;
  msg[1] = handle->device->protocol_id;
  msg[2] = size;
  msg[4] = row;
  if (msg_send(handle->usb_handle, msg, 18)) return EXIT_FAILURE;
  if (msg_recv(handle->usb_handle, msg, sizeof(msg))) return EXIT_FAILURE;
  memcpy(buffer, msg, size / 8 + 1);
  return EXIT_SUCCESS;
}

// Unlocking the TSOP48 adapter.
int tl866a_unlock_tsop48(minipro_handle_t *handle, uint8_t *status) {
  uint8_t msg[64];
  memset(msg, 0, sizeof(msg));
  srand(time(NULL));
  uint16_t i, crc = 0;
  for (i = 7; i < 15; i++) {
    msg[i] = (uint8_t)rand();
    // Calculate the crc16
    crc = (crc >> 8) | (crc << 8);
    crc ^= msg[i];
    crc ^= (crc & 0xFF) >> 4;
    crc ^= (crc << 12);
    crc ^= (crc & 0xFF) << 5;
  }
  msg[0] = TL866A_UNLOCK_TSOP48;
  msg[15] = msg[9];
  msg[16] = msg[11];
  msg[9] = (uint8_t)crc;
  msg[11] = (uint8_t)(crc >> 8);
  if (msg_send(handle->usb_handle, msg, 17)) return EXIT_FAILURE;
  if (msg_recv(handle->usb_handle, msg, sizeof(msg))) return EXIT_FAILURE;
  *status = msg[1];
  return EXIT_SUCCESS;
}

// Minipro hardware check
int tl866a_hardware_check(minipro_handle_t *handle) {
  uint8_t read_buffer[64];
  uint8_t msg[64];
  memset(msg, 0, sizeof(msg));

  uint8_t i, errors = 0;
  // Reset pin drivers state
  msg[0] = TL866A_RESET_PIN_DRIVERS;
  if (msg_send(handle->usb_handle, msg, 10)) {
    return EXIT_FAILURE;
  }

  // Testing 16 VPP pin drivers
  for (i = 0; i < 16; i++) {
    msg[0] = TL866A_SET_LATCH;
    msg[7] = 1;  // This is the number of latches we want to set (1-8)
    msg[8] = vpp_pins[i].oe;     // This is the Output Enable we want to
                                 // select(/OE) (1=OE_VPP, 2=OE_VCC+GND, 3=BOTH)
    msg[9] = vpp_pins[i].latch;  // This is the latch number we want to set
                                 // (0-7; see the schematic diagram)
    msg[10] = vpp_pins[i].mask;  // This is the latch value we want to write
                                 // (see the schematic diagram)
    if (msg_send(handle->usb_handle, msg, 32)) {
      minipro_close(handle);
      return EXIT_FAILURE;
    }
    usleep(5000);
    msg[0] = TL866A_READ_ZIF_PINS;
    if (msg_send(handle->usb_handle, msg, 18)) {
      return EXIT_FAILURE;
    }
    if (msg_recv(handle->usb_handle, read_buffer, sizeof(read_buffer)))
      return EXIT_FAILURE;
    if (read_buffer[1]) {
      msg[0] = TL866A_RESET_PIN_DRIVERS;
      if (msg_send(handle->usb_handle, msg, 10)) {
        return EXIT_FAILURE;
      }
      msg[0] = TL866A_END_TRANSACTION;
      if (msg_send(handle->usb_handle, msg, 4)) {
        return EXIT_FAILURE;
      }
      fprintf(stderr,
              "Overcurrent protection detected while testing VPP pin driver "
              "%u!\007\n",
              vpp_pins[i].pin);
      return EXIT_FAILURE;
    }
    if (!read_buffer[6 + vpp_pins[i].pin]) errors++;
    fprintf(stderr, "VPP driver pin %u is %s\n", vpp_pins[i].pin,
            read_buffer[6 + vpp_pins[i].pin] ? "OK" : "Bad");
    msg[0] = TL866A_RESET_PIN_DRIVERS;
    if (msg_send(handle->usb_handle, msg, 10)) {
      return EXIT_FAILURE;
    }
  }
  fprintf(stderr, "\n");
  // Testing 24 VCC pin drivers
  for (i = 0; i < 24; i++) {
    msg[0] = TL866A_SET_LATCH;
    msg[7] = 1;
    msg[8] = vcc_pins[i].oe;
    msg[9] = vcc_pins[i].latch;
    msg[10] = vcc_pins[i].mask;
    if (msg_send(handle->usb_handle, msg, 32)) {
      return EXIT_FAILURE;
    }
    usleep(5000);
    msg[0] = TL866A_READ_ZIF_PINS;
    if (msg_send(handle->usb_handle, msg, 18)) {
      return EXIT_FAILURE;
    }
    if (msg_recv(handle->usb_handle, read_buffer, sizeof(read_buffer))) {
      return EXIT_FAILURE;
    }
    if (read_buffer[1]) {
      msg[0] = TL866A_RESET_PIN_DRIVERS;
      if (msg_send(handle->usb_handle, msg, 10)) {
        return EXIT_FAILURE;
      }
      if (minipro_end_transaction(handle)) {
        return EXIT_FAILURE;
      }
      fprintf(stderr,
              "Overcurrent protection detected while testing VCC pin driver "
              "%u!\007\n",
              vcc_pins[i].pin);
      return EXIT_FAILURE;
    }
    if (!read_buffer[6 + vcc_pins[i].pin]) errors++;
    fprintf(stderr, "VCC driver pin %u is %s\n", vcc_pins[i].pin,
            read_buffer[6 + vcc_pins[i].pin] ? "OK" : "Bad");
    msg[0] = TL866A_RESET_PIN_DRIVERS;
    if (msg_send(handle->usb_handle, msg, 10)) {
      return EXIT_FAILURE;
    }
  }
  fprintf(stderr, "\n");
  // Testing 25 GND pin drivers
  for (i = 0; i < 25; i++) {
    msg[0] = TL866A_SET_LATCH;
    msg[7] = 1;
    msg[8] = gnd_pins[i].oe;
    msg[9] = gnd_pins[i].latch;
    msg[10] = gnd_pins[i].mask;
    if (msg_send(handle->usb_handle, msg, 32)) {
      return EXIT_FAILURE;
    }
    usleep(5000);
    msg[0] = TL866A_READ_ZIF_PINS;
    if (msg_send(handle->usb_handle, msg, 18)) {
      return EXIT_FAILURE;
    }
    if (msg_recv(handle->usb_handle, read_buffer, sizeof(read_buffer))) {
      return EXIT_FAILURE;
    }
    if (read_buffer[1]) {
      msg[0] = TL866A_RESET_PIN_DRIVERS;
      if (msg_send(handle->usb_handle, msg, 10)) {
        minipro_close(handle);
        return EXIT_FAILURE;
      }
      if (minipro_end_transaction(handle)) {
        return EXIT_FAILURE;
      }
      fprintf(stderr,
              "Overcurrent protection detected while testing GND pin driver "
              "%u!\007\n",
              gnd_pins[i].pin);
      return EXIT_FAILURE;
    }
    if (read_buffer[6 + gnd_pins[i].pin]) errors++;
    fprintf(stderr, "GND driver pin %u is %s\n", gnd_pins[i].pin,
            read_buffer[6 + gnd_pins[i].pin] ? "Bad" : "OK");
    msg[0] = TL866A_RESET_PIN_DRIVERS;
    if (msg_send(handle->usb_handle, msg, 10)) {
      return EXIT_FAILURE;
    }
  }

  fprintf(stderr, "\n");
  // Testing VPP overcurrent protection
  msg[0] = TL866A_SET_LATCH;
  msg[7] = 2;              // We will set two latches
  msg[8] = TL866A_OE_ALL;  // Both OE_VPP and OE_GND active
  msg[9] = vpp_pins[VPP1].latch;
  msg[10] = vpp_pins[VPP1].mask;  // Put the VPP voltage to the ZIF pin1
  msg[11] = gnd_pins[GND1].latch;
  msg[12] = gnd_pins[GND1].mask;  // Now put the same pin ZIF 1 to the GND
  if (msg_send(handle->usb_handle, msg, 32)) {
    return EXIT_FAILURE;
  }
  msg[0] = TL866A_READ_ZIF_PINS;  // Read back the OVC status (should be active)
  if (msg_send(handle->usb_handle, msg, 18)) {
    return EXIT_FAILURE;
  }
  if (msg_recv(handle->usb_handle, read_buffer, sizeof(read_buffer))) {
    return EXIT_FAILURE;
  }
  if (read_buffer[1]) {
    fprintf(stderr, "VPP overcurrent protection is OK.\n");
  } else {
    fprintf(stderr, "VPP overcurrent protection failed!\007\n");
    errors++;
  }

  // Reset internal state
  memset(msg, 0, sizeof(msg));
  msg[0] = TL866A_RESET_PIN_DRIVERS;
  if (msg_send(handle->usb_handle, msg, 10)) {
    return EXIT_FAILURE;
  }

  msg[0] = TL866A_END_TRANSACTION;
  if (msg_send(handle->usb_handle, msg, 4)) {
    return EXIT_FAILURE;
  }

  usleep(5000);
  // Testing VCC overcurrent protection
  msg[0] = TL866A_SET_LATCH;
  msg[7] = 2;                  // We will set two latches
  msg[8] = TL866A_OE_VCC_GND;  // OE GND is active
  msg[9] = vcc_pins[VCC40].latch;
  msg[10] = vcc_pins[VCC40].mask;  // Put the VCC voltage to the ZIF pin 40
  msg[11] = gnd_pins[GND40].latch;
  msg[12] = gnd_pins[GND40].mask;  // Now put the same pin ZIF 40 to the GND
  if (msg_send(handle->usb_handle, msg, 32)) {
    return EXIT_FAILURE;
  }
  msg[0] = TL866A_READ_ZIF_PINS;  // Read back the OVC status
  if (msg_send(handle->usb_handle, msg, 18)) {
    return EXIT_FAILURE;
  }
  if (msg_recv(handle->usb_handle, read_buffer, sizeof(read_buffer))) {
    return EXIT_FAILURE;
  }
  if (read_buffer[1]) {
    fprintf(stderr, "VCC overcurrent protection is OK.\n");
  } else {
    fprintf(stderr, "VCC overcurrent protection failed!\007\n");
    errors++;
  }
  if (errors)
    fprintf(stderr, "\nHardware test completed with %u error(s).\007\n",
            errors);
  else
    fprintf(stderr, "\nHardware test completed successfully!\n");

  // End transaction
  memset(msg, 0, sizeof(msg));
  msg[0] = TL866A_END_TRANSACTION;
  if (msg_send(handle->usb_handle, msg, 4)) {
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}

/* Firmware updater section */
//////////////////////////////////////////////////////////////////////////////////
static const uint8_t a_xortable[] = {
    0xA4, 0x1E, 0x42, 0x8C, 0x3C, 0x76, 0x14, 0xC7, 0xB8, 0xB5, 0x81, 0x4A,
    0x13, 0x37, 0x7C, 0x0A, 0xFE, 0x3B, 0x63, 0xC1, 0xD5, 0xFD, 0x8C, 0x39,
    0xD1, 0x1F, 0x22, 0xC7, 0x7F, 0x4D, 0x2F, 0x15, 0x71, 0x21, 0xF9, 0x25,
    0x33, 0x44, 0x92, 0x93, 0x80, 0xD7, 0xAB, 0x1B, 0xB6, 0x11, 0xA9, 0x5A,
    0x88, 0x29, 0xFB, 0xD9, 0xF3, 0x76, 0xAA, 0x47, 0x73, 0xD5, 0x31, 0x06,
    0x76, 0x4B, 0x90, 0xEA, 0x11, 0xEB, 0x9C, 0x3D, 0xF2, 0xFA, 0x99, 0x06,
    0x96, 0x52, 0x0A, 0x8A, 0xBC, 0x04, 0xC8, 0x14, 0x19, 0x41, 0x52, 0xF2,
    0x4D, 0x7B, 0x64, 0xC0, 0x16, 0xC7, 0xCB, 0xE9, 0xC3, 0x86, 0x77, 0x6A,
    0xEC, 0x44, 0xD2, 0xD9, 0x61, 0xE0, 0x50, 0xA6, 0x60, 0xED, 0x47, 0xA2,
    0x0B, 0x59, 0x02, 0xBD, 0x18, 0x4C, 0x11, 0x14, 0xCB, 0x53, 0xE2, 0x2B,
    0x21, 0xBE, 0x96, 0x76, 0x4F, 0x47, 0x0D, 0x1F, 0x6A, 0xF4, 0x43, 0x03,
    0x68, 0x3E, 0xE0, 0xFE, 0x47, 0x72, 0x0A, 0x68, 0x8C, 0x58, 0x7E, 0xDF,
    0xEF, 0x13, 0xDF, 0x47, 0x55, 0x48, 0x4D, 0x10, 0xFE, 0x82, 0x3A, 0xB7,
    0x00, 0xD5, 0x79, 0x90, 0xF4, 0xC2, 0x98, 0xC2, 0xEF, 0x5B, 0x70, 0x93,
    0xB4, 0xA7, 0xFA, 0xE6, 0x27, 0x48, 0x65, 0x01, 0x05, 0x5B, 0x65, 0x94,
    0xD3, 0xA0, 0xCD, 0xF7, 0x14, 0xDB, 0x60, 0xB4, 0xBF, 0x7A, 0xE4, 0x45,
    0xF0, 0x77, 0x79, 0x1F, 0xDE, 0x80, 0x29, 0xEF, 0x0D, 0x56, 0xC0, 0x23,
    0xC5, 0x73, 0xDE, 0xAC, 0xC2, 0xEF, 0x4A, 0x02, 0x2D, 0xA4, 0x89, 0x69,
    0xCB, 0x91, 0xB0, 0x74, 0x75, 0x7C, 0x76, 0xC7, 0xC8, 0xDB, 0x8D, 0x20,
    0x1D, 0xF5, 0x33, 0x99, 0xBB, 0x45, 0x04, 0x27, 0x4C, 0x1F, 0x12, 0x67,
    0x8E, 0x96, 0x37, 0x9A, 0x4B, 0x9C, 0xAA, 0xED, 0x8B, 0x6B, 0xD1, 0xFF,
    0x08, 0x24, 0x56, 0x9D};

static const uint8_t cs_xortable[] = {
    0x0B, 0x08, 0x07, 0x18, 0xEC, 0xC7, 0xDF, 0x8C, 0xD6, 0x76, 0xCE, 0x10,
    0x9F, 0x61, 0x7C, 0xF5, 0x61, 0x09, 0xFB, 0x59, 0xD0, 0x24, 0xB4, 0x4F,
    0xCA, 0xE4, 0xA1, 0x3A, 0x30, 0x7C, 0xBD, 0x7A, 0xF5, 0xE1, 0xB9, 0x4B,
    0x74, 0xCD, 0xF1, 0xE9, 0x07, 0x0A, 0x9E, 0xF9, 0xD5, 0xED, 0x4D, 0x24,
    0xEB, 0x21, 0x90, 0x05, 0x8F, 0xA5, 0xF3, 0x45, 0xD0, 0x18, 0x31, 0x04,
    0x62, 0x35, 0xA8, 0x7B, 0xA9, 0x9A, 0x0B, 0xE0, 0x14, 0xCD, 0x57, 0x8A,
    0xAC, 0x80, 0x08, 0x56, 0xED, 0x14, 0x8C, 0x49, 0xD4, 0x5D, 0xF8, 0x77,
    0x39, 0xA5, 0xFA, 0x23, 0x5F, 0xF3, 0x0E, 0x27, 0xCA, 0x8D, 0xF5, 0x97,
    0x50, 0xBB, 0x64, 0xA1, 0x73, 0xCE, 0xF9, 0xB7, 0xEE, 0x61, 0x72, 0xF1,
    0x8E, 0xDF, 0x21, 0xAC, 0x43, 0x45, 0x9B, 0x78, 0x77, 0x29, 0xB1, 0x31,
    0x9E, 0xFC, 0xA1, 0x6B, 0x0F, 0x8C, 0x8D, 0x13, 0x12, 0xCC, 0x2B, 0x54,
    0x3A, 0xD8, 0xBF, 0xB8, 0xF5, 0x34, 0x46, 0x90, 0x61, 0x54, 0xF4, 0x95,
    0x61, 0x62, 0xE1, 0xCF, 0xF1, 0x3B, 0x00, 0xB6, 0xB6, 0xBB, 0x50, 0x98,
    0xD9, 0x3A, 0x56, 0x3A, 0x16, 0x56, 0xCA, 0xC2, 0x10, 0xF3, 0x91, 0xD4,
    0xE8, 0x81, 0xEB, 0xFC, 0x0D, 0x7E, 0xEE, 0x4C, 0x56, 0x3B, 0x33, 0x46,
    0x4E, 0xE2, 0xCF, 0xFC, 0xCF, 0xB8, 0x84, 0x75, 0xD2, 0xA0, 0x39, 0x53,
    0x85, 0xE1, 0xA8, 0xB3, 0x9E, 0x28, 0x57, 0x55, 0xEF, 0xD1, 0xC9, 0xFD,
    0x3B, 0x62, 0xF5, 0x18, 0x49, 0x58, 0xF7, 0xA3, 0x36, 0x27, 0x06, 0x49,
    0x0F, 0x7C, 0xA6, 0xCB, 0xA0, 0xC5, 0x1E, 0xA5, 0x86, 0xF3, 0x2D, 0xEF,
    0x8C, 0x7E, 0xF9, 0x81, 0x34, 0xAA, 0x48, 0x5A, 0x93, 0x0A, 0xF2, 0x43,
    0x62, 0x42, 0x97, 0xAF, 0x53, 0x10, 0x8D, 0xE6, 0xA1, 0x8E, 0x1C, 0x62,
    0xEB, 0xB1, 0xEE, 0x79};

// encrypt a block of 80 bytes
static void encrypt_block(uint8_t *data, const uint8_t *xortable,
                          uint8_t index) {
  uint32_t i;
  for (i = 0; i < 16; i++) {
    data[i + 64] = (uint8_t)rand();
  }

  for (i = 0; i < TL866A_FIRMWARE_BLOCK_SIZE / 2; i += 4) {
    uint8_t t = data[i];
    data[i] = data[TL866A_FIRMWARE_BLOCK_SIZE - i - 1];
    data[TL866A_FIRMWARE_BLOCK_SIZE - i - 1] = t;
  }
  for (i = 0; i < TL866A_FIRMWARE_BLOCK_SIZE - 1; i++) {
    data[i] = ((data[i] << 3) & 0xF8) | data[i + 1] >> 5;
  }
  data[TL866A_FIRMWARE_BLOCK_SIZE - 1] =
      (data[TL866A_FIRMWARE_BLOCK_SIZE - 1] << 3) & 0xF8;
  for (i = 0; i < TL866A_FIRMWARE_BLOCK_SIZE; i++) {
    data[i] ^= xortable[index++];
  }
}

// decrypt a block of 80 bytes
static void decrypt_block(uint8_t *data, const uint8_t *xortable,
                          uint8_t index) {
  uint32_t i;
  for (i = 0; i < TL866A_FIRMWARE_BLOCK_SIZE; i++) {
    data[i] ^= xortable[index++];
  }

  for (i = TL866A_FIRMWARE_BLOCK_SIZE - 1; i > 0; i--) {
    data[i] = (uint8_t)((data[i] >> 3 & 0x1F) | data[i - 1] << 5);
  }
  data[0] = (data[0] >> 3) & 0x1F;

  for (i = 0; i < TL866A_FIRMWARE_BLOCK_SIZE / 2; i += 4) {
    uint8_t t = data[i];
    data[i] = data[TL866A_FIRMWARE_BLOCK_SIZE - i - 1];
    data[TL866A_FIRMWARE_BLOCK_SIZE - i - 1] = t;
  }
}

// Encrypt firmware
static void encrypt_firmware(const uint8_t *data_in, uint8_t *data_out,
                             uint8_t key, uint8_t index) {
  uint32_t i;
  uint8_t data[TL866A_FIRMWARE_BLOCK_SIZE];
  const uint8_t *xortable = (key == MP_TL866A ? a_xortable : cs_xortable);

  for (i = 0; i < TL866A_UNENC_FIRMWARE_SIZE;
       i += TL866A_FIRMWARE_BLOCK_SIZE - 16) {
    memcpy(data, data_in + i, TL866A_FIRMWARE_BLOCK_SIZE - 16);
    encrypt_block(data, xortable, index);
    memcpy(data_out, data, TL866A_FIRMWARE_BLOCK_SIZE);
    data_out += TL866A_FIRMWARE_BLOCK_SIZE;
    index += 4;
  }
}

// decrypt firmware
static void decrypt_firmware(uint8_t *data_out, const uint8_t *data_in,
                             uint8_t type, uint8_t index) {
  uint32_t i;
  uint8_t data[TL866A_FIRMWARE_BLOCK_SIZE];
  const uint8_t *xortable = (type == MP_TL866A ? a_xortable : cs_xortable);

  for (i = 0; i < TL866A_ENC_FIRMWARE_SIZE; i += TL866A_FIRMWARE_BLOCK_SIZE) {
    memcpy(data, &data_in[i], TL866A_FIRMWARE_BLOCK_SIZE);
    decrypt_block(data, xortable, index);
    memcpy(data_out, data, TL866A_FIRMWARE_BLOCK_SIZE - 16);
    data_out += TL866A_FIRMWARE_BLOCK_SIZE - 16;
    index += 4;
  }
}

// Performing a firmware update
int tl866a_firmware_update(minipro_handle_t *handle, const char *firmware) {
  update_dat_t update_dat;
  uint8_t msg[TL866A_FIRMWARE_BLOCK_SIZE + 7];

  struct stat st;
  if (stat(firmware, &st)) {
    fprintf(stderr, "%s open error!: ", firmware);
    perror("");
    return EXIT_FAILURE;
  }

  // Check the update.dat size
  if (st.st_size != TL866A_UPDATE_DAT_SIZE) {
    fprintf(stderr, "File size error!\n");
    return EXIT_FAILURE;
  }

  // Open the update.dat firmware file
  FILE *file = fopen(firmware, "rb");
  if (file == NULL) {
    fprintf(stderr, "%s open error!: ", firmware);
    perror("");
    return EXIT_FAILURE;
  }

  // Read the update.dat file
  if (fread(&update_dat, sizeof(char), st.st_size, file) != st.st_size) {
    fprintf(stderr, "File read error!\n");
    fclose(file);
    return EXIT_FAILURE;
  }
  fclose(file);

  // Decrypt firmware
  uint32_t i;
  uint8_t a_firmware[TL866A_ENC_FIRMWARE_SIZE];
  uint8_t cs_firmware[TL866A_ENC_FIRMWARE_SIZE];
  for (i = 0; i < TL866A_ENC_FIRMWARE_SIZE; i++) {
    a_firmware[i] = update_dat.a_firmware[i] ^
                    update_dat.a_xortable2[(i + update_dat.a_index) & 0x3FF] ^
                    update_dat.a_xortable1[(i / 80) & 0xFF];
    cs_firmware[i] =
        update_dat.cs_firmware[i] ^
        update_dat.cs_xortable2[(i + update_dat.cs_index) & 0x3FF] ^
        update_dat.cs_xortable1[(i / 80) & 0xFF];
  }

  if ((update_dat.a_crc32 !=
       ~crc32(a_firmware, sizeof(a_firmware), 0xFFFFFFFF)) ||
      (update_dat.cs_crc32 !=
       ~crc32(cs_firmware, sizeof(cs_firmware), 0xFFFFFFFF))) {
    fprintf(stderr, "%s crc error!\n", firmware);
    return EXIT_FAILURE;
  }

  fprintf(stderr, "%s contains firmware version 3.2.%u", firmware,
          update_dat.header[0]);
  if ((handle->firmware & 0xFF) > update_dat.header[0])
    fprintf(stderr, " (older)");
  else if ((handle->firmware & 0xFF) < update_dat.header[0])
    fprintf(stderr, " (newer)");
  fprintf(stderr, "\n");

  uint8_t version;
  printf(
      "\nWhich firmware version do you want to reflash? \n1) Device default "
      "(%s)\n2) "
      "%s\n3) Exit\n",
      handle->version == MP_TL866A ? "A" : "CS",
      handle->version == MP_TL866A ? "CS" : "A");
  char c = getchar();
  switch (c) {
    case '1':
      version = handle->version;
      break;
    case '2':
      version = handle->version == MP_TL866A ? MP_TL866CS : MP_TL866A;
      break;
    default:
      fprintf(stderr, "Firmware update aborted.\n");
      return EXIT_FAILURE;
  }

  // Switch to boot mode if necessary
  if (handle->status == MP_STATUS_NORMAL) {
    fprintf(stderr, "Switching to bootloader... ");
    fflush(stderr);
    if (minipro_reset(handle)) {
      fprintf(stderr, "failed\n");
      return EXIT_FAILURE;
    }

    handle = minipro_open(NULL);
    if (!handle) {
      fprintf(stderr, "failed!\n");
      return EXIT_FAILURE;
    }

    if (handle->status == MP_STATUS_NORMAL) {
      fprintf(stderr, "failed!\n");
      return EXIT_FAILURE;
    }
    fprintf(stderr, "OK\n");
  }

  // Reencrypt the firmware if necessary
  if (version != handle->version) {
    uint8_t data[TL866A_UNENC_FIRMWARE_SIZE];

    // First step: decrypt the desired firmware specified by 'version'
    decrypt_firmware(
        data, version == MP_TL866A ? a_firmware : cs_firmware, version,
        version == MP_TL866A ? update_dat.a_erase : update_dat.cs_erase);
    /*
     Second step: encrypt back the firmware with the true device version key.
     This way we can have CS devices flashed with A firmware and vice versa.
     */
    encrypt_firmware(data,
                     handle->version == MP_TL866A ? a_firmware : cs_firmware,
                     handle->version,
                     handle->version == MP_TL866A ? update_dat.a_erase
                                                  : update_dat.cs_erase);
  }

  // Erase device
  fprintf(stderr, "Erasing... ");
  fflush(stderr);
  memset(msg, 0, sizeof(msg));
  msg[0] = TL866A_BOOTLOADER_ERASE;
  msg[7] =
      handle->version == MP_TL866A ? update_dat.a_erase : update_dat.cs_erase;
  if (msg_send(handle->usb_handle, msg, 20)) {
    fprintf(stderr, "\nErase failed!\n");
    return EXIT_FAILURE;
  }
  memset(msg, 0, sizeof(msg));
  if (msg_recv(handle->usb_handle, msg, 32)) {
    fprintf(stderr, "\nErase failed!\n");
    return EXIT_FAILURE;
  }
  if (msg[0] != TL866A_BOOTLOADER_ERASE) {
    fprintf(stderr, "failed\n");
    return EXIT_FAILURE;
  }

  // Reflash firmware
  fprintf(stderr, "OK\n");
  fprintf(stderr, "Reflashing TL866%s firmware... ",
          version == MP_TL866A ? "A" : "CS");
  fflush(stderr);

  uint32_t address = TL866A_BOOTLOADER_SIZE;
  uint8_t *p_firmware = handle->version == MP_TL866A ? a_firmware : cs_firmware;
  for (i = 0; i < TL866A_ENC_FIRMWARE_SIZE; i += TL866A_FIRMWARE_BLOCK_SIZE) {
    msg[0] = TL866A_BOOTLOADER_WRITE;     // command LSB
    msg[1] = 0x00;                        // command MSB
    msg[2] = TL866A_FIRMWARE_BLOCK_SIZE;  // Block size without header(LSB)
    msg[3] = 0x00;                        // Block size MSB
    msg[4] = address & 0xff;              // 24 bit address
    msg[5] = (address & 0xff00) >> 8;
    msg[6] = (address & 0xff0000) >> 16;
    memcpy(&msg[7], p_firmware + i, TL866A_FIRMWARE_BLOCK_SIZE);

    if (msg_send(handle->usb_handle, msg, sizeof(msg))) {
      fprintf(stderr, "\nReflash... Failed\n");
      return EXIT_FAILURE;
    }
    address += 64;  // next data block
    fprintf(stderr, "\r\e[KReflashing TL866%s firmware...  %2d%%",
            version == MP_TL866A ? "A" : "CS", i / 1935);
    fflush(stderr);
  }
  fprintf(stderr, "\r\e[KReflashing TL866%s firmware... 100%%\n",
          version == MP_TL866A ? "A" : "CS");

  // Switching back to normal mode
  fprintf(stderr, "Resetting device... ");
  fflush(stderr);
  if (minipro_reset(handle)) {
    fprintf(stderr, "failed!\n");
    return EXIT_FAILURE;
  }
  handle = minipro_open(NULL);
  if (!handle) {
    fprintf(stderr, "failed!\n");
    return EXIT_FAILURE;
  }
  fprintf(stderr, "OK\n");
  if (handle->status != MP_STATUS_NORMAL) {
    fprintf(stderr, "Reflash... failed\n");
    return EXIT_FAILURE;
  }

  fprintf(stderr, "Reflash... OK\n");
  return EXIT_SUCCESS;
}

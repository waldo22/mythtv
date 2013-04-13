/* Copyright 2008 Alan Nisota <alannisota@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef R5000_INT_H
#define R5000_INT_H

#include <usb.h>
#include <linux/usbdevice_fs.h>

#define PRINTHEX(str, data, len) if(1) do { \
  int _i; \
  fprintf(stderr, str); \
  for(_i = 0; _i < (len); _i++) { \
    fprintf(stderr, "%02x ", (unsigned char)(data)[_i]); \
    if((_i % 16) == 15) fprintf(stderr, "\n"); \
  } \
  if(_i % 16) fprintf(stderr, "\n"); \
} while(0)

#define R5K_MAX_BUTTON_SIZE 0xF8
#define R5K_MAX_PIDS 10
#define IS_VIDEO(x) ((x) == 0x02 || (x) == 0x1b || (x) == 0x80)

struct r5k_descriptor {
  unsigned char d[10];
};

struct r5k_epid {
  int pid;
  unsigned char id;
  struct r5k_descriptor desc;
};

struct r5000_buttons {
  int len;
  int delay;
  char b0[R5K_MAX_BUTTON_SIZE];
  char b1[R5K_MAX_BUTTON_SIZE];
  char b2[R5K_MAX_BUTTON_SIZE];
  char b3[R5K_MAX_BUTTON_SIZE];
  char b4[R5K_MAX_BUTTON_SIZE];
  char b5[R5K_MAX_BUTTON_SIZE];
  char b6[R5K_MAX_BUTTON_SIZE];
  char b7[R5K_MAX_BUTTON_SIZE];
  char b8[R5K_MAX_BUTTON_SIZE];
  char b9[R5K_MAX_BUTTON_SIZE];
  char clear[R5K_MAX_BUTTON_SIZE];
  char enter[R5K_MAX_BUTTON_SIZE];
  char power[R5K_MAX_BUTTON_SIZE];
  char power_on[R5K_MAX_BUTTON_SIZE];
  char power_off[R5K_MAX_BUTTON_SIZE];
  char guide[R5K_MAX_BUTTON_SIZE];
};

struct r5kdev {
  usb_dev_handle *handle;
  unsigned char serial[8];
  void *urbblk;
  struct usbdevfs_urb **urbs;
  char *buffer;
  int stb_type;
  int read_words;
  void (*process_block)(struct r5kdev *r5kdev, unsigned char *buf, int len);
  void (*start_stream)(struct r5kdev *r5kdev);
  void (*change_channel)(struct r5kdev *r5kdev);
  void *stbdata;
  unsigned int (*cb)(unsigned char *buffer, int len, void *callback_data);
  void *cb_data;
  int nexturb;
  int streaming;
  int bytes_read;
  struct r5000_buttons *button;
  int power_active_low;
  int discrete_power;
  int unreliable_power_detect;
  int channel;
  time_t *last_command_time;
  unsigned char pmt_pkt[188];
  unsigned char num_pmt_entries;
  unsigned char pmt_state;
  unsigned char pmt_next_cc;
  unsigned char pmt_version;
  struct r5k_epid pmt[R5K_MAX_PIDS];
} r5kdev_t;
#define r5kdev_t struct r5kdev

#include "r5000.h"

extern void r5000_print(const char *fmt, ...);
extern unsigned char r5000_pat_pkt[188];
extern struct r5k_descriptor r5000_pmt_audio_desc;
extern struct r5k_descriptor r5000_pmt_video_desc;
extern struct r5k_descriptor r5000_pmt_nowplaying_desc;
extern void r5000_reset_pmt(r5kdev_t *r5kdev);
extern void r5000_send_pmt(r5kdev_t *r5kdev);

//Support for DirecTV and Dish/BEV 6000 boxes
extern void pes_init(r5kdev_t *r5kdev);
extern void pes_free(r5kdev_t *r5kdev);

//Support for ViP series Dish Network boxes
extern void vip_init(r5kdev_t *r5kdev);
extern void vip_free(r5kdev_t *r5kdev);

//Support for HDD and DSR series satellite/cable boxes
extern void sat_init(r5kdev_t *r5kdev);
extern void sat_free(r5kdev_t *r5kdev);

//Buttons
extern struct r5000_buttons vip_button_cmd;
extern struct r5000_buttons directv_button_cmd;
extern struct r5000_buttons dish6000_button_cmd;
extern struct r5000_buttons dsr_button_cmd;
#endif

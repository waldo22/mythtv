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

#ifndef R5000_H
#define R5000_H

#ifndef r5kdev_t
#define r5kdev_t void
#endif

#define R5K_MAX_DEVS 10
typedef struct {
  unsigned char serial[R5K_MAX_DEVS][8];
  int count;
} r5kenum_t;

typedef enum {
  R5K_STB_VIP211 = 0,
  R5K_STB_DIRECTV,
  R5K_STB_HDD,
  R5K_STB_DSR,
  R5K_STB_VIP622,
  R5K_STB_DISH6000,
  R5K_STB_MAX,
} r5ktype_t;

extern int r5000_init(void (*_msgcb)(char *str));
extern r5kdev_t *r5000_open(r5ktype_t type, unsigned int (*cb)(unsigned char *buffer, int len, void *callback_data), void *cb_data, const char *serial);
extern int r5000_close(r5kdev_t *r5kdev);
extern int r5000_start_stream(r5kdev_t *r5kdev);
extern int r5000_stop_stream(r5kdev_t *r5kdev);
extern int r5000_loop_iterate(r5kdev_t *r5kdev, int timeout_usec);
extern int r5000_get_power_state(r5kdev_t *r5kdev);
extern int r5000_toggle_on_off(r5kdev_t *r5kdev);
extern int r5000_power_on_off(r5kdev_t *r5kdev, int turn_on);
extern int r5000_change_channel(r5kdev_t *r5kdev, const char *chan, int mpeg_prog);
extern int r5000_find_stbs(r5kenum_t *devs);
extern void r5000_get_fw_version(r5kdev_t *r5kdev, char *fwver);
#endif //R5000_H

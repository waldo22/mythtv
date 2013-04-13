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

#include <stdio.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <time.h>

#ifdef R5K_DEBUG
char strmfile[256] = "/tmp/strm";
int fd = -1;
#endif

#ifdef R5K_RAWUSB
int usbfd = -1;
#endif
#include "r5000_internal.h"
#include "r5000init.h"

#define R5K_WARM_VID 0x0547
#define R5K_WARM_PID 0x1002

#define MAX_URBS_IN_FLIGHT 128

static struct {
  char serial[8];
  time_t time;
} last_cmd_time[10] = {
  {{0}, 0}, {{0}, 0}, {{0}, 0}, {{0}, 0}, {{0}, 0},
  {{0}, 0}, {{0}, 0}, {{0}, 0}, {{0}, 0}, {{0}, 0},
};

  
#define R5K_UNRELIABLE_TIMEOUT (5 * 60)
#define R5K_BUTTON_LEN(_r, _b) \
	((_r)->button->len ? (_r)->button->len \
                           : ((unsigned char)(_b)[1]) + 6 )
#ifndef USE_ISOCHRONOUS
  //BULK
  #define R5K_URB_BUFFER_SIZE (1 << 14)
#else
  //ISOCHRONOUS
  #define LIBUSB_AUGMENT
  #define R5K_URB_BUFFER_SIZE (1 << 15)
#endif
#include "libusb_augment.h"

static int r5000_usb_init = 0;
static struct {
  unsigned char serial[R5K_MAX_DEVS][8];
  struct usb_device *dev[R5K_MAX_DEVS];
  int count;
} r5000_dev_map;

static void (*msgcb)(char *msg);

enum {
  R5K_PMT_START = 0x01,
  R5K_PMT_READY = 0x02
};

long r5klog_count = 0;
FILE *r5klog_fh = NULL;
struct timeval r5klog_tv;
char r5klog_msg[2048];

#ifdef R5000_TRACE
void r5000_log(int flush, const char *fmt, ...)
{
  struct timeval tv, tv1;
  va_list args;
  char logmsg[2048];

  if(! r5klog_fh) {
    int oldmask = umask(0);
    r5klog_fh = fopen("/tmp/r5000_trace.log", "w+");
    umask(oldmask);
  }

  va_start(args,fmt);
  vsnprintf(logmsg,sizeof(logmsg),fmt,args);
  va_end(args);

  if (strncmp(logmsg, r5klog_msg, sizeof(logmsg)) == 0) {
    r5klog_count++;
    return;
  }
  gettimeofday(&tv, NULL);
  timersub(&tv, &r5klog_tv, &tv1);
  r5klog_tv = tv;
  if(r5klog_count > 1) {
    fprintf(r5klog_fh, "%ld.%06ld: Repeated %ld times\n", (long)tv1.tv_sec, (long)tv1.tv_usec, r5klog_count);
    printf("%ld.%06ld: Repeated %ld times\n", (long)tv1.tv_sec, (long)tv1.tv_usec, r5klog_count);
  }
  fprintf(r5klog_fh, "%ld.%ld: %s\n", (long)tv1.tv_sec, (long)tv1.tv_usec, logmsg);
  printf("%ld.%ld: %s\n", (long)tv1.tv_sec, (long)tv1.tv_usec, logmsg);
  memcpy(r5klog_msg, logmsg, sizeof(logmsg));
  r5klog_count = 1;
  if(flush) {
    fflush(r5klog_fh);
  }
}
#else
void r5000_log(int flush, const char *fmt, ...)
{
}
#endif
 
void r5000_print(const char *fmt, ...)
{
  va_list args;
  char logmsg[1024];
  va_start(args,fmt);
  vsnprintf(logmsg,sizeof(logmsg),fmt,args);
  va_end(args);
  if(msgcb) {
    //msgcb routine must ensure thread safeness
    msgcb(logmsg);
  } else {
    fprintf(stderr, "%s", logmsg);
  }
}

int r5000_create_urbs(r5kdev_t *r5kdev)
{
  int i, urbsize;
#ifndef USE_ISOCHRONOUS
  urbsize = sizeof(struct usbdevfs_urb);
#else
  urbsize = sizeof(struct usbdevfs_urb) + sizeof(struct usb_iso_packet_desc) * R5K_URB_BUFFER_SIZE / 1024;
#endif

  r5kdev->buffer = (char *)malloc(R5K_URB_BUFFER_SIZE * MAX_URBS_IN_FLIGHT);
  r5kdev->urbs = (struct usbdevfs_urb **)malloc(sizeof(struct usbdevfs_urb *) * MAX_URBS_IN_FLIGHT);
  r5kdev->urbblk = (void *)malloc(urbsize * MAX_URBS_IN_FLIGHT);
  for(i = 0; i < MAX_URBS_IN_FLIGHT; i++) {
    r5kdev->urbs[i] = (struct usbdevfs_urb *)((unsigned long)r5kdev->urbblk + (urbsize * i));
#ifndef USE_ISOCHRONOUS
    usb_bulk_setup(r5kdev->urbs[i], 0x82, r5kdev->buffer + (R5K_URB_BUFFER_SIZE*i), R5K_URB_BUFFER_SIZE);
#else
    usb_isochronous_setup(r5kdev->urbs[i], 0x82, 1024, r5kdev->buffer + (R5K_URB_BUFFER_SIZE*i), R5K_URB_BUFFER_SIZE);
#endif
  }
  r5kdev->nexturb = 0;
  return 1;
}

int r5000_free_urbs(r5kdev_t *r5kdev)
{
  free(r5kdev->urbblk);
  free(r5kdev->urbs);
  free(r5kdev->buffer);
  return 0;
}

usb_dev_handle *r5000_locate_device(
    unsigned short vendor_id, unsigned short product_id, int skip)
{
  struct usb_bus *bus;
  struct usb_device *dev;
  usb_dev_handle *device_handle = 0;
  usb_find_busses();
  usb_find_devices();

  for (bus = usb_get_busses(); bus && !device_handle; bus = bus->next)
  {
    for (dev = bus->devices; dev && !device_handle; dev = dev->next)
    {
      if (dev->descriptor.idVendor == vendor_id &&
          dev->descriptor.idProduct == product_id)
      {
        device_handle = usb_open(dev);
        if(device_handle && skip) {
          usb_close(device_handle);
          device_handle = NULL;
          skip--;
        }
      }
    }
  }

  if (device_handle) {
    int open_status = usb_set_configuration(device_handle,1);

    open_status = usb_claim_interface(device_handle,0);

    open_status = usb_set_altinterface(device_handle,0);
  }
  return (device_handle);
}

int r5000_dev_init(r5kdev_t *r5kdev) {
  int i, bytes;
  char *ptr;
  unsigned char *serial = r5kdev->serial;
  char datain[0x80];
  bytes = usb_bulk_read(r5kdev->handle, 129, datain, sizeof(datain), 5000);
  for(i = 0; i < R5K_INIT_SERIAL; i++) {
    //PRINTHEX("Write:\n", r5kinit[i].data, r5kinit[i].wlen);
    if(r5kinit[i].wsleep) usleep(r5kinit[i].wsleep);
    usb_bulk_write(r5kdev->handle, 1, r5kinit[i].data, r5kinit[i].wlen, 5000);
    if(r5kinit[i].rsleep) usleep(r5kinit[i].rsleep);
    bytes = usb_bulk_read(r5kdev->handle, 129, datain, sizeof(datain), 5000);
    //PRINTHEX("Read:\n", datain, bytes);
    if(r5kinit[i].rlen > 0 && bytes != r5kinit[i].rlen) {
      r5000_print("R5000 initialization failed at stage %d:\n\tExpected %d bytes, but got %d bytes\n", i, r5kinit[i].rlen, bytes);
      return 0;
    }
  }

  //last read is serial #
  if (datain[0] != 0x08) {
    r5000_print("R5000 initialization failed reading serial #\n");
    return 0;
  }
  for(ptr = datain + 6; ptr < datain + 13; ptr++) {
    *serial++ = ( *ptr >= '0' && *ptr <= 'z' ) ? *ptr : '*';
  }
  *serial = 0;

  //complete initialization now
  for(; i < R5K_INIT_MAX; i++) {
    //PRINTHEX("Write:\n", r5kinit[i].data, r5kinit[i].wlen);
    if(r5kinit[i].wsleep) usleep(r5kinit[i].wsleep);
    usb_bulk_write(r5kdev->handle, 1, r5kinit[i].data, r5kinit[i].wlen, 5000);
    if(r5kinit[i].rsleep) usleep(r5kinit[i].rsleep);
    bytes = usb_bulk_read(r5kdev->handle, 129, datain, sizeof(datain), 5000);
    //PRINTHEX("Read:\n", datain, bytes);
    if(r5kinit[i].rlen > 0 && bytes != r5kinit[i].rlen) {
      r5000_print("R5000 initialization failed at stage %d:\n\tExpected %d bytes, but got %d bytes\n", i, r5kinit[i].rlen, bytes);
      return 0;
    }
  }
  return 1;
}

int r5000_start_stream(r5kdev_t *r5kdev)
{
  char data[0x80];
  int bytes;
  int i;
  struct usb_dev_handle *handle = (struct usb_dev_handle *)r5kdev->handle;

  //*r5kdev->last_command_time = time(NULL);
  r5000_log(0, "r5000_start_stream(%p)", r5kdev);
  if(! r5kdev->urbs)
    r5000_create_urbs(r5kdev);

  data[0] = 0x30;
  usb_bulk_write(handle, 1, data, 1, 5000);
  bytes = usb_bulk_read(handle, 129, data, 2, 5000);

  //0x50 sets byte mode.  Use '0x60' to set word mode
  data[0] = r5kdev->read_words ? 0x60: 0x50;
  usb_bulk_write(handle, 1, data, 1, 5000);

  for(i=0; i < MAX_URBS_IN_FLIGHT; i++) {
    usb_urb_submit(handle, r5kdev->urbs[i], NULL);
  }
  r5kdev->nexturb = 0;
  r5kdev->streaming = 1;
  if(r5kdev->start_stream)
    r5kdev->start_stream(r5kdev);
  return 1;
}

int r5000_stop_stream(r5kdev_t *r5kdev)
{
  struct usb_dev_handle *handle = (struct usb_dev_handle *)r5kdev->handle;
  struct usbdevfs_urb **urbs = r5kdev->urbs;
  int i;

  //*r5kdev->last_command_time = time(NULL);
  r5000_log(0, "r5000_stop_stream(%p)", r5kdev);
  if(r5kdev->streaming) {
    for(i=0; i < MAX_URBS_IN_FLIGHT; i++)
      usb_urb_cancel(handle, urbs[i]);
    r5kdev->streaming = 0;
  }
  return 1;
}

/* r5000_init must be called from a thread-safe context */
int r5000_init(void (*_msgcb)(char *str))
{
  int bus_count, dev_count;
  r5kdev_t r5kd;
  int count = 0;

  r5000_log(0, "r5000_init()");

  msgcb = _msgcb;

  if(r5000_usb_init)
    return 1;
  usb_init();
  bus_count = usb_find_busses();
  dev_count = usb_find_devices();
  if(bus_count == 0 || dev_count ==0) {
    r5000_print("R5000 failed to locate any USB devices.  Are you sure you have permissions set properly?\n");
    return 0;
  }
  while(r5000_dev_map.count < R5K_MAX_DEVS) {
    r5kd.handle = r5000_locate_device(R5K_WARM_VID, R5K_WARM_PID, count);
    if(! r5kd.handle)
      break;
    if (r5000_dev_init(&r5kd)) {
      memcpy(r5000_dev_map.serial[r5000_dev_map.count], r5kd.serial, 8);
      r5000_dev_map.dev[r5000_dev_map.count] = usb_device(r5kd.handle);
      r5000_dev_map.count++;
    }
    count++;
    usb_close(r5kd.handle);
  }
  if(! r5000_dev_map.count) {
    r5000_print("R5000 failed to locate any R5000 devices.  Are you sure you have permissions set properly?\n");
    return 0;
  }
  r5000_usb_init = 1;
  return 1;
}

r5kdev_t *r5000_open(r5ktype_t type,
                     unsigned int (*cb)(unsigned char *buffer, int len, void *callback_data),
                     void *cb_data,
                     const char *serial)
{
  r5kdev_t *r5kdev, r5kd;
  int count = 0;

  r5000_log(0, "r5000_open(%d, NULL, %p, %s)", type, cb_data, serial);
  memset(&r5kd, 0, sizeof(r5kdev_t));

  if(! r5000_usb_init) {
    r5000_print("R5000 was not initialized before r5000_open().  Please call r5000_init() first\n");
    return NULL;
  }
  for(count = 0; count < r5000_dev_map.count; count++) {
    if(! serial || memcmp(r5000_dev_map.serial[count], serial, 8) == 0) {
      r5kd.handle = usb_open(r5000_dev_map.dev[count]);
      if(! r5kd.handle)
        return NULL;
      usb_set_configuration(r5kd.handle,1);
      usb_claim_interface(r5kd.handle,0);
      usb_set_altinterface(r5kd.handle,0);
      if(! r5000_dev_init(&r5kd)) {
        usb_close(r5kd.handle);
        return NULL;
      }
      break;
    }
  }
  if(count == r5000_dev_map.count) {
    //We can't get here unless a serial was specified
    r5000_print("Could not locate R5000 device with serial '%s'\n", serial);
    return NULL;
  }
#ifdef R5K_DEBUG
  printf("Reading stream file: %s\n", strmfile);
  fd = open(strmfile, O_RDONLY);
#endif
#ifdef R5K_RAWUSB
  usbfd = open("raw.av", O_WRONLY | O_CREAT | O_TRUNC, 0666);
#endif
  r5kdev = (r5kdev_t *)malloc(sizeof(r5kdev_t));
  *r5kdev = r5kd;
  r5kdev->urbs = NULL;
  r5kdev->cb = cb;
  r5kdev->cb_data = cb_data;
  r5kdev->stb_type = type;

  r5kdev->last_command_time = &last_cmd_time[0].time;
  for(count = 0; count < 10; count++) {
    if (last_cmd_time[count].serial[0] == 0) {
      memcpy(last_cmd_time[count].serial, r5kdev->serial, 8);
      r5kdev->last_command_time = &last_cmd_time[count].time;
      break;
    } else if(memcmp(last_cmd_time[count].serial, r5kdev->serial, 8) == 0) {
      r5kdev->last_command_time = &last_cmd_time[count].time;
      break;
    }
  }

  switch(type) {
    case R5K_STB_VIP211:
    case R5K_STB_VIP622:
      vip_init(r5kdev);
      break;
    case R5K_STB_DIRECTV:
    case R5K_STB_DISH6000:
      pes_init(r5kdev);
      break;
    case R5K_STB_DSR:
    case R5K_STB_HDD:
      sat_init(r5kdev);
      break;
    default:
      r5000_print("Unknown STB type %d specified.\n", type);
      r5kdev->stb_type = R5K_STB_VIP211;
      vip_init(r5kdev);
      break;
  }
  {
    char fw[4];
    r5000_get_fw_version(r5kdev, fw);
    r5000_print("R5000 firmware version for %s: %s\n", r5kdev->serial, fw);
  }
  return r5kdev;
}

int r5000_close(r5kdev_t *r5kdev)
{
  r5000_log(1, "r5000_close(%p)", r5kdev);
  if(! r5kdev)
    return 1;
  if(r5kdev->urbs) {
    if(r5kdev->streaming)
      r5000_stop_stream(r5kdev);
    r5000_free_urbs(r5kdev);
  }
  usb_close(r5kdev->handle);
#ifdef R5K_RAWUSB
  if(usbfd >= 0) close(usbfd);
#endif
  switch(r5kdev->stb_type) {
    case R5K_STB_VIP211:
    case R5K_STB_VIP622:
      vip_free(r5kdev);
      break;
    case R5K_STB_DIRECTV:
    case R5K_STB_DISH6000:
      pes_free(r5kdev);
      break;
    case R5K_STB_DSR:
    case R5K_STB_HDD:
      sat_free(r5kdev);
      break;
  }
  free(r5kdev);
  return 1;
}

int r5000_loop_iterate(r5kdev_t *r5kdev, int timeout_usec)
{
  struct usb_dev_handle *handle = (struct usb_dev_handle *)r5kdev->handle;
  struct usbdevfs_urb **urbs = r5kdev->urbs;
  int len;
  char *buf;

  r5000_log(0, "r5000_loop_iterate(%p, %d)", r5kdev, timeout_usec);
  if(! r5kdev->streaming)
    return -1;
  len = usb_urb_reap(handle, urbs[r5kdev->nexturb], timeout_usec);
  if(len <= 0) {
    if(len != -ETIMEDOUT)
      r5000_print("(%d) Reap failed at %08x: %s\n", r5kdev->nexturb, r5kdev->bytes_read, strerror(errno));
    return len;
  }
  buf = r5kdev->buffer + (R5K_URB_BUFFER_SIZE*r5kdev->nexturb);
#ifdef R5K_RAWUSB
  if(usbfd >= 0) write(usbfd, buf, len);
#endif
#ifdef R5K_DEBUG
  if(fd >= 0) {
    int newlen = read(fd, buf, len);
    if(newlen < len) {
      r5000_print("hit end of debug file\n");
      lseek(fd, 0, SEEK_SET);
      read(fd, buf + newlen, len - newlen);
    }
  }
#endif
  r5kdev->process_block(r5kdev, (unsigned char *)buf, len);
  r5kdev->bytes_read += len;
  usb_urb_submit(handle, urbs[r5kdev->nexturb], NULL);
  r5kdev->nexturb = (r5kdev->nexturb + 1) % MAX_URBS_IN_FLIGHT;
  if(r5kdev->nexturb == 0) {
    *r5kdev->last_command_time = time(NULL);
  }
  return 0;
}

//use this to read a status frame.  It doesn't do anything special
//but makes it obvious what data is expected
int r5000_clear_status(r5kdev_t *r5kdev, char *buf)
{
  int i = 5;
  int ret;

  while (i--) {
      ret = usb_bulk_read(r5kdev->handle, 129, buf, 128, 5000);
      if (ret > 2 && (unsigned char)buf[0] == 0xcc && buf[1] == 0x00)
          return ret;
      buf[0] = 0xcc;
      buf[1] = 0x00;
      r5000_log(0, "r5000_clear_status overflow detected: %d %02x, %02x", ret, (unsigned int)buf[0], (unsigned int)buf[1]);
      usb_bulk_write(r5kdev->handle, 1, buf, 2, 5000);
      usleep(100000);
  }
  return ret;
}

#ifdef USE_OLD_POWER_CHECK
int r5000_get_power_state(r5kdev_t *r5kdev)
{
  unsigned char data1[1]  = { 0x30 };
  unsigned char data2[0x80];
  int count = 10;
  while(count--) {
    usb_bulk_read(r5kdev->handle, 129, (char *)data2, 128, 5000);
    usb_bulk_write(r5kdev->handle, 1, (char *)data1, 1, 5000);
    usleep(100000);
    if(usb_bulk_read(r5kdev->handle, 1, (char *)data2, 2, 5000) == 2 &&
       data2[0] == 0x0a && (data2[1] & 0x4e) == 0x4c) {
      // The following boxes are known to be power active low:
      // 4DTV 922, Dish 622/722, Bell 9242, D*
      if(r5kdev->power_active_low)
        return (!(data2[1] == 0x4d));
      else
        return (!!(data2[1] == 0x4d));
    }
    usleep(100000);
  }
  r5000_print("R5000 failed to read power state.  Assuming ON state\n");
  return 1;
}

#else
int r5000_get_power_state(r5kdev_t *r5kdev)
{
  unsigned char data2[0x80];
  int count = 10;
  int len;

  r5000_log(0, "r5000_get_power_state(%p)", r5kdev);

  while(count--) {
    data2[0] = 0x00;
    len = usb_bulk_read(r5kdev->handle, 129, (char *)data2, 128, 5000);
    //r5000_print("%d: %02x %02x %02x\n", len, data2[0], data2[1], data2[2]);
    usleep(100000);
    if (data2[0] == 0xcc) {
      // The following boxes are known to be power active low:
      // 4DTV 922, Dish 622/722, Bell 9242, D*
      if(r5kdev->power_active_low)
        return (!(data2[2] & 0x01));
      else
        return (!!(data2[2] & 0x01));
    }
  }
  r5000_print("R5000 failed to read power state.  Assuming ON state\n");
  return 1;
}
#endif

int r5000_send_pwr_cmd(r5kdev_t *r5kdev, char *data, int send_clear)
{
  r5000_get_power_state(r5kdev);
  if (send_clear) {
    usb_bulk_write(r5kdev->handle, 1, r5kdev->button->clear,
                   R5K_BUTTON_LEN(r5kdev, r5kdev->button->clear),
                   5000);
    usleep(r5kdev->button->delay);
    usleep(r5kdev->button->delay);
  }
  usb_bulk_write(r5kdev->handle, 1, data,
                 R5K_BUTTON_LEN(r5kdev, data),
                 5000);
  usleep(r5kdev->button->delay);
  return 1;
}

int r5000_wait_pwr(r5kdev_t *r5kdev, int on_off)
{
  int new_state, count = 20;
  while(count-- && (new_state = r5000_get_power_state(r5kdev)) != on_off)
    ;
  //r5000_print("End state: %s\n", !on_off ? "On" : "Off");
  return new_state;
}

int r5000_toggle_on_off(r5kdev_t *r5kdev)
{
  unsigned on_off;

  r5000_log(0, "r5000_toggle_on_off(%p)", r5kdev);

  on_off = r5000_get_power_state(r5kdev);
  if(! r5kdev->button) {
    r5000_print("No button IR commands defined for this device!\n");
    return on_off;
  }
  //r5000_print("Start state: %s\n", on_off ? "On" : "Off");
  r5000_send_pwr_cmd(r5kdev, r5kdev->button->power, 1);
  on_off = r5000_wait_pwr(r5kdev, ! on_off);
  //r5000_print("End state: %s\n", on_off ? "On" : "Off");
  return on_off;
}

int r5000_power_on_off(r5kdev_t *r5kdev, int turn_on)
{
  char *pwr_command;
  int on_off;
  int wait_time = 0;

  r5000_log(0, "r5000_power_on_off(%p, %d)", r5kdev, turn_on);

  on_off = r5000_get_power_state(r5kdev);

  if(! r5kdev->button) {
    r5000_print("No button IR commands defined for this device!\n");
    return on_off;
  }

  if(r5kdev->discrete_power) {
    //r5000_print("Using discrete power commands\n");
    if(turn_on) {
      pwr_command = r5kdev->button->power_on;
    } else {
      pwr_command = r5kdev->button->power_off;
    }
  } else {
    pwr_command = r5kdev->button->power;
  }

  if(r5kdev->unreliable_power_detect && on_off &&
     (time(NULL) - *r5kdev->last_command_time > R5K_UNRELIABLE_TIMEOUT)) {
    //The VIP211 can not be relied upon to be on when it says it is
    // But 'off' is trustworthy
    // We assume that if we've recorded or changed channel in the past 5 mins, that we can trust the state
    printf("Potential unreliable power state detected.  Trying to ensure power state!\n");
    if(r5kdev->discrete_power) {
      wait_time = 2500000;
    } else {
      //We can't trustthe 'on' state, so toggle it off then back on to be sure
      r5000_send_pwr_cmd(r5kdev, pwr_command, turn_on != 2);
      r5000_wait_pwr(r5kdev, 0);
    }
  } else if(on_off == !!turn_on) {
      return on_off;
  }

  r5000_send_pwr_cmd(r5kdev, pwr_command, turn_on != 2);
  if(wait_time) 
    usleep(wait_time);
  return r5000_wait_pwr(r5kdev, !!turn_on);
}

int r5000_change_channel(r5kdev_t *r5kdev, const char *chan, int mpeg_prog)
{
  char data2[0x80];
  const char *ptr = NULL;
  const char *p;
  const char ignore[1] = {0xfe};

  r5000_log(0, "r5000_change_channel(%p, %s, %d)", r5kdev, chan, mpeg_prog);
  printf("Got channel change command: %s (%d)\n", chan, mpeg_prog);

  if(! r5kdev)
    return 0;
  if (chan) {
    if(! r5kdev->button) {
      r5000_print("No button IR commands defined for this device!\n");
    } else {
      r5000_clear_status(r5kdev, data2);
      if(r5kdev->button->clear[0] == 0x00) {
        usb_bulk_write(r5kdev->handle, 1, r5kdev->button->clear,
                       R5K_BUTTON_LEN(r5kdev, r5kdev->button->clear),
                       5000);
        usleep(900000);
      }
      //usleep(r5kdev->button->delay);
      //usleep(r5kdev->button->delay);
      for(p = chan; *p; p++) {
        printf("Sending button '%c'\n", *p);
        ptr = NULL;
        switch(*p) {
          case '0' : ptr = r5kdev->button->b0; break;
          case '1' : ptr = r5kdev->button->b1; break;
          case '2' : ptr = r5kdev->button->b2; break;
          case '3' : ptr = r5kdev->button->b3; break;
          case '4' : ptr = r5kdev->button->b4; break;
          case '5' : ptr = r5kdev->button->b5; break;
          case '6' : ptr = r5kdev->button->b6; break;
          case '7' : ptr = r5kdev->button->b7; break;
          case '8' : ptr = r5kdev->button->b8; break;
          case '9' : ptr = r5kdev->button->b9; break;
          case 'P' : ptr = ignore; r5000_power_on_off(r5kdev, 2); break;
        }
        if(ptr && ptr[0] == 0x00) {
          //r5000_clear_status(r5kdev, data2);
          usb_bulk_write(r5kdev->handle, 1, ptr,
                         R5K_BUTTON_LEN(r5kdev, ptr),
                         5000);
        } else if(ptr != ignore) {
          r5000_print("No button information found for %c\n", *p);
        }
        usleep(r5kdev->button->delay);
      }
      //r5000_clear_status(r5kdev, data2);
      if(r5kdev->button->enter[0] == 0x00) {
        usb_bulk_write(r5kdev->handle, 1, r5kdev->button->enter,
                       R5K_BUTTON_LEN(r5kdev,r5kdev->button->enter),
                       5000);
      }
    }
  }
  r5kdev->channel =  (mpeg_prog > 0xffff) ? -(mpeg_prog & 0xffff) : mpeg_prog;
  if(r5kdev->change_channel)
    r5kdev->change_channel(r5kdev);
  *r5kdev->last_command_time = time(NULL);
  return 1;
}

int r5000_find_stbs(r5kenum_t *devs)
{
  r5000_log(0, "r5000_find_stbs(%p)", devs);
  devs->count = 0;
  if(! r5000_usb_init) {
    r5000_print("R5000 was not initialized before r5000_find_stbs().  Please call r5000_init() first\n");
    return 0;
  }
  devs->count = r5000_dev_map.count;
  memcpy(devs->serial, r5000_dev_map.serial, 8 * r5000_dev_map.count);
  return 1;
}

void r5000_get_fw_version(r5kdev_t *r5kdev, char *fwver)
{
  char buf[128];

  r5000_clear_status(r5kdev, buf);

  buf[0]=0x20;
  buf[1]=0x00;
  usb_bulk_write(r5kdev->handle, 1, buf, 2, 5000);
  usleep(100000);
  usb_bulk_read(r5kdev->handle, 129, buf, 4, 5000);
  if ((unsigned char)buf[0] == 0xdd) {
    fwver[0] = buf[1];
    fwver[1] = buf[2];
    fwver[2] = buf[3];
  } else {
    fwver[0] = 'x';
    fwver[1] = '.';
    fwver[2] = 'x';
  }
  fwver[3] = 0;
}

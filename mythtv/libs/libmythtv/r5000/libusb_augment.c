/* Copyright 2008 Alan Nisota <alannisota@gmail.com>
 * 2005-10-19/lindi: downloaded from http://www.gaesi.org/~nmct/cvista/cvista/
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

// libusb_augment.c
// $Revision$
// $Date$

// Hopefully, the functions in this file will become part of libusb.

#include <stdio.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/poll.h>
#include <usb.h>
#include <linux/usbdevice_fs.h>
#include <string.h>
#include <signal.h>
#define LIBUSB_AUGMENT
#include "libusb_augment.h"

// Taken from libusb file usbi.h because usb.h
// hides the definition of usb_dev_handle.
extern int usb_debug;

struct usb_dev_handle {
  int fd;

  struct usb_bus *bus;
  struct usb_device *device;

  int config;
  int interface;
  int altsetting;

  /* Added by RMT so implementations can store other per-open-device data */
  void *impl_info;
};

// Taken from libusb file error.h to supply error handling macro definition.
typedef enum {
  USB_ERROR_TYPE_NONE = 0,
  USB_ERROR_TYPE_STRING,
  USB_ERROR_TYPE_ERRNO,
} usb_error_type_t;

extern char usb_error_str[1024];
extern usb_error_type_t usb_error_type;

#define USB_ERROR_STR(format, args...) \
        do { \
          usb_error_type = USB_ERROR_TYPE_STRING; \
          snprintf(usb_error_str, sizeof(usb_error_str) - 1, format, ## args); \
          if (usb_debug >= 2) \
            fprintf(stderr, "USB error: %s\n", usb_error_str); \
        } while (0)

static int urb_signr = 0;
void (*urb_completion_callback)(struct usbdevfs_urb *) = NULL;
#define USB_ASYNC_COMPLETION_SIGNAL (SIGRTMIN + 5)

void urb_completion_handler(int signum, siginfo_t *info, void *context)
{
   struct usbdevfs_urb *urb = (struct usbdevfs_urb *)info->si_addr;
   struct usbdevfs_urb *context1;
   usb_dev_handle *dev = (usb_dev_handle *)urb->usercontext;
   int ret;
   if (info->si_code != SI_ASYNCIO ||
       info->si_signo != USB_ASYNC_COMPLETION_SIGNAL) {
       return;
   }
   if(info->si_errno != 0) {
     USB_ERROR_STR("Async URB Completion failed: %s", strerror(info->si_errno));
     return;
   }
   ret = ioctl(dev->fd, USBDEVFS_REAPURB, &context1);
   if(ret  < 0) {
     USB_ERROR_STR("Failed to read URB: %s", strerror(-ret));
     return;
   }
   if(context1 != urb) {
     USB_ERROR_STR("Reaped unexpected urb");
     return;
   }
   if(urb_completion_callback)
     urb_completion_callback(urb);
}

int usbdevfs_urb_signal_completion(void (*cb)( struct usbdevfs_urb *))
{
  urb_completion_callback = cb;
  urb_signr = USB_ASYNC_COMPLETION_SIGNAL;
  struct sigaction usb_linux_sa;
  usb_linux_sa.sa_sigaction = urb_completion_handler;
  sigfillset(&usb_linux_sa.sa_mask);
  usb_linux_sa.sa_flags = SA_SIGINFO;
  usb_linux_sa.sa_flags |= SA_ONSTACK;
  sigaction(USB_ASYNC_COMPLETION_SIGNAL, &usb_linux_sa, NULL);
  return 0;
}

struct usbdevfs_urb *usb_bulk_setup(
                struct usbdevfs_urb *iso_urb, // URB pointer-pointer.
                unsigned char ep,  // Device endpoint.
                char *bytes,       // Data buffer pointer.
                int size) {        // Size of the buffer.
  struct usbdevfs_urb *local_urb;

  // No more than 16384 bytes can be transferred at a time.
  if (size > 16384) {
    USB_ERROR_STR("error on transfer size: %s", strerror(EINVAL));
    return NULL;
  }
  local_urb = iso_urb;
  if (!local_urb) {
    local_urb = (struct usbdevfs_urb *) calloc(1, sizeof(struct usbdevfs_urb));
    if (!local_urb) {
      USB_ERROR_STR("error on packet size: %s", strerror(EINVAL));
      return NULL;
    }
  }
  local_urb->type = USBDEVFS_URB_TYPE_BULK;
  local_urb->endpoint = ep;
  local_urb->status = 0;
  local_urb->flags = 0;
  local_urb->buffer = bytes;
  local_urb->buffer_length = size;
  local_urb->actual_length = 0;
  local_urb->start_frame = 0;
  local_urb->number_of_packets = 0;
  local_urb->error_count = 0;
  local_urb->signr = urb_signr;
  local_urb->usercontext = (void *) 0;
  return local_urb;
}
// Reading and writing are the same except for the endpoint
struct usbdevfs_urb *usb_isochronous_setup(
                          struct usbdevfs_urb *iso_urb, // URB pointer-pointer.
                          unsigned char ep,  // Device endpoint.
                          int pktsize,       // Endpoint packet size.
                          char *bytes,       // Data buffer pointer.
                          int size) {        // Size of the buffer.
  struct usbdevfs_urb *local_urb;
  // int ret
  // was unused /lindi
  int pktcount, fullpkts, partpktsize, packets, urb_size;

  // No more than 32768 bytes can be transferred at a time.
  if (size > 32768) {
    USB_ERROR_STR("error on transfer size: %s", strerror(EINVAL));
    return NULL;
  }

  // Determine the number of packets that need to be created based upon the
  // amount of data to be transferred, and the maximum packet size of the
  // endpoint.

  // Find integral number of full packets.
  //fprintf(stderr, "buf size: %d\n", size);
  //fprintf(stderr, "iso size: %d\n", pktsize);
  fullpkts = size / pktsize;
  //fprintf(stderr, "Number of full packets: %d\n", fullpkts);
  // Find length of partial packet.
  partpktsize = size % pktsize;
  //fprintf(stderr, "Size of partial packet: %d\n", partpktsize);
  // Find total number of packets to be transfered.
  packets = fullpkts + ((partpktsize > 0) ? 1 : 0);
  //fprintf(stderr, "Total number of packets: %d\n", packets);
  // Limit the number of packets transfered according to
  // the Linux usbdevfs maximum read/write buffer size.
  if ((packets < 1) || (packets > 128)) {
    USB_ERROR_STR("error on packet size: %s", strerror(EINVAL));
    return NULL;
  }

  // If necessary, allocate the urb and packet
  // descriptor structures from the heap.
  local_urb = iso_urb;
  if (!local_urb) {
    urb_size = sizeof(struct usbdevfs_urb) +
      packets * sizeof(struct usb_iso_packet_desc);
    local_urb = (struct usbdevfs_urb *) calloc(1, urb_size);
    if (!local_urb) {
      USB_ERROR_STR("error on packet size: %s", strerror(EINVAL));
      return NULL;
    }
  }

  // Set up each packet for the data to be transferred.
  for (pktcount = 0; pktcount < fullpkts; pktcount++) {
    local_urb->iso_frame_desc[pktcount].length = pktsize;
    local_urb->iso_frame_desc[pktcount].actual_length = 0;
    local_urb->iso_frame_desc[pktcount].status = 0;
  }

  // Set up the last packet for the partial data to be transferred.
  if (partpktsize > 0) {
    local_urb->iso_frame_desc[pktcount].length = partpktsize;
    local_urb->iso_frame_desc[pktcount].actual_length = 0;
    local_urb->iso_frame_desc[pktcount++].status = 0;
  }

  // Set up the URB structure.
  local_urb->type = USBDEVFS_URB_TYPE_ISO;
  //fprintf(stderr, "type: %d\n", local_urb->type);
  local_urb->endpoint = ep;
  //fprintf(stderr, "endpoint: 0x%x\n", local_urb->endpoint);
  local_urb->status = 0;
  local_urb->flags = USBDEVFS_URB_ISO_ASAP; // Additional flags here?
  //fprintf(stderr, "flags: %d\n", local_urb->flags);
  local_urb->buffer = bytes;
  //fprintf(stderr, "buffer: 0x%x\n", local_urb->buffer);
  local_urb->buffer_length = size;
  //fprintf(stderr, "buffer_length: %d\n", local_urb->buffer_length);
  local_urb->actual_length = 0;
  local_urb->start_frame = 0;
  //fprintf(stderr, "start_frame: %d\n", local_urb->start_frame);
  local_urb->number_of_packets = pktcount;
  //fprintf(stderr, "number_of_packets: %d\n", local_urb->number_of_packets);
  local_urb->error_count = 0;
  local_urb->signr = 0;
  //fprintf(stderr, "signr: %d\n", local_urb->signr);
  local_urb->usercontext = (void *) 0;
  return local_urb;
}


int usb_urb_submit(usb_dev_handle *dev,     // Open usb device handle.
                   struct usbdevfs_urb *iso_urb,        // Pointer to URB.
                   struct timeval *tv_submit) { // Time structure pointer.
  int ret;

  iso_urb->usercontext = dev;
  // Get actual time, of the URB submission.
  if(tv_submit)
    gettimeofday(tv_submit, NULL);
  // Submit the URB through an IOCTL call.
  ret = ioctl(dev->fd, USBDEVFS_SUBMITURB, iso_urb);
  //fprintf(stderr, "start_frame now: %d\n", iso_urb->start_frame);
  //fprintf(stderr, "submit ioctl return value: %d\n", ret);
  if (ret < 0) {
    //fprintf(stderr, "error submitting URB: %s\n", strerror(errno));
    USB_ERROR_STR("error submitting URB: %s", strerror(errno));
    return -errno;
  }
  return ret;
}


int usb_urb_reap(usb_dev_handle *dev,     // Open usb device handle.
                 struct usbdevfs_urb *iso_urb,        // Pointer to URB.
                 int timeout) {           // Attempt timeout (usec).
  void *context = NULL;
  int ret;
  struct pollfd ufd[1];

  // Get actual time, and add the timeout value. The result is the absolute
  // time where we have to quit waiting for an isochronous message.
  ufd[0].fd = dev->fd;
  ufd[0].events = POLLIN | POLLOUT;
  ufd[0].revents = 0;
  ret = poll(ufd, 1, timeout);
  if(ret <= 0)
    return -ETIMEDOUT;

  //fprintf(stderr, "preparing to reap\n");
  ret = ioctl(dev->fd, USBDEVFS_REAPURB, &context);

  /*
   * If there was an error, that wasn"t EAGAIN (no completion), then
   * something happened during the reaping and we should return that
   * error now
   */
  //fprintf(stderr, "reap ioctl return value: %d\n", ret);
  if (ret < 0) {
    USB_ERROR_STR("error reaping interrupt URB: %s",
                  strerror(errno));
    return -errno;
  }

  //fprintf(stderr, "actual_length: %d\n", iso_urb->actual_length);
  //fprintf(stderr, "URB status: %d\n", iso_urb->status);
  //fprintf(stderr, "error count: %d\n", iso_urb->error_count);

  //fprintf(stderr, "waiting done\n");
  if(context != NULL && iso_urb != context) {
    fprintf(stderr, "Expected urb: %p but got %p\n", iso_urb, context);
    return -1;
  }
  //fprintf(stderr, "Total bytes: %d\n", bytesdone);
  return iso_urb->actual_length;
}

int usb_urb_cancel(usb_dev_handle *dev, struct usbdevfs_urb *iso_urb)
{
  return ioctl(dev->fd, USBDEVFS_DISCARDURB, iso_urb);
}

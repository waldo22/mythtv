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

#ifdef LIBUSB_AUGMENT
// Taken from libusb file linux.h to provide the URB structure definitions.
struct usb_iso_packet_desc {
  unsigned int length;
  unsigned int actual_length;
  unsigned int status;
};
#endif

int usbdevfs_urb_signal_completion(void (*cb)( struct usbdevfs_urb *));
struct usbdevfs_urb *usb_bulk_setup(struct usbdevfs_urb *iso_urb, unsigned char ep,
                   char *bytes, int size);
struct usbdevfs_urb *usb_isochronous_setup(struct usbdevfs_urb *iso_urb,
                    unsigned char ep, int pktsize, char *bytes, int size);
int usb_urb_submit(usb_dev_handle *dev, struct usbdevfs_urb *iso_urb,
                   struct timeval *tv_rsubmit);
int usb_urb_reap(usb_dev_handle *dev, struct usbdevfs_urb *iso_urb,
                 int timeout_usec);
int usb_urb_cancel(usb_dev_handle *dev, struct usbdevfs_urb *iso_urb);

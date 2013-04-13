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

//Support for Dish Network ViP211/ViP422 boxes
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include "r5000_internal.h"

struct r5000_vip {
  unsigned char leftover[188];
  int offset;
  int pos;
};
#define CHECK_CONTINUITY 1
#ifdef CHECK_CONTINUITY
  static unsigned char continuity[0x2000];
#endif

#define PMT_READY(_r5kdev) (((_r5kdev)->pmt_state == 0x03) || ((_r5kdev)->channel < 0 && (_r5kdev)->pmt_state == 0x02))

#define IS_PRIVATE_SECTION(x) (x == 0x05)

enum {
  R5K_STREAMTYPE_VIDEO,
  R5K_STREAMTYPE_AUDIO,
  R5K_STREAMTYPE_PRIVATE,
};
  
static struct timeval last_tv;
void vip_add_pmt(r5kdev_t *r5kdev, int pid, int table_id)
{
  int i;
  struct r5000_vip *vip = (struct r5000_vip *)r5kdev->stbdata;

  int streamtype = IS_VIDEO(table_id) ? R5K_STREAMTYPE_VIDEO : (IS_PRIVATE_SECTION(table_id) ? R5K_STREAMTYPE_PRIVATE : R5K_STREAMTYPE_AUDIO);

  for(i = 0; i < r5kdev->num_pmt_entries; i++) {
    if(r5kdev->pmt[i].pid == pid) {
      if(r5kdev->pmt[i].id == table_id) {
        if (streamtype == R5K_STREAMTYPE_VIDEO && r5kdev->pmt_state == 0x03) {
          //If we haven't seen a PAT or PMT in 500ms, send one
          struct timeval tv, tv_test;
          gettimeofday(&tv, NULL);
          timersub(&tv, &last_tv, &tv_test);
          if(tv.tv_sec > 0 || tv.tv_usec > 500 * 1000) {
            r5kdev->cb(r5000_pat_pkt, 188, r5kdev->cb_data);
            r5000_send_pmt(r5kdev);
            last_tv = tv;
          }
        }
        return;
      }
      //different table_id for existing pid.  Reset
      r5000_reset_pmt(r5kdev);
      break;
    }
    if(streamtype == R5K_STREAMTYPE_VIDEO) {
      if(IS_VIDEO(r5kdev->pmt[i].id)) {
        //a video entry already exists.  Reset
        r5000_reset_pmt(r5kdev);
        break;
      }
    }
  }
  //Didn't find this PID, add it
  if(i == R5K_MAX_PIDS)
    return;
  gettimeofday(&last_tv, NULL);
  r5000_print("Adding %04x: %02x @ %08x\n", pid, table_id, r5kdev->bytes_read + vip->pos);
  r5kdev->pmt[r5kdev->num_pmt_entries].pid = pid;
  r5kdev->pmt[r5kdev->num_pmt_entries].id  = table_id;
  switch(streamtype) {
    case R5K_STREAMTYPE_VIDEO:
      r5kdev->pmt[r5kdev->num_pmt_entries].desc = r5000_pmt_video_desc;
      r5kdev->pmt_state |= 0x01;
      break;
    case R5K_STREAMTYPE_AUDIO:
      r5kdev->pmt[r5kdev->num_pmt_entries].desc = r5000_pmt_audio_desc;
      r5kdev->pmt_state |= 0x02;
      break;
    case R5K_STREAMTYPE_PRIVATE:
      r5kdev->pmt[r5kdev->num_pmt_entries].desc = r5000_pmt_nowplaying_desc;
      break;
  }
  r5kdev->num_pmt_entries++;
  //Force rebuild of PMT
  r5kdev->pmt_pkt[0] = 0;
}

void vip_force_pmt(r5kdev_t *r5kdev, unsigned char *buf)
{
  int stream_id, pid, afc, af_size = 0;

  pid = ((buf[1] << 8) | buf[2]) & 0x1fff;
  if(pid == 0x1fff)
    return;
#ifdef CHECK_CONTINUITY
  if(pid >= 0x1000) {
    struct r5000_vip *vip = (struct r5000_vip *)r5kdev->stbdata;
    unsigned char cont = buf[3] & 0x0f;
    if(continuity[pid] != cont && (buf[3] & 0x30) != 0x20) {
      r5000_print("Continuity (0x%04x) %d != %d @%08x\n",
                  pid, cont, continuity[pid], r5kdev->bytes_read + vip->pos);
    }
    continuity[pid] = (cont + 1) & 0x0f;
  }
#endif
  if(pid == 0) {
    gettimeofday(&last_tv, NULL);
    r5kdev->cb(r5000_pat_pkt, 188, r5kdev->cb_data);
    if(PMT_READY(r5kdev))
      r5000_send_pmt(r5kdev);
  } else {
    if(pid != 0x21)
      r5kdev->cb(buf, 188, r5kdev->cb_data);
    //Only interested in PES packets starting in this TS packet
    if (!(buf[1] & 0x40))
      return;
    afc = (buf[3]>>4) & 0x03;
    if(afc == 0x2)
      return;
    if(afc == 0x3)
      af_size = buf[4]+1;
    if(buf[4+af_size] != 0x00 || buf[5+af_size] != 0x00 || buf[6+af_size] != 0x01)
      return;
    //We have a PES packet
    stream_id = buf[7+af_size];
    if((stream_id & 0xf0) == 0xe0) {
      //Video stream (we need the adaptation field)
      if(0) {
        PRINTHEX("data", buf, af_size+8);
      }
      if(afc == 0x03) {
        if(buf[5] & 0x02) {
          // Has private data, this is MPEG4
          vip_add_pmt(r5kdev, pid, 0x1b);
        } else { //if(buf[5] & 0x10) {
          // Has PCR and no private data, this is MPEG2
          vip_add_pmt(r5kdev, pid, 0x02);
        }
      } else {
        // No PCR, maybe this is ATSC?
        vip_add_pmt(r5kdev, pid, 0x02);
      }
    } else if((stream_id & 0xf0) == 0xc0) {
      //Audio stream
      vip_add_pmt(r5kdev, pid, 0x04);
    } else if(stream_id  == 0xbd) {
      //Audio stream
      vip_add_pmt(r5kdev, pid, 0x81);
    }
  }
}

static void vip_process_block(r5kdev_t *r5kdev, unsigned char *buf, int len)
{
  struct r5000_vip *vip = (struct r5000_vip *)r5kdev->stbdata;

  int sync = 1;
  if(! r5kdev->streaming)
    return;
  if(len <= 0)
    return;

  vip->pos = vip->offset;
  while(vip->pos < len) {
      if(buf[vip->pos] != 0x47 || (vip->pos <len-1 && buf[vip->pos+1] == 0xff))
        goto nosync;
      // If we get here, buf[vip->pos] == 0x47
      if(vip->offset) {
        //previous data exists and is part of a good packet
        memcpy(vip->leftover+188-vip->offset, buf, vip->offset);
        vip_force_pmt(r5kdev, vip->leftover);
        vip->offset = 0;
      }
      if(vip->pos+188 <= len) {
        //at least one full packet is available
        if(vip->pos+188 < len && buf[vip->pos+188] != 0x47)
          goto nosync;
      } else {
        //Out of data, but the partial packet may be ok.
        memcpy(vip->leftover, buf+vip->pos, len-vip->pos);
        vip->offset = 188-(len-vip->pos);
        break;
      }
      //If we get here, we have a good packet
      vip_force_pmt(r5kdev, buf+vip->pos);
      if(! sync)
        r5000_print("(%d) Found sync @ %08x\n", r5kdev->nexturb, r5kdev->bytes_read+vip->pos);
      sync = 1;
      vip->pos+=188;
      continue;
  nosync:
      vip->offset=0;
      if(sync)
        r5000_print("(%d)Lost sync @ %08x\n", r5kdev->nexturb, r5kdev->bytes_read+vip->pos);
      sync = 0;
      vip->pos++;
  }
}

static void vip_start_stream(r5kdev_t *r5kdev)
{
  struct r5000_vip *vip = (struct r5000_vip *)r5kdev->stbdata;
  vip->offset = 0;
}

static void vip_change_channel(r5kdev_t *r5kdev)
{
  r5000_reset_pmt(r5kdev);
}

void vip_init(r5kdev_t *r5kdev)
{
  struct r5000_vip *vip = (struct r5000_vip *)malloc(sizeof(struct r5000_vip));
  r5kdev->stbdata = vip;
  r5kdev->process_block = vip_process_block;
  r5kdev->start_stream  = vip_start_stream;
  r5kdev->button = &vip_button_cmd;
  r5kdev->change_channel = &vip_change_channel;
  r5kdev->discrete_power = 1;
  if(r5kdev->stb_type == R5K_STB_VIP211)
    r5kdev->unreliable_power_detect = 1;
  if(r5kdev->stb_type == R5K_STB_VIP622)
    r5kdev->power_active_low = 1;
}

void vip_free(r5kdev_t *r5kdev)
{
  free(r5kdev->stbdata);
}


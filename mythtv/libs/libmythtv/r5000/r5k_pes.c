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

//Support for DirecTV boxes
#include <netinet/in.h>
#include <string.h>

#include "r5000_internal.h"

#define DTV_VPID 0x1322
#define DTV_APID 0x1333
#define DTV_PAT_PMT_COUNT 5000

struct r5000_dtv {
  unsigned char vbuf[188*2+4];
  unsigned char abuf[188*2+6];
  unsigned char *video;
  unsigned char *audio;
  unsigned int vpos;
  unsigned int apos;
  unsigned int vstart;
  unsigned int astart;
  unsigned int video_cc;
  unsigned int audio_cc;
  unsigned int vstate;
  unsigned int pic_pos;
  int alen;
  unsigned int pat_pmt_count;
};

//The following are used to convert DirectTV into TS format
static int directv_make_empty_pes(unsigned char *ptr, unsigned char type)
{
  ptr[0] = 0x00;
  ptr[1] = 0x00;
  ptr[2] = 0x01;
  ptr[3] = type;
  ptr[4] = 0x00;
  ptr[5] = 0x00;
  ptr[6] = 0x80;
  ptr[7] = 0x00; //2 MSB represent PTS/DTS flag
  ptr[8] = 0x0a;
  ptr[9]  = 0xff;
  ptr[10] = 0xff;
  ptr[11] = 0xff;
  ptr[12] = 0xff;
  ptr[13] = 0xff;
  ptr[14] = 0xff;
  ptr[15] = 0xff;
  ptr[16] = 0xff;
  ptr[17] = 0xff;
  ptr[18] = 0xff;
  return 19;
}

static void directv_make_pespts(unsigned char *outptr, unsigned int pts)
{
        pts = htonl(pts);
        unsigned char *inpts = (unsigned char *)&pts;
        outptr[0] = 0x01 |
                    ((inpts[0] & 0xC0) >>5);
        outptr[1] = ((inpts[0] & 0x3F) << 2) |
                    ((inpts[1] & 0xC0) >> 6);
        outptr[2] = 0x01 | ((inpts[1] & 0x3F) << 2) |
                    ((inpts[2] & 0x80) >> 6);
        outptr[3] = ((inpts[2] & 0x7F) << 1) |
                    ((inpts[3] & 0x80) >> 7);
        outptr[4] = 0x01 | ((inpts[3] & 0x7F) << 1);
}

static void directv_update_video_pes(unsigned char *ptr, int pos)
{
  //pos points at the 1st char after a pic start code
  int picture_coding_type;
  int hdr_len;
  unsigned int pts1, dts1;
  unsigned char *buf = ptr + pos;
  picture_coding_type = (buf[1] >> 3) & 0x07;
  hdr_len = (picture_coding_type > 1) ? 5 : 4;
  if(buf[hdr_len + 3] == 0xb5)
    hdr_len += 9;
  if(buf[hdr_len + 3] == 0xb2) {
    pts1 = ((buf[hdr_len+6] & 0x03)   << 30) +
           ((buf[hdr_len+7] & 0x7f) << 23) +
           ((buf[hdr_len+8])          << 15) +
           ((buf[hdr_len+9] & 0x7f) << 8) +
           buf[hdr_len+10];
    dts1 = ((buf[hdr_len+13] & 0x03)   << 30) +
           ((buf[hdr_len+14] & 0x7f) << 23) +
           ((buf[hdr_len+15])          << 15) +
           ((buf[hdr_len+16] & 0x7f) << 8) +
           buf[hdr_len+17];
    //NOTE:  This is wrong.  DSS timestamps only have a resolution of 2^32/300
    //r5000_print("pts: %08x/%f dts: %08x/%f\n", pts1, pts1 / 27000000.0, dts1, dts1 / 27000000.0);
    ptr[7] |= 0xc0;
    directv_make_pespts(ptr+9, pts1/300);
    ptr[9] |= 0x30;
    directv_make_pespts(ptr+14, dts1/300);
    ptr[14] |= 0x10;
  }
}

static void directv_fix_audio_pts(unsigned char *ptr)
{
  unsigned int pts = ((ptr[0] & 0x06) << 29) +
                     (ptr[1] << 22) +
                     ((ptr[2] & 0xfe) << 14) +
                     (ptr[3] << 7) +
                     (ptr[4] >> 1);
  directv_make_pespts(ptr, pts/300);
  ptr[0] |= 0x20;
}


static void pes_write_ts(r5kdev_t *r5kdev, unsigned char *ptr, int len, int pid, int start, unsigned int *cc)
{
  int stuff = 184 - len;
  //Note:  we know that there are 188 bytes allocated before 'ptr'
  //       that we can use for the header
  if(stuff > 0) {
    int stuff1 = stuff;
    while(stuff1 > 2) {
      *--ptr = 0xff;
      stuff1--;
    }
    if(stuff1 == 2) {
      *--ptr = 0x00;
    }
    *--ptr = stuff - 1;
    *--ptr = 0x30 | *cc;
  } else {
    *--ptr = 0x10 | *cc;
  }
  *--ptr = pid & 0xff;
  *--ptr = (start << 6) | (pid >> 8);
  *--ptr = 0x47;
  r5kdev->cb(ptr, 188, r5kdev->cb_data);
  *cc = (*cc+1) & 0x0f;
}

static void pes_process_block(r5kdev_t *r5kdev, unsigned char *buf, int len)
{
  int i;
  struct r5000_dtv *dtv = (struct r5000_dtv *)r5kdev->stbdata;
  for(i = 0; i < len; i+=2) {
    unsigned char data = buf[i];
    unsigned char type = buf[i+1];
    if(0xff == type) {
      //video
      dtv->video[dtv->vpos++] = data;
      if(dtv->vpos > 4 && dtv->video[dtv->vpos-2] == 0x01 &&
         dtv->video[dtv->vpos-3] == 0x00 && dtv->video[dtv->vpos-4] == 0x00) {
        if (data == 0xe0) {
          //HD video header (PES)
          pes_write_ts(r5kdev, dtv->video, dtv->vpos - 4, DTV_VPID, dtv->vstart, &dtv->video_cc);
          dtv->pat_pmt_count++;
          dtv->video[0] = 0x00;
          dtv->video[1] = 0x00;
          dtv->video[2] = 0x01;
          dtv->video[3] = 0xe0;
          dtv->vpos = 4;
          dtv->vstart = 1;
          dtv->vstate = data;
        } else if (r5kdev->stb_type == R5K_STB_DIRECTV &&
                   (data == 0xb3 || data == 0x00)) {
          if (dtv->vstate == 0xff) {
            dtv->vstate = data;
            pes_write_ts(r5kdev, dtv->video, dtv->vpos - 4, DTV_VPID, dtv->vstart, &dtv->video_cc);
            dtv->pat_pmt_count++;
            //Create a PES header, but no PTS/DTS info yet so just use stuffing bytes
            dtv->vpos = directv_make_empty_pes(dtv->video, 0xe0);
            dtv->video[dtv->vpos++] = 0x00;
            dtv->video[dtv->vpos++] = 0x00;
            dtv->video[dtv->vpos++] = 0x01;
            dtv->video[dtv->vpos++] = data;
            dtv->vstart = 1;
            dtv->pic_pos = dtv->vpos;
          }
        }
      }
      if(dtv->vpos == 188) {
        if (dtv->vstate == 0x00)
          //We found pic frame without a PES header (SD)
          directv_update_video_pes(dtv->video, dtv->pic_pos);
        pes_write_ts(r5kdev, dtv->video, 184, DTV_VPID, dtv->vstart, &dtv->video_cc);
        dtv->pat_pmt_count++;
        dtv->video[0] = dtv->video[184];
        dtv->video[1] = dtv->video[185];
        dtv->video[2] = dtv->video[186];
        dtv->video[3] = dtv->video[187];
        dtv->vpos = 4;
        dtv->vstart = 0;
        dtv->vstate = 0xff;
      }
    } else if(0xfe == type) {
      //audio
      dtv->audio[dtv->apos++] = data;
      dtv->alen--;
      if(dtv->alen <= 0 && dtv->apos > 6 && dtv->audio[dtv->apos-3] == 0xbd && dtv->audio[dtv->apos-4] == 0x01 &&
         dtv->audio[dtv->apos-5] == 0x00 && dtv->audio[dtv->apos-6] == 0x00) {
        dtv->alen = (dtv->audio[dtv->apos-2] << 8) + data;
        pes_write_ts(r5kdev, dtv->audio, dtv->apos - 6, DTV_APID, dtv->astart, &dtv->audio_cc);
        dtv->pat_pmt_count++;
        dtv->audio[0] = 0x00;
        dtv->audio[1] = 0x00;
        dtv->audio[2] = 0x01;
        dtv->audio[3] = 0xbd;
        dtv->audio[4] = dtv->audio[dtv->apos-2];
        dtv->audio[5] = data;
        dtv->apos = 6;
        dtv->astart = 1;
        r5kdev->pmt[1].id = 0x81; //AC3
      } else if(dtv->alen <= 0 && dtv->apos > 6 && dtv->audio[dtv->apos-3] == 0xc0 && dtv->audio[dtv->apos-4] == 0x01 &&
           dtv->audio[dtv->apos-5] == 0x00 && dtv->audio[dtv->apos-6] == 0x00) {
        dtv->alen = (dtv->audio[dtv->apos-2] << 8) + data;
        pes_write_ts(r5kdev, dtv->audio, dtv->apos - 6, DTV_APID, dtv->astart, &dtv->audio_cc);
        dtv->pat_pmt_count++;
        dtv->audio[0] = 0x00;
        dtv->audio[1] = 0x00;
        dtv->audio[2] = 0x01;
        dtv->audio[3] = 0xc0;
        if (r5kdev->stb_type == R5K_STB_DIRECTV) {
          dtv->audio[4] = (dtv->alen + 3) >> 8;
          dtv->audio[5] = (dtv->alen + 3) & 0xff;
          dtv->audio[6] = 0x80;
          dtv->audio[7] = 0x80;
          dtv->audio[8] = 0x05;
          dtv->apos = 9;
        } else {
          dtv->audio[4] = dtv->audio[dtv->apos-2];
          dtv->audio[5] = data;
          dtv->apos = 6;
        }
        dtv->astart = 1;
        r5kdev->pmt[1].id = 0x04; //MP2
      } else if(dtv->apos == 190) {
        if(dtv->astart && dtv->audio[3] == 0xc0 && r5kdev->stb_type == R5K_STB_DIRECTV)
          directv_fix_audio_pts(dtv->audio+9);
        pes_write_ts(r5kdev, dtv->audio, 184, DTV_APID, dtv->astart, &dtv->audio_cc);
        dtv->pat_pmt_count++;
        dtv->audio[0] = dtv->audio[184];
        dtv->audio[1] = dtv->audio[185];
        dtv->audio[2] = dtv->audio[186];
        dtv->audio[3] = dtv->audio[187];
        dtv->audio[4] = dtv->audio[188];
        dtv->audio[5] = dtv->audio[189];
        dtv->apos = 6;
        dtv->astart = 0;
      }
    }
  }
  if(r5kdev->pmt[1].id && dtv->pat_pmt_count > DTV_PAT_PMT_COUNT) {
    r5kdev->cb(r5000_pat_pkt, 188, r5kdev->cb_data);
    r5000_send_pmt(r5kdev);
    dtv->pat_pmt_count = 0;
  }
}

static void pes_start_stream(r5kdev_t *r5kdev)
{
  struct r5000_dtv *dtv = (struct r5000_dtv *)r5kdev->stbdata;
  dtv->vstart = 0;
  dtv->astart = 0;
  dtv->vpos = 0;
  dtv->apos = 0;
  dtv->video_cc = 0;
  dtv->audio_cc = 0;
  dtv->vstate = 0xff;
  dtv->pat_pmt_count = DTV_PAT_PMT_COUNT;
}

void pes_init(r5kdev_t *r5kdev)
{
  struct r5000_dtv *dtv = (struct r5000_dtv *)calloc(1, sizeof(struct r5000_dtv));
  dtv->video = dtv->vbuf + 188;
  dtv->audio = dtv->abuf + 188;
  dtv->pat_pmt_count = DTV_PAT_PMT_COUNT;
  dtv->vstate = 0xff;
  r5kdev->stbdata = dtv;
  r5kdev->pmt[0].id = 0x02; //MPEG2
  r5kdev->pmt[0].pid = DTV_VPID;
  r5kdev->pmt[0].desc = r5000_pmt_video_desc;
  r5kdev->pmt[1].id = 0x00;
  r5kdev->pmt[1].pid = DTV_APID;
  r5kdev->pmt[1].desc = r5000_pmt_audio_desc;
  r5kdev->num_pmt_entries = 2;
  r5kdev->process_block = pes_process_block;
  r5kdev->start_stream  = pes_start_stream;
  if(r5kdev->stb_type == R5K_STB_DIRECTV) {
    r5kdev->button = &directv_button_cmd;
    r5kdev->power_active_low = 1;
  } else {
    r5kdev->button = &dish6000_button_cmd;
    r5kdev->power_active_low = 0;
    r5kdev->discrete_power = 1;
  }
  r5kdev->read_words = 1;
}

void pes_free(r5kdev_t *r5kdev)
{
  free(r5kdev->stbdata);
}

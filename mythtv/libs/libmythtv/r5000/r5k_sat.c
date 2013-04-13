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
#include "r5000_internal.h"

#define DTV_PAT_PMT_COUNT 5000
#define MAX_PKT_SIZE 272
#define MAX_PAT_SECTIONS 1
#define MAX_SIDS 200
#define MAX_EPIDS 5

struct pat {
  int version;
  unsigned char last_section;
  unsigned char section_seen[MAX_PAT_SECTIONS];
  unsigned int crc[MAX_PAT_SECTIONS];
  unsigned int pmts[MAX_SIDS];
  int pmt_count;
};

struct sids {
  unsigned short sid;
  unsigned int crc;
  unsigned char seen_name;
  struct r5k_epid epid[MAX_EPIDS];
  unsigned char epid_count;
};

struct r5000_sat {
  unsigned char buf[MAX_PKT_SIZE];
  unsigned int pos;
  int offset;
  unsigned char sync;
  unsigned char bytesize;
  unsigned char sync_byte;
  unsigned int has_sync_byte;
  unsigned int pat_pmt_count;
  unsigned int packet_size;
  unsigned int allowed_packet_size[8];

  unsigned int current_sid;
  struct pat pats;
  struct sids *sids;
};

static int sat_read_pat_pkt(unsigned char *pes, struct pat *pat, unsigned int size) {
  unsigned int sec, end, crc, current_next;
  int version;
  unsigned char *ptr, last_sec;

  if (pes[0] != 0x00) {
    r5000_print("read_pat: expected PAT table 0x00 but got 0x%02x\n", pes[0]);
    return -1;
  }
  end = (((pes[1] & 0x03) << 8 | pes[2]) + 3 - 4);
  if(end > size-4) {
    r5000_print("read_pat: invalid PAT table size (%d > %d)\n", end, size-4);
    return -1;
  }
  crc = (pes[end]<<24) | (pes[end+1]<<16) | (pes[end+2]<<8) | pes[end+3];
  version = (pes[5] >> 1) & 0x1f;
  current_next = pes[5] & 0x01;
  sec = pes[6];
  last_sec = pes[7];
  if(! current_next)
    //ignore inactive PAT
    return 0;
  if(last_sec >= MAX_PAT_SECTIONS) {
    r5000_print("read_pat: illegal section count %d > %d\n",
             last_sec, MAX_PAT_SECTIONS);
    return -1;
  }
  if (pat->version != version || last_sec != pat->last_section ||
      pat->crc[sec] != crc) {
    pat->version = version;
    pat->last_section = last_sec;
    pat->pmt_count = 0;
    memset(pat->section_seen, 0, sizeof(pat->section_seen));
  }
  if(pat->section_seen[sec])
    return 0;
  pat->crc[sec] = crc;
  pat->section_seen[sec] = 1;
  for(ptr = pes + 8; ptr < pes + end; ptr += 4) {
    int sid, pid;
    sid = (ptr[0] << 8) | ptr[1];
    pid = ((ptr[2] & 0x1F) << 8) | ptr[3];
    r5000_print("found PID: %04x for sid: %d\n", pid, sid);
    if(sid != 0) {
      pat->pmts[pat->pmt_count++] = (sid << 16) | pid;
    }
  }
  return 1;
}

static struct sids *sat_read_pmt_pkt(unsigned char *buf, struct sids *sids,
                                     unsigned int size) {
  //
  // NOTE we aren't using last_sec here yet!
  //

  struct sids *sidptr = sids;
  unsigned int count, skip, pos, crc, current_next;
  int sid, sec, last_sec, pcrpid, epid, type;
  if (buf[0] != 0x02) {
    r5000_print("read_pmt expected table 0x02 but got 0x%02x\n", buf[0]);
    return NULL;
  }
  count = (((buf[1] & 0x03) << 8) | buf[2]) + 3 - 4;
  sid = (buf[3] << 8) | buf[4];
  current_next = buf[5] & 0x01;
  crc = (buf[count]<<24) | (buf[count+1]<<16) | (buf[count+2]<<8) | buf[count+3];
  sec = buf[6];
  last_sec = buf[7];
  pcrpid = ((buf[8] & 0x1F) << 8) | buf[9];
  skip = ((buf[10] & 0x03) << 8) | buf[11];
  if(skip > count - 12 || count > size) {
    r5000_print("skip: %d > count: %d - 12 || count > size: %d\n",
           skip, count, size);
    return NULL;
  }
  if(! current_next) {
    r5000_print("read_pmt ignoring future PMT\n");
    return NULL;
  }
  while(sidptr->sid != 0) {
    if(sidptr->sid == sid)
      break;
    sidptr++;
  }
  if(sidptr->sid == 0) {
    // We weren't expecting this sid
    r5000_print("read_pmt found unexpected sid: %d\n", sidptr->sid);
    return sidptr;
  }
  if(sidptr->crc == crc) {
    //sid is unchanged
    //r5000_print("read_pmt found identical crc (%08x) for %d\n", crc, sidptr->sid);
    return NULL;
  }
  memset(sidptr, 0, sizeof(struct sids));

  r5000_print("read_pmt: sid: %d pcrpid: %d skip: %d count: %d\n", sid, pcrpid, skip, count);
  sidptr->sid = sid;
  sidptr->crc = crc;
  sidptr->epid_count = 0;
  for(pos = 12 + skip; pos < count;) {
    struct r5k_epid *pidptr = &sidptr->epid[sidptr->epid_count];
    type = buf[pos];
    epid = ((buf[pos+1] & 0x1F) << 8) | buf[pos+2];
    skip = ((buf[pos+3] & 0x03) << 8) | buf[pos+4];
    pidptr->pid = epid;
    pidptr->id  = type == 0x80 ? 0x02 : type;
    memcpy(pidptr->desc.d, buf + pos + 4, skip + 1);
    sidptr->epid_count++;
    r5000_print("read_pmt: epid %04x (type %02x, skip=%d) mapped to sid %d\n", epid, type, skip, sid);
    pos += 5 + skip;
  }
  return sidptr;
}

void sat_find_chname(unsigned char *buf, struct sids *sids, unsigned int size) {
  struct sids *sidptr = sids;
  unsigned int count;
  int sid;
  unsigned char str[20], *strptr = str;
  while(size) {
    count = (((buf[1] & 0x03) << 8) | buf[2]) + 3;
    if(buf[0] == 0x41) {
      buf += count;
      size -= count;
      continue;
    }
    if(buf[0] == 0xc1) {
      unsigned char *ptr = buf+16;
      sid = (buf[7]<<8) | buf[8];
      while(sidptr->sid != 0) {
        if(sidptr->sid == sid)
          break;
        sidptr++;
      }
      if(sidptr->sid == 0) {
        // We weren't expecting this sid
        return;
      }
      if(sidptr->seen_name)
        return;
      sidptr->seen_name = 1;
      while(*ptr >= 0x20 && *ptr < 0x7b && (strptr-str) < (int)(sizeof(str)-1)) {
        *strptr++ = *ptr++;
      }
      *strptr = 0;
      r5000_print("sid: %d is channel %s\n", sid, str);
      return;
    }
    return;
  }
}

int sat_add_pmt(r5kdev_t *r5kdev, struct r5k_epid *epid)
{
  int i;
  int is_video = IS_VIDEO(epid->id);

  for(i = 0; i < r5kdev->num_pmt_entries; i++) {
    if(r5kdev->pmt[i].pid == epid->pid) {
      if(r5kdev->pmt[i].id == epid->id)
        return 0;
      //different table_id for existing pid.  Reset
      r5000_reset_pmt(r5kdev);
      break;
    }
    if(is_video) {
      if(IS_VIDEO(r5kdev->pmt[i].id)) {
        //a video entry already exists.  Reset
        r5000_reset_pmt(r5kdev);
        break;
      }
    }
  }
  //Didn't find this PID, add it
  if(i == R5K_MAX_PIDS)
    return 0;
  r5000_print("Adding %04x: %02x (desc_len=%d) @ %08x\n", epid->pid, epid->id, epid->desc.d[0], r5kdev->bytes_read);
  r5kdev->pmt[r5kdev->num_pmt_entries++] = *epid;
  r5kdev->pmt_state |= is_video ? 0x01 : 0x02;
  return 1;
}

void sat_process_ts(r5kdev_t *r5kdev, unsigned char *buf)
{
  struct r5000_sat *sat = (struct r5000_sat *)r5kdev->stbdata;
  int pid;
  int i, j, k;
  pid = ((buf[1] << 8) | buf[2]) & 0x1fff;
  if(pid == 0x1fff)
    return;
  if(pid == 0 && (buf[1] & 0x40)) {
    //Always read PAT in case we changed transponders
    if(sat_read_pat_pkt(buf+buf[4]+5, &sat->pats, 188-buf[4]-5) > 0) {
      r5000_print("Found new PAT\n");
      if(sat->pats.pmt_count) {
        sat->sids = (struct sids *)realloc(sat->sids, sizeof(struct sids)*(sat->pats.pmt_count+1));
        memset(sat->sids, 0, sizeof(struct sids)*(sat->pats.pmt_count+1));
        //Pre load sids so we don't overrun if the PMT changes before the PAT
        for(i = 0; i < sat->pats.pmt_count; i++) {
          sat->sids[i].sid = sat->pats.pmts[i]>>16;
        }
        sat->sids[i].sid = 0;
        r5000_reset_pmt(r5kdev);
      }
    } else if(r5kdev->pmt_state == 0x03) {
      r5kdev->cb(r5000_pat_pkt, 188, r5kdev->cb_data);
      r5000_send_pmt(r5kdev);
      sat->pat_pmt_count = 0;
    }
    return;
  }
  //Always reread PMT in case the pids have changed
  for(i = 0; i < sat->pats.pmt_count; i++) {
    if(pid == (unsigned short)(sat->pats.pmts[i])) {
#if 0
    {
      char s[80];
      FILE *fh;
      sprintf(s, "0x%04x.ts", pid);
      fh= fopen(s, "a");
      fwrite(buf, 188, 1, fh);
      fclose(fh);
    }
#endif
      //r5000_print("PID: %04x - %04x (%d) %02x %02x %02x %02x %02x\n", pid, (unsigned short)(sat->pats.pmts[i]), i, buf[1], buf[2], buf[3], buf[4], buf[5]);
      if(buf[1] & 0x40 && buf[4]+5 < 188) {
        if(buf[buf[4]+5] == 0x02) {
          struct sids *sidptr;
          if((sidptr = sat_read_pmt_pkt(buf+buf[4]+5, sat->sids, 188-buf[4]-5))) {
            if(sidptr->sid == 0) {
              //Unexpected sid, reset PAT;
              memset(&sat->pats, 0, sizeof(struct pat));
            } else if(sat->current_sid == sidptr->sid) {
              r5000_reset_pmt(r5kdev);
              sat->current_sid = 0;
            }
          }
        //} else if(buf[buf[4]+5] == 0xc1 || buf[buf[4]+5] == 0x41) {
        //  sat_find_chname(buf+buf[4]+5, sat->sids, 188-buf[4]-5);
        }
      }
      return;
    }
    if(! sat->current_sid) {
      for(j = 0; j < sat->sids[i].epid_count; j++) {
        if(pid == sat->sids[i].epid[j].pid &&
           IS_VIDEO(sat->sids[i].epid[j].id) &&
           (!r5kdev->channel || r5kdev->channel == sat->sids[i].sid)) {
          //Found unencrypted video.  Choose this one
          // Assume this is MPEG2.  Don't know what MPEG4 looks like yet
          sat->current_sid = sat->sids[i].sid;
          r5000_print("Found decrypted sid %d\n", sat->current_sid);
          if (sat_add_pmt(r5kdev, &sat->sids[i].epid[j])) {
            //This is a new Video PID
            for(k = 0; k < sat->sids[i].epid_count; k++) {
              if(sat->sids[i].epid[k].id == 0x81 ||
                 sat->sids[i].epid[k].id == 0xbd) {
                sat_add_pmt(r5kdev, &sat->sids[i].epid[k]);
              }
            }
            r5kdev->cb(r5000_pat_pkt, 188, r5kdev->cb_data);
            r5000_send_pmt(r5kdev);
            sat->pat_pmt_count = 0;
          }
          r5kdev->cb(sat->buf, 188, r5kdev->cb_data);
          return;
        }
      }
    }
  }
  for(i = 0; i < r5kdev->num_pmt_entries; i++) {
    if(pid == r5kdev->pmt[i].pid) {
      r5kdev->cb(sat->buf, 188, r5kdev->cb_data);
      sat->pat_pmt_count++;
      return;
    }
  }
#if 0
  //r5000_print("Ignoring PID: %04x\n", pid);
  {
    char s[80];
    FILE *fh;
    sprintf(s, "0x%04x.ts", pid);
    fh= fopen(s, "a");
    fwrite(buf, 188, 1, fh);
    fclose(fh);
  }
#endif
}

static void sat_process_block(r5kdev_t *r5kdev, unsigned char *buf, int len)
{
  struct r5000_sat *sat = (struct r5000_sat *)r5kdev->stbdata;

  int pos;
  if(! r5kdev->streaming)
    return;
  if(len <= 0)
    return;

  for(pos = sat->offset; pos < len; pos += sat->bytesize) {
    if(! sat->sync) {
      if(buf[pos] == 0x47) {
        int i;
        if ((int)(pos + (sat->allowed_packet_size[0]<<1)) < len) {
          if((buf[pos+1] & 0xfc) == 0xfc && (buf[pos+3] & 0xfc) == 0xfc &&
             (buf[pos+5] & 0xfc) == 0xfc && (buf[pos+7] & 0xfc) == 0xfc) {
             sat->bytesize = 2;
          }
          for(i=0; sat->allowed_packet_size[i] != 0; i++) {
            if(buf[pos+(sat->allowed_packet_size[i] * sat->bytesize)] == 0x47) {
              r5000_print("(%d) Found %d byte sync at %08x: bytesize = %d\n",
                    r5kdev->nexturb, sat->allowed_packet_size[i],
                    r5kdev->bytes_read+pos, sat->bytesize);
              sat->packet_size = sat->allowed_packet_size[i];
              sat->sync = 1;
              sat->buf[0] = 0x47;
              sat->pos = 1;
              break;
            }
          }
        }
      }
      continue;
    }
    if (sat->pos == 0 && buf[pos] != 0x47) {
      sat->sync = 0;
      sat->bytesize = 1;
      r5000_print("(%d)Lost sync at %08x\n", r5kdev->nexturb,
             r5kdev->bytes_read+pos);
      continue;
    }
    if(sat->pos == 3 && (buf[pos] & 0xc0) != 0x00) {
      // Encrypted channel, skip this packet
      sat->pos = 0;
      pos += sat->bytesize * (sat->packet_size - 4); //Loop adds additional 2
      continue;
    }
    sat->buf[sat->pos++] = buf[pos];
    if (sat->pos == sat->packet_size) {
      sat_process_ts(r5kdev, sat->buf);
      //if packet size > 188, assume remaining bytes are parity and ignore
      sat->pos = 0;
    }
  }
  sat->offset = pos - len;
  if(r5kdev->pmt_state == 0x03 && sat->pat_pmt_count > DTV_PAT_PMT_COUNT) {
    r5kdev->cb(r5000_pat_pkt, 188, r5kdev->cb_data);
    r5000_send_pmt(r5kdev);
    sat->pat_pmt_count = 0;
  }
}

static void sat_start_stream(r5kdev_t *r5kdev)
{
  struct r5000_sat *sat = (struct r5000_sat *)r5kdev->stbdata;
  sat->offset = 0;
  sat->sync = 0;
  sat->pos = 0;
  sat->bytesize = 1;

  //Clear any defined PAT or PMT data
  if(sat->sids) {
    free(sat->sids);
    sat->sids = 0;
  }
  memset(&sat->pats, 0, sizeof(struct pat));
  sat->pat_pmt_count = DTV_PAT_PMT_COUNT;
}

static void sat_change_channel(r5kdev_t *r5kdev)
{
  r5000_reset_pmt(r5kdev);
}

void sat_init(r5kdev_t *r5kdev)
{
  struct r5000_sat *sat =
                   (struct r5000_sat *)calloc(1, sizeof(struct r5000_sat));
  r5kdev->stbdata = sat;
  sat->pat_pmt_count = DTV_PAT_PMT_COUNT;
  if(r5kdev->stb_type == R5K_STB_HDD) {
    sat->sync_byte = 0xff;
    sat->has_sync_byte = 1;
    sat->allowed_packet_size[0] = 272;
    sat->allowed_packet_size[1] = 233;
    sat->allowed_packet_size[2] = 204;
    sat->allowed_packet_size[3] = 188;
    sat->allowed_packet_size[4] = 0;
  } else {
    sat->allowed_packet_size[0] = 188;
    sat->allowed_packet_size[1] = 0;
  }
  r5kdev->process_block = sat_process_block;
  r5kdev->start_stream  = sat_start_stream;
  r5kdev->change_channel = &sat_change_channel;
  if(r5kdev->stb_type == R5K_STB_DSR) {
    r5kdev->power_active_low = 1;
  }
#ifdef R5K_DSR_BUTTONS
  r5kdev->button = &dsr_button_cmd;
#endif
}

void sat_free(r5kdev_t *r5kdev)
{
  struct r5000_sat *sat = (struct r5000_sat *)r5kdev->stbdata;
  if(sat->sids)
    free(sat->sids);
  free(r5kdev->stbdata);
}


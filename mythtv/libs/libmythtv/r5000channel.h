/**
 *  R5000Channel
 *  Copyright (c) 2008 by Alan Nisota
 *  Copyright (c) 2005 by Jim Westfall and Dave Abrahams
 *  Distributed as part of MythTV under GPL v2 and later.
 */

#ifndef _R5000CHANNEL_H_
#define _R5000CHANNEL_H_

#include "tv_rec.h"
#include "dtvchannel.h"
#include "r5000device.h"

class R5000Channel : public DTVChannel
{
  public:
    R5000Channel(TVRec *parent, const QString &videodevice,
                    const QString &_r5ktype, bool pocc);
    ~R5000Channel() { Close(); }

    // Commands
    virtual bool Open(void);
    virtual void Close(void);

    virtual bool TuneMultiplex(uint /*mplexid*/, QString /*inputname*/)
        { return false; }
    virtual bool Tune(const DTVMultiplex &/*tuning*/, QString /*inputname*/)
        { return false; }
    virtual bool Retune(void);

    // Sets
    virtual bool SetChannelByString(const QString &chan);
    virtual bool SetChannelByNumber(const QString &channel, int mpeg_prog);
    virtual bool SetPowerState(bool on);
    void SetSlowTuning(uint how_slow_in_ms)
        { tuning_delay = how_slow_in_ms; }

    // Gets
    virtual bool IsOpen(void) const { return isopen; }
    virtual R5000Device::PowerState GetPowerState(void) const;
    virtual QString GetDevice(void) const;
    virtual R5000Device *GetR5000Device(void) { return device; }

  protected:
    QString            videodevice;
    FireWireDBOptions  fw_opts;
    bool               power_on_channel_change;
    R5000Device        *device;
    QString            current_channel;
    uint               current_mpeg_prog;
    bool               isopen;
    uint               tuning_delay;///< Extra delay to add
};

#endif // _R5000CHANNEL_H_

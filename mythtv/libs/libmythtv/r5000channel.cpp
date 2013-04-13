/**
 *  R5000Channel
 *  Copyright (c) 2005 by Jim Westfall, Dave Abrahams
 *  Copyright (c) 2006 by Daniel Kristjansson
 *  Distributed as part of MythTV under GPL v2 and later.
 */

#include "mythcontext.h"
#include "tv_rec.h"
#include "r5000channel.h"

#define LOC QString("R5kChan(%1): ").arg(GetDevice())
#define LOC_WARN QString("R5kChan(%1), Warning: ").arg(GetDevice())
#define LOC_ERR QString("R5kChan(%1), Error: ").arg(GetDevice())

R5000Channel::R5000Channel(TVRec *parent, const QString &_videodevice,const QString &_r5ktype, bool pocc) :
    DTVChannel(parent),
    videodevice(_videodevice),
    power_on_channel_change(pocc),
    device(NULL),
    current_channel(""),
    current_mpeg_prog(0),
    isopen(false),
    tuning_delay(0)
{
    int type = R5000Device::GetDeviceType(_r5ktype);
    device = new R5000Device(type, videodevice);

    InitializeInputs();
}

bool R5000Channel::SetChannelByString(const QString &channum)
{
    QString loc = LOC + QString("SetChannelByString(%1)").arg(channum);
    bool ok = false;
    LOG(VB_CHANNEL, LOG_DEBUG, LOC);

    InputMap::const_iterator it = m_inputs.find(m_currentInputID);
    if (it == m_inputs.end())
        return false;

    QString tvformat, modulation, freqtable, freqid, dtv_si_std;
    int finetune;
    uint64_t frequency;
    int mpeg_prog_num;
    uint atsc_major, atsc_minor, mplexid, tsid, netid;
    if (!ChannelUtil::GetChannelData(
        (*it)->sourceid, channum,
        tvformat, modulation, freqtable, freqid,
        finetune, frequency,
        dtv_si_std, mpeg_prog_num, atsc_major, atsc_minor, tsid, netid,
        mplexid, m_commfree))
    {
        LOG(VB_GENERAL, LOG_DEBUG, LOC + " " + QString(
                    "Requested channel '%1' is on input '%2' "
                    "which is in a busy input group")
                .arg(channum).arg(m_currentInputID));

        return false;
    }
    uint mplexid_restriction;
    if (!IsInputAvailable(m_currentInputID, mplexid_restriction))
    {
        LOG(VB_GENERAL, LOG_DEBUG, LOC + " " + QString(
                    "Requested channel '%1' is on input '%2' "
                    "which is in a busy input group")
                .arg(channum).arg(m_currentInputID));

        return false;
    }

    if (!(*it)->externalChanger.isEmpty())
    {
        ok = ChangeExternalChannel((*it)->externalChanger, freqid);
        // -1 resets any state without executing a channel change
        device->SetChannel(fw_opts.model, 0, mpeg_prog_num);
        SetSIStandard("mpeg");
        SetDTVInfo(0,0,0,0,1);
    }
    else
    {
        ok = isopen && SetChannelByNumber(freqid, mpeg_prog_num);
    }

    if (ok)
    {
        if (tuning_delay) {
            LOG(VB_CHANNEL, LOG_DEBUG, LOC + " " + QString(
                    "Adding additional delay: %1ms").arg(tuning_delay));
            usleep(tuning_delay * 1000);
        }
        // Set the current channum to the new channel's channum
        QString tmp = channum;
        tmp.detach();
        m_curchannelname = tmp;
        tmp.detach();
        (*it)->startChanNum = tmp;
    }

    LOG(VB_CHANNEL, LOG_DEBUG, LOC + " " + ((ok) ? "success" : "failure"));

    return ok;
}

bool R5000Channel::Open(void)
{
    LOG(VB_CHANNEL, LOG_DEBUG, LOC + "Open()");

    if (m_inputs.find(m_currentInputID) == m_inputs.end())
        return false;

    if (!device)
        return false;

    if (isopen)
        return true;

    if (!device->OpenPort())
        return false;

    isopen = true;

    return true;
}

void R5000Channel::Close(void)
{
    LOG(VB_CHANNEL, LOG_DEBUG, LOC + "Close()");
    if (isopen)
    {
        device->ClosePort();
        isopen = false;
    }
}

QString R5000Channel::GetDevice(void) const
{
    return videodevice;
}

bool R5000Channel::SetPowerState(bool on)
{
    if (!isopen)
    {
        LOG(VB_GENERAL, LOG_DEBUG, LOC_ERR +
                "SetPowerState() called on closed R5000Channel.");

        return false;
    }
    if (power_on_channel_change)
        return R5000Device::kAVCPowerOn;

    return device->SetPowerState(on);
}

R5000Device::PowerState R5000Channel::GetPowerState(void) const
{
    if (!isopen)
    {
        LOG(VB_GENERAL, LOG_DEBUG, LOC_ERR +
                "GetPowerState() called on closed R5000Channel.");

        return R5000Device::kAVCPowerQueryFailed;
    }

    if (power_on_channel_change)
        return R5000Device::kAVCPowerOn;

    return device->GetPowerState();
}

bool R5000Channel::Retune(void)
{
    LOG(VB_CHANNEL, LOG_DEBUG, LOC + "Retune()");

    if (! power_on_channel_change && R5000Device::kAVCPowerOff == GetPowerState())
    {
        LOG(VB_GENERAL, LOG_DEBUG, LOC_ERR +
                "STB is turned off, must be on to retune.");

        return false;
    }

    if (current_channel.length())
        return SetChannelByNumber(current_channel, current_mpeg_prog);

    return false;
}

bool R5000Channel::SetChannelByNumber(const QString &channel, int mpeg_prog)
{
    LOG(VB_CHANNEL, LOG_DEBUG, QString("SetChannelByNumber(%1)").arg(channel));
    current_channel = channel;
    current_mpeg_prog = mpeg_prog;

    if (! power_on_channel_change && R5000Device::kAVCPowerOff == GetPowerState())
    {
        LOG(VB_GENERAL, LOG_DEBUG, LOC_WARN +
                "STB is turned off, must be on to set channel.");

        SetSIStandard("mpeg");
        SetDTVInfo(0,0,0,0,1);

        return true; // signal monitor will call retune later...
    }

    QString tmpchan = (power_on_channel_change ? "P" : "") + channel;
    if (! device->SetChannel(fw_opts.model, tmpchan, mpeg_prog))
        return false;

    SetSIStandard("mpeg");
    SetDTVInfo(0,0,0,0,1);

    return true;
}

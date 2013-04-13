// -*- Mode: c++ -*-
// Copyright (c) 2006, Daniel Thor Kristjansson

#include <pthread.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/select.h>

#include "mythcontext.h"
#include "mythdbcon.h"
#include "atscstreamdata.h"
#include "mpegtables.h"
#include "atsctables.h"
#include "r5000channel.h"
#include "r5000signalmonitor.h"

#define LOC QString("R5kSM(%1): ").arg(channel->GetDevice())
#define LOC_WARN QString("R5kSM(%1), Warning: ").arg(channel->GetDevice())
#define LOC_ERR QString("R5kSM(%1), Error: ").arg(channel->GetDevice())

const uint R5000SignalMonitor::kPowerTimeout  = 3000; /* ms */
const uint R5000SignalMonitor::kBufferTimeout = 5000; /* ms */

QMap<void*,uint> R5000SignalMonitor::pat_keys;
QMutex           R5000SignalMonitor::pat_keys_lock;

/** \fn R5000SignalMonitor::R5000SignalMonitor(int,R5000Channel*,uint,const char*)
 *  \brief Initializes signal lock and signal values.
 *
 *   Start() must be called to actually begin continuous
 *   signal monitoring. The timeout is set to 3 seconds,
 *   and the signal threshold is initialized to 0%.
 *
 *  \param db_cardnum Recorder number to monitor,
 *                    if this is less than 0, SIGNAL events will not be
 *                    sent to the frontend even if SetNotifyFrontend(true)
 *                    is called.
 *  \param _channel R5000Channel for card
 *  \param _flags   Flags to start with
 *  \param _name    Name for Qt signal debugging
 */
R5000SignalMonitor::R5000SignalMonitor(
    int db_cardnum,
    R5000Channel *_channel,
    uint64_t _flags, const char *_name) :
    DTVSignalMonitor(db_cardnum, _channel, _flags),
    dtvMonitorRunning(false),
    stb_needs_retune(true),
    stb_needs_to_wait_for_pat(false),
    stb_needs_to_wait_for_power(false)
{
    LOG(VB_CHANNEL, LOG_DEBUG, LOC + "ctor");

    signalStrength.SetThreshold(65);

    AddFlags(kSigMon_WaitForSig);

    stb_needs_retune =
        (R5000Device::kAVCPowerOff == _channel->GetPowerState());
}

/** \fn R5000SignalMonitor::~R5000SignalMonitor()
 *  \brief Stops signal monitoring and table monitoring threads.
 */
R5000SignalMonitor::~R5000SignalMonitor()
{
    LOG(VB_CHANNEL, LOG_DEBUG, LOC + "dtor");
    Stop();
}

/** \fn R5000SignalMonitor::Stop(void)
 *  \brief Stop signal monitoring and table monitoring threads.
 */
void R5000SignalMonitor::Stop(void)
{
    LOG(VB_CHANNEL, LOG_DEBUG, LOC + "Stop() -- begin");
    SignalMonitor::Stop();
    if (dtvMonitorRunning)
    {
        dtvMonitorRunning = false;
        pthread_join(table_monitor_thread, NULL);
    }
    LOG(VB_CHANNEL, LOG_DEBUG, LOC + "Stop() -- end");
}

void R5000SignalMonitor::HandlePAT(const ProgramAssociationTable *pat)
{
    AddFlags(kDTVSigMon_PATSeen);

    R5000Channel *fwchan = dynamic_cast<R5000Channel*>(channel);
    if (!fwchan)
        return;

    bool crc_bogus = !fwchan->GetR5000Device()->IsSTBBufferCleared();
    if (crc_bogus && stb_needs_to_wait_for_pat &&
        (stb_wait_for_pat_timer.elapsed() < (int)kBufferTimeout))
    {
        LOG(VB_CHANNEL, LOG_DEBUG, LOC + "HandlePAT() ignoring PAT");
        uint tsid = pat->TransportStreamID();
        GetStreamData()->SetVersionPAT(tsid, -1,0);
        return;
    }

    if (crc_bogus && stb_needs_to_wait_for_pat)
    {
        LOG(VB_GENERAL, LOG_DEBUG, LOC_WARN + "Wait for valid PAT timed out");
        stb_needs_to_wait_for_pat = false;
    }

    DTVSignalMonitor::HandlePAT(pat);
}

void R5000SignalMonitor::HandlePMT(uint pnum, const ProgramMapTable *pmt)
{
    LOG(VB_CHANNEL, LOG_DEBUG, LOC + "HandlePMT()");

    AddFlags(kDTVSigMon_PMTSeen);

    if (!HasFlags(kDTVSigMon_PATMatch))
    {
        GetStreamData()->SetVersionPMT(pnum, -1,0);
        LOG(VB_CHANNEL, LOG_DEBUG, LOC + "HandlePMT() ignoring PMT");
        return;
    }

    DTVSignalMonitor::HandlePMT(pnum, pmt);
}

void *R5000SignalMonitor::TableMonitorThread(void *param)
{
    R5000SignalMonitor *mon = (R5000SignalMonitor*) param;
    mon->RunTableMonitor();
    return NULL;
}

void R5000SignalMonitor::RunTableMonitor(void)
{
    stb_needs_to_wait_for_pat = true;
    stb_wait_for_pat_timer.start();
    dtvMonitorRunning = true;

    LOG(VB_CHANNEL, LOG_DEBUG, LOC + "RunTableMonitor(): -- begin");

    R5000Channel *lchan = dynamic_cast<R5000Channel*>(channel);
    if (!lchan)
    {
        LOG(VB_CHANNEL, LOG_DEBUG, LOC + "RunTableMonitor(): -- err end");
        dtvMonitorRunning = false;
        return;
    }

    R5000Device *dev = lchan->GetR5000Device();

    dev->OpenPort();
    dev->AddListener(this);

    while (dtvMonitorRunning && GetStreamData())
        usleep(100000);

    LOG(VB_CHANNEL, LOG_DEBUG, LOC + "RunTableMonitor(): -- shutdown ");

    dev->RemoveListener(this);
    dev->ClosePort();

    dtvMonitorRunning = false;

    LOG(VB_CHANNEL, LOG_DEBUG, LOC + "RunTableMonitor(): -- end");
}

void R5000SignalMonitor::AddData(const unsigned char *data, uint len)
{
    if (!dtvMonitorRunning)
        return;

    if (GetStreamData())
        GetStreamData()->ProcessData((unsigned char *)data, len);
}

/** \fn R5000SignalMonitor::UpdateValues(void)
 *  \brief Fills in frontend stats and emits status Qt signals.
 *
 *   This function uses five ioctl's FE_READ_SNR, FE_READ_SIGNAL_STRENGTH
 *   FE_READ_BER, FE_READ_UNCORRECTED_BLOCKS, and FE_READ_STATUS to obtain
 *   statistics from the frontend.
 *
 *   This is automatically called by MonitorLoop(), after Start()
 *   has been used to start the signal monitoring thread.
 */
void R5000SignalMonitor::UpdateValues(void)
{
    if (!running || exit)
        return;

    //if (!IsChannelTuned())
      //  return;

    if (dtvMonitorRunning)
    {
        EmitStatus();
        if (IsAllGood())
            SendMessageAllGood();
        // TODO dtv signals...

        update_done = true;
        return;
    }

    if (stb_needs_to_wait_for_power &&
        (stb_wait_for_power_timer.elapsed() < (int)kPowerTimeout))
    {
        return;
    }
    stb_needs_to_wait_for_power = false;

    R5000Channel *fwchan = dynamic_cast<R5000Channel*>(channel);
    if (!fwchan)
        return;

    if (HasFlags(kFWSigMon_WaitForPower) && !HasFlags(kFWSigMon_PowerMatch))
    {
        bool retried = false;
        while (true)
        {
            R5000Device::PowerState power = fwchan->GetPowerState();
            if (R5000Device::kAVCPowerOn == power)
            {
                AddFlags(kFWSigMon_PowerSeen | kFWSigMon_PowerMatch);
            }
            else if (R5000Device::kAVCPowerOff == power)
            {
                AddFlags(kFWSigMon_PowerSeen);
                fwchan->SetPowerState(true);
                stb_wait_for_power_timer.start();
                stb_needs_to_wait_for_power = true;
            }
            else
            {
                bool qfailed = (R5000Device::kAVCPowerQueryFailed == power);
                if (qfailed && !retried)
                {
                    retried = true;
                    continue;
                }

                LOG(VB_RECORD, LOG_INFO, "Can't determine if STB is power on, "
                        "assuming it is...");
                AddFlags(kFWSigMon_PowerSeen | kFWSigMon_PowerMatch);
            }
            break;
        }
    }

    bool isLocked = !HasFlags(kFWSigMon_WaitForPower) ||
        HasFlags(kFWSigMon_WaitForPower | kFWSigMon_PowerMatch);

    if (isLocked && stb_needs_retune)
    {
        fwchan->Retune();
        isLocked = stb_needs_retune = false;
    }

    // Set SignalMonitorValues from info from card.
    {
        QMutexLocker locker(&statusLock);
        signalStrength.SetValue(isLocked ? 100 : 0);
        signalLock.SetValue(isLocked ? 1 : 0);
    }

    EmitStatus();
    if (IsAllGood())
        SendMessageAllGood();

    // Start table monitoring if we are waiting on any table
    // and we have a lock.
    if (isLocked && GetStreamData() &&
        HasAnyFlag(kDTVSigMon_WaitForPAT | kDTVSigMon_WaitForPMT |
                   kDTVSigMon_WaitForMGT | kDTVSigMon_WaitForVCT |
                   kDTVSigMon_WaitForNIT | kDTVSigMon_WaitForSDT))
    {
        pthread_create(&table_monitor_thread, NULL,
                       TableMonitorThread, this);

        LOG(VB_CHANNEL, LOG_DEBUG, LOC + "UpdateValues() -- "
                "Waiting for table monitor to start");

        while (!dtvMonitorRunning)
            usleep(50);

        LOG(VB_CHANNEL, LOG_DEBUG, LOC + "UpdateValues() -- "
                "Table monitor started");
    }

    update_done = true;
}

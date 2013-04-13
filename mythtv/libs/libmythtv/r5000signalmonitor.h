// -*- Mode: c++ -*-

#ifndef _R5000SIGNALMONITOR_H_
#define _R5000SIGNALMONITOR_H_

#include <qmap.h>
#include <qmutex.h>
#include <qdatetime.h>

#include "dtvsignalmonitor.h"
#include "r5000device.h"
//#include "util.h"

class R5000Channel;

class R5000SignalMonitor : public DTVSignalMonitor, public TSDataListener
{
  public:
    R5000SignalMonitor(int db_cardnum, R5000Channel *_channel,
                          uint64_t _flags = kFWSigMon_WaitForPower,
                          const char *_name = "R5000SignalMonitor");

    virtual void HandlePAT(const ProgramAssociationTable*);
    virtual void HandlePMT(uint, const ProgramMapTable*);

    void Stop(void);

  protected:
    R5000SignalMonitor(void);
    R5000SignalMonitor(const R5000SignalMonitor&);
    virtual ~R5000SignalMonitor();

    virtual void UpdateValues(void);

    static void *TableMonitorThread(void *param);
    void RunTableMonitor(void);

    bool SupportsTSMonitoring(void);

    void AddData(const unsigned char *data, uint dataSize);

  public:
    static const uint kPowerTimeout;
    static const uint kBufferTimeout;

  protected:
    bool               dtvMonitorRunning;
    pthread_t          table_monitor_thread;
    bool               stb_needs_retune;
    bool               stb_needs_to_wait_for_pat;
    bool               stb_needs_to_wait_for_power;
    MythTimer          stb_wait_for_pat_timer;
    MythTimer          stb_wait_for_power_timer;

    vector<unsigned char> buffer;

    static QMap<void*,uint> pat_keys;
    static QMutex           pat_keys_lock;
};

#endif // _R5000SIGNALMONITOR_H_

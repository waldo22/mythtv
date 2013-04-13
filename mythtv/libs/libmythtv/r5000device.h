/**
 *  R5000Device
 *  Copyright (c) 2005 by Jim Westfall
 *  Distributed as part of MythTV under GPL v2 and later.
 */

#ifndef _R5000_DEVICE_H_
#define _R5000_DEVICE_H_

// C++ headers
#include <vector>
using namespace std;

// Qt headers
#include <qstring.h>
#include <qmutex.h>

// MythTV headers
#include "streamlisteners.h"

extern "C" {
#include "r5000/r5000.h"
}

class TSPacket;
class R5kPriv;
class R5000Device
{
  public:

    // Public enums
    typedef enum
    {
        kAVCPowerOn,
        kAVCPowerOff,
        kAVCPowerUnknown,
        kAVCPowerQueryFailed,
    } PowerState;


    // AVC param 0
    typedef enum
    {
        kAVCPowerStateOn           = 0x70,
        kAVCPowerStateOff          = 0x60,
        kAVCPowerStateQuery        = 0x7f,
    } IEEE1394UnitPowerParam0;

    R5000Device(int type, QString serial);
    ~R5000Device();

    bool OpenPort(void);
    bool ClosePort(void);
    void RunPortHandler(void);

    // Commands
    virtual bool ResetBus(void) { return false; }

    virtual void AddListener(TSDataListener*);
    virtual void RemoveListener(TSDataListener*);

    // Sets
    virtual bool SetPowerState(bool on);
    virtual bool SetChannel(const QString &panel_model,
                            const QString &channel, uint mpeg_prog);

    // Gets
    bool IsSTBBufferCleared(void) const { return m_buffer_cleared; }

    // non-const Gets
    virtual PowerState GetPowerState(void);

    // Statics
    static bool IsSTBSupported(const QString &model);
    static QString GetModelName(uint vendorid, uint modelid);
    static QStringList GetSTBList(void);
    static int GetDeviceType(const QString &r5ktype);
    void BroadcastToListeners(
        const unsigned char *data, uint dataSize);

  protected:

    bool GetSubunitInfo(uint8_t table[32]);

    void SetLastChannel(const QString &channel);
    void ProcessPATPacket(const TSPacket&);
    bool StartStreaming(void);
    bool StopStreaming(void);

    int                      m_type;
    QString                  m_serial;
    uint                     m_subunitid;
    uint                     m_speed;
    QString                  m_last_channel;
    uint                     m_last_crc;
    bool                     m_buffer_cleared;

    uint                     m_open_port_cnt;
    vector<TSDataListener*>  m_listeners;
    mutable QMutex           m_lock;

    /// Vendor ID + Model ID to R5000Device STB model string
    static QMap<uint64_t,QString> s_id_to_model;
    static QMutex                 s_static_lock;
private:
    r5kdev_t                 *usbdev;
    R5kPriv                  *m_priv;
};

#endif // _FIREWIRE_DEVICE_H_

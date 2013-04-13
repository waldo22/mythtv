/**
 *  R5000Device
 *  Copyright (c) 2008 by Alan Nisota
 *  Copyright (c) 2005 by Jim Westfall
 *  Distributed as part of MythTV under GPL v2 and later.
 */

// C++ headers
#include <algorithm>

// Qt headers
#include <QMap>

// for usleep
#include <unistd.h> 

// MythTV headers
#include "r5000device.h"
#include "mythcontext.h"
#include "mythlogging.h"
#include "pespacket.h"
#include "mthread.h"
#include "mythtimer.h"

#define LOC      QString("R5kDev: ")
#define LOC_WARN QString("R5kDev, Warning: ")
#define LOC_ERR  QString("R5kDev, Error: ")

static int r5k_init = 0;
QMap<uint64_t,QString> R5000Device::s_id_to_model;
QMutex                 R5000Device::s_static_lock;

unsigned int r5000_device_tspacket_handler(unsigned char *tspacket, int len, void *callback_data)
{
    R5000Device *fw = (R5000Device*) callback_data;
    if (! fw)
        return 0;
    if (len > 0)
        fw->BroadcastToListeners(tspacket, len);
    return 1;
}

void r5000_msg(char * msg)
{
    LOG(VB_GENERAL, LOG_INFO, LOC +
        QString("R5kLib: ") + msg);
}

class R5kPriv
{
  public:
    R5kPriv() :
        reset_timer_on(false),
        run_port_handler(false), is_port_handler_running(false),
        channel(-1),
        is_streaming(false)
    {
    }

    bool             reset_timer_on;
    MythTimer        reset_timer;

    bool             run_port_handler;
    bool             is_port_handler_running;
    QMutex           start_stop_port_handler_lock;

    int              channel;

    bool             is_streaming;

    QDateTime        stop_streaming_timer;
    pthread_t        port_handler_thread;

    static QMutex           s_lock;
};
QMutex          R5kPriv::s_lock;

//callback functions
void *r5000_device_port_handler_thunk(void *param);

R5000Device::R5000Device(int type, QString serial) :
    m_type(type),
    m_serial(serial),
    m_last_channel(""),      m_last_crc(0),
    m_buffer_cleared(true), m_open_port_cnt(0),
    m_lock(),          m_priv(new R5kPriv())
{
    QMutexLocker locker(&s_static_lock);
    usbdev = NULL;
    if (! r5k_init)
      r5k_init = r5000_init(r5000_msg);
}

R5000Device::~R5000Device()
{
    if (usbdev)
    {
        LOG(VB_GENERAL, LOG_DEBUG, LOC_ERR + "ctor called with open port");
        while (usbdev)
            ClosePort();
    }

    if (m_priv)
    {
        delete m_priv;
        m_priv = NULL;
    }
}

void R5000Device::AddListener(TSDataListener *listener)
{
    QMutexLocker locker(&m_lock);
    if (listener)
    {
        vector<TSDataListener*>::iterator it =
            find(m_listeners.begin(), m_listeners.end(), listener);

        if (it == m_listeners.end())
            m_listeners.push_back(listener);
    }

    LOG(VB_RECORD, LOG_INFO, LOC +
        QString("AddListener() %1").arg(m_listeners.size()));
    if (!m_listeners.empty())
    {
        StartStreaming();
    }
}

void R5000Device::RemoveListener(TSDataListener *listener)
{
    QMutexLocker locker(&m_lock);
    vector<TSDataListener*>::iterator it = m_listeners.end();

    do
    {
        it = find(m_listeners.begin(), m_listeners.end(), listener);
        if (it != m_listeners.end())
            m_listeners.erase(it);
    }
    while (it != m_listeners.end());

    LOG(VB_RECORD, LOG_INFO, LOC +
        QString("RemoveListener() %1").arg(m_listeners.size()));
    if (m_listeners.empty())
    {
        StopStreaming();
    }
}

bool R5000Device::StartStreaming(void)
{
    if (m_priv->is_streaming)
        return m_priv->is_streaming;

    if (! usbdev)
    {
        LOG(VB_GENERAL, LOG_DEBUG, LOC_ERR + "Device not open");
        return false;
    }
    if (r5000_start_stream(usbdev))
    {
        m_priv->is_streaming = true;
    }
    else
    {
        LOG(VB_GENERAL, LOG_DEBUG, LOC_ERR + "Starting A/V streaming ");
    }

    LOG(VB_RECORD, LOG_DEBUG, LOC + "Starting A/V streaming -- done");

    return m_priv->is_streaming;
}

bool R5000Device::StopStreaming(void)
{
    if (m_priv->is_streaming)
    {
        LOG(VB_RECORD, LOG_DEBUG, LOC + "Stopping A/V streaming -- really");

        m_priv->is_streaming = false;

        r5000_stop_stream(usbdev);
    }

    LOG(VB_RECORD, LOG_DEBUG, LOC + "Stopped A/V streaming");

    return true;
}

bool R5000Device::SetPowerState(bool on)
{
    QMutexLocker locker(&m_lock);
    QString cmdStr = (on) ? "on" : "off";
    LOG(VB_RECORD, LOG_DEBUG, LOC + QString("Powering %1").arg(cmdStr));
    r5000_power_on_off(usbdev, on);
    return true;
}

R5000Device::PowerState R5000Device::GetPowerState(void)
{
    QMutexLocker locker(&m_lock);
    int on_off;

    LOG(VB_CHANNEL, LOG_DEBUG, LOC + "Requesting STB Power State");
    on_off = r5000_get_power_state(usbdev);
    LOG(VB_CHANNEL, LOG_DEBUG, LOC + (on_off ? "On" : "Off"));
    return on_off ? kAVCPowerOn : kAVCPowerOff;
}

bool R5000Device::SetChannel(const QString &panel_model,
                                const QString &channel, uint mpeg_prog)
{
    LOG(VB_CHANNEL, LOG_DEBUG, QString("SetChannel(model %1, chan %2 mpeg_prog %3)")
            .arg(panel_model).arg(channel).arg(mpeg_prog));

    QMutexLocker locker(&m_lock);
    LOG(VB_CHANNEL, LOG_DEBUG, "SetChannel() -- locked");
    if (! r5000_change_channel(usbdev, channel.toAscii().constData(), mpeg_prog))
        LOG(VB_GENERAL, LOG_DEBUG, LOC + "Failed to set channel");
    return true;
}

void R5000Device::BroadcastToListeners(
    const unsigned char *data, uint dataSize)
{
    QMutexLocker locker(&m_lock);
    if ((dataSize >= TSPacket::kSize) && (data[0] == SYNC_BYTE) &&
        ((data[1] & 0x1f) == 0) && (data[2] == 0))
    {
        ProcessPATPacket(*((const TSPacket*)data));
    }

    vector<TSDataListener*>::iterator it = m_listeners.begin();
    for (; it != m_listeners.end(); ++it)
        (*it)->AddData(data, dataSize);
}

void R5000Device::SetLastChannel(const QString &channel)
{
    m_buffer_cleared = (channel == m_last_channel);
    m_last_channel   = channel;

    LOG(VB_GENERAL, LOG_DEBUG, QString("SetLastChannel(%1): cleared: %2")
            .arg(channel).arg(m_buffer_cleared ? "yes" : "no"));
}

void R5000Device::ProcessPATPacket(const TSPacket &tspacket)
{
    if (!tspacket.TransportError() && !tspacket.Scrambled() &&
        tspacket.HasPayload() && tspacket.PayloadStart() && !tspacket.PID())
    {
        PESPacket pes = PESPacket::View(tspacket);
        uint crc = pes.CalcCRC();
        m_buffer_cleared |= (crc != m_last_crc);
        m_last_crc = crc;
#if 0
        LOG(VB_RECORD, LOG_DEBUG, LOC +
                QString("ProcessPATPacket: CRC 0x%1 cleared: %2")
                .arg(crc,0,16).arg(m_buffer_cleared ? "yes" : "no"));
#endif
    }
    else
    {
        LOG(VB_GENERAL, LOG_DEBUG, LOC_ERR + "Can't handle large PAT's");
    }
}

QString R5000Device::GetModelName(uint vendor_id, uint model_id)
{
    QMutexLocker locker(&s_static_lock);
/*
    if (s_id_to_model.empty())
        fw_init(s_id_to_model);

    QString ret = s_id_to_model[(((uint64_t) vendor_id) << 32) | model_id];

    if (ret.isEmpty())
        return "GENERIC";
*/
    return "R5000";
}

bool R5000Device::IsSTBSupported(const QString &panel_model)
{
    return true;
}

bool R5000Device::OpenPort(void)
{
    LOG(VB_RECORD, LOG_DEBUG, LOC + "Starting Port Handler Thread");
    QMutexLocker mlocker(&m_lock);
    LOG(VB_RECORD, LOG_DEBUG, LOC + "Starting Port Handler Thread -- locked");
    if (usbdev)
    {
        m_open_port_cnt++;
        return true;
    }

    if (m_serial.length())
    {
      LOG(VB_RECORD, LOG_DEBUG, LOC + QString("Opening R5000 device type %1 with serial#: "+ m_serial).arg(m_type));
      usbdev = r5000_open((r5ktype_t)m_type, r5000_device_tspacket_handler, this, m_serial.toAscii().constData());
    }
    else
    {
      LOG(VB_RECORD, LOG_DEBUG, LOC + "Opening R5000 device with unknown serial#");
      usbdev = r5000_open((r5ktype_t)m_type, r5000_device_tspacket_handler, this, NULL);
    }
    if (! usbdev)
    {
        LOG(VB_GENERAL, LOG_DEBUG, LOC + "Failed to open R5000 device");
        return false;
    }

    LOG(VB_RECORD, LOG_DEBUG, LOC + "Starting port handler thread");
    m_priv->run_port_handler = true;
    pthread_create(&m_priv->port_handler_thread, NULL,
                   r5000_device_port_handler_thunk, this);

    LOG(VB_RECORD, LOG_DEBUG, LOC + "Waiting for port handler thread to start");
    while (!m_priv->is_port_handler_running)
    {
        m_lock.unlock();
        usleep(5000);
        m_lock.lock();
    }

    LOG(VB_RECORD, LOG_DEBUG, LOC + "Port handler thread started");

    m_open_port_cnt++;

    return true;
}

bool R5000Device::ClosePort(void)
{
    LOG(VB_RECORD, LOG_DEBUG, LOC + "Stopping Port Handler Thread");
    QMutexLocker locker(&m_priv->start_stop_port_handler_lock);
    LOG(VB_RECORD, LOG_DEBUG, LOC + "Stopping Port Handler Thread -- locked");

    QMutexLocker mlocker(&m_lock);

    LOG(VB_RECORD, LOG_DEBUG, LOC + "ClosePort()");

    if (m_open_port_cnt < 1)
        return false;

    m_open_port_cnt--;

    if (m_open_port_cnt != 0)
        return true;

    if (!usbdev)
        return false;

    LOG(VB_RECORD, LOG_DEBUG, LOC + "Waiting for port handler thread to stop");
    m_priv->run_port_handler = false;
    while (m_priv->is_port_handler_running)
    {
        m_lock.unlock();
        usleep(5000);
        m_lock.lock();
    }
    LOG(VB_RECORD, LOG_DEBUG, LOC + "Joining port handler thread");
    pthread_join(m_priv->port_handler_thread, NULL);

    r5000_close(usbdev);
    usbdev = NULL;

    return true;
}

void *r5000_device_port_handler_thunk(void *param)
{
    R5000Device *mon = (R5000Device*) param;
    mon->RunPortHandler();
    return NULL;
}

void R5000Device::RunPortHandler(void)
{
    LOG(VB_RECORD, LOG_DEBUG, LOC + "RunPortHandler -- start");
    m_lock.lock();
    LOG(VB_RECORD, LOG_DEBUG, LOC + "RunPortHandler -- got first lock");
    m_priv->is_port_handler_running = true;
    m_lock.unlock();

    while (m_priv->run_port_handler)
    {
        if (m_priv->is_streaming)
        {
            // This will timeout after 10ms regardless of data availability
            r5000_loop_iterate(usbdev, 10);
        }
        else
        {
            usleep(10000);
        }
    }

    m_lock.lock();
    m_priv->is_port_handler_running = false;
    m_lock.unlock();
    LOG(VB_RECORD, LOG_DEBUG, LOC + "RunPortHandler -- end");
}

QStringList R5000Device::GetSTBList(void)
{
    QStringList STBList;
    int i;
    r5kenum_t r5k_stbs;
    if (! r5k_init)
    {
        r5k_init = r5000_init(r5000_msg);
        if (! r5k_init)
            return STBList;
    }
    if (! r5000_find_stbs(&r5k_stbs))
        LOG(VB_GENERAL, LOG_DEBUG, LOC + "Locating R5000 devices failed."
                                    "  This may be a permission problem");

    for (i = 0; i < r5k_stbs.count; i++)
        STBList.append((char *)r5k_stbs.serial[i]);
    return STBList;
}

int R5000Device::GetDeviceType(const QString &r5ktype)
{
    QString type = r5ktype.toUpper();
    if (type == "DIRECTV")
    {
        return R5K_STB_DIRECTV;
    }
    else if ("VIP211/VIP622/DISH411" == type ||
               "VIP211/VIP422"         == type ||
               "VIP211"                == type ||
               "VIP411"                == type)
    {
        return R5K_STB_VIP211;
    }
    else if ("STARCHOICE/DSR" == type)
    {
        return R5K_STB_DSR;
    }
    else if ("HDD-200" == type)
    {
        return R5K_STB_HDD;
    }
    else if ("VIP622"  == type ||
               "VIP722"  == type ||
               "BEV9242" == type)
    {
        return R5K_STB_VIP622;
    }
    else if ("DISH6000" == type)
    {
        return R5K_STB_DISH6000;
    }
    else
    {
      return R5K_STB_VIP211;
    }
}

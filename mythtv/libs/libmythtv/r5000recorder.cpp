/**
 *  R5000Recorder
 *  Copyright (c) 2005 by Jim Westfall and Dave Abrahams
 *  Distributed as part of MythTV under GPL v2 and later.
 */

// MythTV includes
#include "r5000recorder.h"
#include "r5000channel.h"
#include "mythcontext.h"
#include "mpegtables.h"
#include "mpegstreamdata.h"
#include "tv_rec.h"

#define LOC QString("R5000RecBase(%1): ").arg(channel->GetDevice())
#define LOC_ERR QString("R5000RecBase(%1), Error: ").arg(channel->GetDevice())

R5000Recorder::R5000Recorder(TVRec *rec, R5000Channel *chan) :
    DTVRecorder(rec),
    channel(chan), isopen(false)
{
    //_wait_for_keyframe_option = false;
}

R5000Recorder::~R5000Recorder()
{
    Close();
}

bool R5000Recorder::Open(void)
{
    if (!isopen)
        isopen = channel->GetR5000Device()->OpenPort();

    return isopen;
}

void R5000Recorder::Close(void)
{
    if (isopen)
    {
        channel->GetR5000Device()->ClosePort();
        isopen = false;
    }
}

void R5000Recorder::StartStreaming(void)
{
    channel->GetR5000Device()->AddListener(this);
}

void R5000Recorder::StopStreaming(void)
{
    channel->GetR5000Device()->RemoveListener(this);
}

void R5000Recorder::StartRecording(void)
{
    LOG(VB_RECORD, LOG_DEBUG, LOC + "StartRecording");

    if (!Open())
    {
        _error = true;
        return;
    }

    request_recording = true;
    recording = true;

    StartStreaming();

    while (request_recording)
    {
        if (!PauseAndWait())
            usleep(50 * 1000);
    }

    StopStreaming();
    FinishRecording();

    recording = false;
}

void R5000Recorder::run(void)
{
    LOG(VB_RECORD, LOG_INFO, LOC + "run");

    if (!Open())
    {
        _error = "Failed to open R5000 device";
        LOG(VB_GENERAL, LOG_ERR, LOC + _error);
        return;
    }

    {
        QMutexLocker locker(&pauseLock);
        request_recording = true;
        recording = true;
        recordingWait.wakeAll();
    }

    StartStreaming();

    while (IsRecordingRequested() && !IsErrored())
    {
        if (PauseAndWait())
            continue;

        if (!IsRecordingRequested())
            break;

        {   // sleep 1 seconds unless StopRecording() or Unpause() is called,
            // just to avoid running this too often.
            QMutexLocker locker(&pauseLock);
            if (!request_recording || request_pause)
                continue;
            unpauseWait.wait(&pauseLock, 1000);
        }
    }

    StopStreaming();
    FinishRecording();

    QMutexLocker locker(&pauseLock);
    recording = false;
    recordingWait.wakeAll();
}

void R5000Recorder::AddData(const unsigned char *data, uint len)
{
    uint bufsz = buffer.size();
    if ((SYNC_BYTE == data[0]) && (TSPacket::kSize == len) &&
        (TSPacket::kSize > bufsz))
    {
        if (bufsz)
            buffer.clear();

        ProcessTSPacket(*(reinterpret_cast<const TSPacket*>(data)));
        return;
    }

    buffer.insert(buffer.end(), data, data + len);
    bufsz += len;

    int sync_at = -1;
    for (uint i = 0; (i < bufsz) && (sync_at < 0); i++)
    {
        if (buffer[i] == SYNC_BYTE)
            sync_at = i;
    }

    if (sync_at < 0)
        return;

    if (bufsz < 30 * TSPacket::kSize)
        return; // build up a little buffer

    while (sync_at + TSPacket::kSize < bufsz)
    {
        ProcessTSPacket(*(reinterpret_cast<const TSPacket*>(
                              &buffer[0] + sync_at)));

        sync_at += TSPacket::kSize;
    }

    buffer.erase(buffer.begin(), buffer.begin() + sync_at);

    return;
}

bool R5000Recorder::ProcessTSPacket(const TSPacket &tspacket)
{
    if (tspacket.TransportError())
        return true;

    if (tspacket.Scrambled())
        return true;

    if (! GetStreamData())
        return true;
    if (tspacket.HasAdaptationField())
        GetStreamData()->HandleAdaptationFieldControl(&tspacket);

    if (tspacket.HasPayload())
    {
        const unsigned int lpid = tspacket.PID();
        // Pass or reject packets based on PID, and parse info from them
        if (lpid == GetStreamData()->VideoPIDSingleProgram())
        {
            ProgramMapTable *pmt = GetStreamData()->PMTSingleProgram();
            uint video_stream_type = pmt->StreamType(pmt->FindPID(lpid));

            if (video_stream_type == StreamID::H264Video)
                _buffer_packets = !FindH264Keyframes(&tspacket);
            else if (StreamID::IsVideo(video_stream_type))
                _buffer_packets = !FindMPEG2Keyframes(&tspacket);

            if ((video_stream_type != StreamID::H264Video) || _seen_sps)
                BufferedWrite(tspacket);
        }
        else if (GetStreamData()->IsAudioPID(lpid))
        {
            _buffer_packets = !FindAudioKeyframes(&tspacket);
            BufferedWrite(tspacket);
        }
        else if (GetStreamData()->IsListeningPID(lpid))
            GetStreamData()->HandleTSTables(&tspacket);
        else if (GetStreamData()->IsWritingPID(lpid))
            BufferedWrite(tspacket);
    }

    return true;
}

void R5000Recorder::SetOptionsFromProfile(RecordingProfile *profile,
                                                 const QString &videodev,
                                                 const QString &audiodev,
                                                 const QString &vbidev)
{
    (void)videodev;
    (void)audiodev;
    (void)vbidev;
    (void)profile;
}

// documented in recorderbase.cpp
bool R5000Recorder::PauseAndWait(int timeout)
{
    if (request_pause)
    {
        LOG(VB_RECORD, LOG_INFO, LOC +
            QString("PauseAndWait(%1) -- pause").arg(timeout));
        if (!IsPaused(true))
        {
            StopStreaming();
            paused = true;
            pauseWait.wakeAll();
            if (tvrec)
                tvrec->RecorderPaused();
        }
        QMutex unpause_lock;
        unpause_lock.lock();
        unpauseWait.wait(&unpause_lock, timeout);
    }
    if (!request_pause && IsPaused(true))
    {
        LOG(VB_RECORD, LOG_INFO, LOC +
            QString("PauseAndWait(%1) -- unpause").arg(timeout));
        StartStreaming();
        paused = false;
    }
    return paused;
}

void R5000Recorder::SetStreamData(void)
{
    _stream_data->AddMPEGSPListener(this);

    if (_stream_data->DesiredProgram() >= 0)
        _stream_data->SetDesiredProgram(_stream_data->DesiredProgram());
}

void R5000Recorder::HandleSingleProgramPAT(ProgramAssociationTable *pat)
{
    if (!pat)
    {
        LOG(VB_RECORD, LOG_DEBUG, LOC + "HandleSingleProgramPAT(NULL)");
        return;
    }
    int next = (pat->tsheader()->ContinuityCounter()+1)&0xf;
    pat->tsheader()->SetContinuityCounter(next);
    BufferedWrite(*(reinterpret_cast<const TSPacket*>(pat->tsheader())));
}

void R5000Recorder::HandleSingleProgramPMT(ProgramMapTable *pmt)
{
    if (!pmt)
    {
        LOG(VB_RECORD, LOG_DEBUG, LOC + "HandleSingleProgramPMT(NULL)");
        return;
    }
    int next = (pmt->tsheader()->ContinuityCounter()+1)&0xf;
    pmt->tsheader()->SetContinuityCounter(next);
    BufferedWrite(*(reinterpret_cast<const TSPacket*>(pmt->tsheader())));
}

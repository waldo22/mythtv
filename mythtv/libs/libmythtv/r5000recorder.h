/**
 *  R5000Recorder
 *  Copyright (c) 2005 by Jim Westfall
 *  Distributed as part of MythTV under GPL v2 and later.
 */

#ifndef _R5000RECORDER_H_
#define _R5000RECORDER_H_

// MythTV headers
#include "dtvrecorder.h"
#include "tspacket.h"
#include "streamlisteners.h"

class TVRec;
class R5000Channel;

/** \class R5000Recorder
 *  \brief This is a specialization of DTVRecorder used to
 *         handle DVB and ATSC streams from a firewire input.
 *
 *  \sa DTVRecorder
 */
class R5000Recorder : public DTVRecorder,
//                         public MPEGSingleProgramStreamListener,
                         public TSDataListener
{
    friend class MPEGStreamData;
    friend class TSPacketProcessor;

  public:
    R5000Recorder(TVRec *rec, R5000Channel *chan);
    virtual ~R5000Recorder();

    // Commands
    bool Open(void);
    void Close(void);

    void StartStreaming(void);
    void StopStreaming(void);

    void StartRecording(void);
    void run(void);
    bool PauseAndWait(int timeout = 100);

    void AddData(const unsigned char *data, uint dataSize);
    bool ProcessTSPacket(const TSPacket &tspacket);

    // Sets
    void SetOptionsFromProfile(RecordingProfile *profile,
                               const QString &videodev,
                               const QString &audiodev,
                               const QString &vbidev);
    void SetStreamData(void);

    // MPEG Single Program
    void HandleSingleProgramPAT(ProgramAssociationTable*);
    void HandleSingleProgramPMT(ProgramMapTable*);

  protected:
    R5000Recorder(TVRec *rec);

  private:
    R5000Channel       *channel;
    bool                   isopen;
    vector<unsigned char>  buffer;
};

#endif //  _R5000RECORDER_H_

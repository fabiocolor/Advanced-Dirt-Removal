#pragma once

#include "wrapper/plugin_api.h"

using namespace IOPlugin;

class DummyTrackWriter;
class DummyContainer : public IPluginContainerRef
{
public:
    static const uint8_t s_UUID[];

public:
    DummyContainer();

    static StatusCode s_Register(HostListRef* p_pList);

    StatusCode WriteVideo(uint32_t p_TrackIdx, HostBufferRef* p_pBuf);
    StatusCode WriteAudio(uint32_t p_TrackIdx, HostBufferRef* p_pBuf);

protected:
    // interface imp
    virtual StatusCode DoInit(HostPropertyCollectionRef* p_pProps);
    virtual StatusCode DoOpen(HostPropertyCollectionRef* p_pProps);

    virtual StatusCode DoAddTrack(HostPropertyCollectionRef* p_pProps, HostPropertyCollectionRef* p_pCodecProps, IPluginTrackBase** p_pTrack);
    virtual StatusCode DoClose();

protected:
    virtual ~DummyContainer();
    std::vector<DummyTrackWriter*> m_VideoTrackVec;
    std::vector<DummyTrackWriter*> m_AudioTrackVec;
};
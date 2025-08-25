#include "dummy_container.h"

#include <assert.h>

#include "x264_encoder.h"

using namespace IOPlugin;

// NOTE: When creating a plugin for release, please generate a new Container UUID in order to prevent conflicts with other third-party plugins.
const uint8_t DummyContainer::s_UUID[] = { 0xad, 0x90, 0x3d, 0x57, 0x02, 0xf2, 0x4a, 0xc1, 0x9d, 0xde, 0x8f, 0xac, 0xa3, 0x48, 0x80, 0x51 };

class DummyTrackWriter : public IPluginTrackBase, public IPluginTrackWriter
{
public:
    DummyTrackWriter(DummyContainer* p_pContainer, uint32_t p_TrackIdx, bool p_IsVideo)
        : IPluginTrackBase(p_pContainer)
        , m_TrackIdx(p_TrackIdx)
        , m_IsVideo(p_IsVideo)
    {
    }
    virtual ~DummyTrackWriter() = default;

public:
    virtual StatusCode DoWrite(HostBufferRef* p_pBuf)
    {
        DummyContainer* pContainer = dynamic_cast<DummyContainer*>(m_pContainer);
        assert(pContainer != NULL);

        return m_IsVideo ? pContainer->WriteVideo(m_TrackIdx, p_pBuf) : pContainer->WriteAudio(m_TrackIdx, p_pBuf);
    }

private:
    uint32_t m_TrackIdx;
    bool m_IsVideo;
};

StatusCode DummyContainer::s_Register(HostListRef* p_pList)
{
    HostPropertyCollectionRef containerInfo;
    if (!containerInfo.IsValid())
    {
        return errAlloc;
    }

    containerInfo.SetProperty(pIOPropUUID, propTypeUInt8, DummyContainer::s_UUID, 16);

    const char* pContainerName = "Dummy Container";
    containerInfo.SetProperty(pIOPropName, propTypeString, pContainerName, strlen(pContainerName));

    const uint32_t mediaType = (mediaAudio | mediaVideo);
    containerInfo.SetProperty(pIOPropMediaType, propTypeUInt32, &mediaType, 1);

    // set extension, otherwise default will be used. It is required even for folder path
    containerInfo.SetProperty(pIOPropContainerExt, propTypeString, "mxf", 3);

    if (!p_pList->Append(&containerInfo))
    {
        return errFail;
    }

    return errNone;
}

DummyContainer::DummyContainer()
{
}

DummyContainer::~DummyContainer()
{
}

StatusCode DummyContainer::DoInit(HostPropertyCollectionRef* p_pProps)
{
    return errNone;
}

StatusCode DummyContainer::DoOpen(HostPropertyCollectionRef* p_pProps)
{
    std::string path;
    p_pProps->GetString(pIOPropPath, path);

    g_Log(logLevelWarn, "Dummy Container Plugin :: open container with path: %s", path.c_str());

    return errNone;
}

StatusCode DummyContainer::DoAddTrack(HostPropertyCollectionRef* p_pProps, HostPropertyCollectionRef* p_pCodecProps, IPluginTrackBase** p_pTrack)
{
    uint32_t mediaType = 0;
    if (!p_pProps->GetUINT32(pIOPropMediaType, mediaType) || ((mediaType != mediaVideo) && (mediaType != mediaAudio)))
    {
        return errInvalidParam;
    }
    const bool isVideo = (mediaType == mediaVideo);

    // extract some params for demo purposes
    PropertyType propType;
    const void* pVal = NULL;
    int numVals = 0;

    //
    std::string name;
    p_pProps->GetString(pIOPropName, name);

    std::string reel;
    p_pProps->GetString(pIOPropReel, reel);

    // Edit rate shared between audio and video, frame rate for the clip
    uint32_t fpsNum = 0;
    uint32_t fpsDen = 0;
    if ((errNone == p_pProps->GetProperty(pIOPropFrameRate, &propType, &pVal, &numVals)) &&
        (propType == propTypeUInt32) && (pVal != NULL) && (numVals == 2))
    {
        const uint32_t* pFrameRate = static_cast<const uint32_t*>(pVal);
        fpsNum = pFrameRate[0];
        fpsDen = pFrameRate[1];
    }

    uint8_t isDropFrame = 0;
    p_pProps->GetUINT8(pIOPropFrameRateIsDrop, isDropFrame);

    double startTimeSeconds = 0.0; // start timecode, should be converted via frame rate
    p_pProps->GetDouble(pIOPropStartTime, startTimeSeconds);

    if (isVideo)
    {
        HostCodecConfigCommon trackCfg;
        trackCfg.Load(p_pProps);

        double duration = 0.0; // optional estimated duration in seconds
        p_pProps->GetDouble(pIOPropDuration, duration);

        double par = 1.0; // pixel aspect ratio
        p_pProps->GetDouble(pIOPropPAR, par);

        HostCodecConfigCommon codecCfg;
        codecCfg.Load(p_pCodecProps);

        // extract extra options from p_pCodecProps if needed such as magic cookie etc, whichever the codec has set

        // try to find the sample x264 plugin config entry if it was set
        std::string markerColor;
        p_pCodecProps->GetString("x264_enc_markers", markerColor);
        if (!markerColor.empty())
        {
            g_Log(logLevelWarn, "Dummy Container Plugin :: Using marker color: %s", markerColor.c_str());

            // get all markers data from track props and find those matching codec settings color
            PropertyType type = propTypeNull;
            const void* pVal = NULL;
            int numVals = 0;
            const StatusCode err = p_pProps->GetProperty(pIOPropMarkersBlob, &type, &pVal, &numVals);
            if ((err == errNone) && (type == propTypeUInt8) && (numVals > 0))
            {
                HostMarkersMap markers;
                if (markers.FromBuffer(static_cast<const uint8_t*>(pVal), numVals))
                {
                    const std::map<double, HostMarkerInfo>& markersMap = markers.GetMarkersMap();
                    for (auto it = markersMap.begin(); it != markersMap.end(); ++it)
                    {
                        const HostMarkerInfo& marker = it->second;
                        if (marker.GetColor() == markerColor)
                        {
                            g_Log(logLevelWarn, "Dummy Container Plugin :: Matchind marker at %f seconds with name: %s", marker.GetPositionSeconds(), marker.GetName().c_str());
                        }
                    }
                }
            }
        }
    }
    else
    {
        uint32_t samplingRate = 0;
        p_pProps->GetUINT32(pIOPropSamplingRate, samplingRate);

        // always interleaved signed int packing, specs:
        uint32_t numChannels = 0;
        p_pProps->GetUINT32(pIOPropNumChannels, numChannels);

        uint32_t bitDepth = 0; // storage depth
        p_pProps->GetUINT32(pIOPropBitDepth, bitDepth);

        uint32_t dataBitDepth = 0; // data bit depth - less or equal to storage depth, currently shall be equal
        p_pProps->GetUINT32(pIOPropBitsPerSample, dataBitDepth);

        uint8_t isFloat = 0; // 0 - integer data, 1 - floating point data
        p_pProps->GetUINT8(pIOPropIsFloat, isFloat);

        uint32_t channelLayout = 0; // AudioChannelLayout enum value
        p_pProps->GetUINT32(pIOPropAudioChannelLayout, channelLayout);
    }

    DummyTrackWriter* pTrack = new DummyTrackWriter(this, isVideo ? m_VideoTrackVec.size() : m_AudioTrackVec.size(), isVideo);
    pTrack->Retain();

    *p_pTrack = pTrack;
    if (isVideo)
    {
        m_VideoTrackVec.push_back(pTrack);
    }
    else
    {
        m_AudioTrackVec.push_back(pTrack);
    }
    return errNone;
}

StatusCode DummyContainer::DoClose()
{
    // release all tracks and dereferencing the container

    for (size_t i = 0; i < m_AudioTrackVec.size(); ++i)
    {
        m_AudioTrackVec[i]->Release();
    }
    m_AudioTrackVec.clear();

    for (size_t i = 0; i < m_VideoTrackVec.size(); ++i)
    {
        m_VideoTrackVec[i]->Release();
    }
    m_VideoTrackVec.clear();

    return errNone;
}

StatusCode DummyContainer::WriteVideo(uint32_t p_TrackIdx, HostBufferRef* p_pBuf)
{
    if (p_pBuf == NULL)
    {
        // flush
        return errNone;
    }

    int64_t pts = -1LL;
    int64_t dts = -1LL;
    double duration = 0.0; // optional, may be invalid and default to 1 frame in track fps
    p_pBuf->GetINT64(pIOPropPTS, pts);
    if (!p_pBuf->GetINT64(pIOPropDTS, dts))
    {
        dts = pts;
    }
    if (!p_pBuf->GetDouble(pIOPropDuration, duration))
    {
        // duration may be double or int64, if double - in seconds, if int64_t then in time base units (or fps)
        int64_t intDuration = 0;
        p_pBuf->GetINT64(pIOPropDuration, intDuration);
    }

    char* pBuf = NULL;
    size_t bufSize = 0;
    if (p_pBuf->LockBuffer(&pBuf, &bufSize))
    {
        // put the writing code here
        g_Log(logLevelWarn, "Dummy Container Plugin :: Write Video of %ld for track %d: pts: %lld, dts: %lld, duration: %f", bufSize, p_TrackIdx, pts, dts, duration);
        p_pBuf->UnlockBuffer();
    }

    return errNone;
}

StatusCode DummyContainer::WriteAudio(uint32_t p_TrackIdx, HostBufferRef* p_pBuf)
{
    if (p_pBuf == NULL)
    {
        // flush
        return errNone;
    }

    int64_t pts = -1LL;
    int64_t dts = -1LL;
    int64_t duration = 0;
    p_pBuf->GetINT64(pIOPropPTS, pts);
    if (!p_pBuf->GetINT64(pIOPropDTS, dts))
    {
        dts = pts;
    }

    p_pBuf->GetINT64(pIOPropDuration, duration); // may be double in seconds or int64 in samples

    // if pIOPropTimeBase is set then pts/dts/duration in seconds will be the corresponding value multiplied by time base
    // otherwise it's the value divided by frame rate of the track

    char* pBuf = NULL;
    size_t bufSize = 0;
    if (p_pBuf->LockBuffer(&pBuf, &bufSize))
    {
        // put the writing code here
        g_Log(logLevelWarn, "Dummy Container Plugin :: Write Audio of %ld for track %d: pts: %lld, dts: %lld, duration: %d", bufSize, p_TrackIdx, pts, dts, duration);
        p_pBuf->UnlockBuffer();
    }

    return errNone;
}

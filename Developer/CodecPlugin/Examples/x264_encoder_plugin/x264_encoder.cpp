#include "x264_encoder.h"

#include <assert.h>
#include <cstring>
#include <vector>
#include <stdint.h>

#include <algorithm>

#include "x264.h"

// NOTE: When creating a plugin for release, please generate a new Codec UUID in order to prevent conflicts with other third-party plugins.
const uint8_t X264Encoder::s_UUID[] = { 0x6a, 0x88, 0xe8, 0x41, 0xd8, 0xe4, 0x41, 0x4b, 0x87, 0x9e, 0xa4, 0x80, 0xfc, 0x90, 0xda, 0xb4 };

static std::string s_TmpFileName = "/tmp/x264_multipass.log";

class UISettingsController
{
public:
    UISettingsController()
    {
        InitDefaults();
    }

    explicit UISettingsController(const HostCodecConfigCommon& p_CommonProps)
        : m_CommonProps(p_CommonProps)
    {
        InitDefaults();
    }

    ~UISettingsController()
    {
    }

    void Load(IPropertyProvider* p_pValues)
    {
        uint8_t val8 = 0;
        p_pValues->GetUINT8("x264_reset", val8);
        if (val8 != 0)
        {
            *this = UISettingsController();
            return;
        }

        p_pValues->GetINT32("x264_enc_preset", m_EncPreset);
        p_pValues->GetINT32("x264_tune", m_Tune);
        p_pValues->GetINT32("x264_profile", m_Profile);

        p_pValues->GetINT32("x264_num_passes", m_NumPasses);
        p_pValues->GetINT32("x264_q_mode", m_QualityMode);
        p_pValues->GetINT32("x264_qp", m_QP);
        p_pValues->GetINT32("x264_bitrate", m_BitRate);
        p_pValues->GetString("x264_enc_markers", m_MarkerColor);
    }

    StatusCode Render(HostListRef* p_pSettingsList)
    {
        StatusCode err = RenderGeneral(p_pSettingsList);
        if (err != errNone)
        {
            return err;
        }

        {
            HostUIConfigEntryRef item("x264_separator");
            item.MakeSeparator();
            if (!item.IsSuccess() || !p_pSettingsList->Append(&item))
            {
                g_Log(logLevelError, "X264 Plugin :: Failed to add a separator entry");
                return errFail;
            }
        }

        err = RenderQuality(p_pSettingsList);
        if (err != errNone)
        {
            return err;
        }

        {
            HostUIConfigEntryRef item("x264_reset");
            item.MakeButton("Reset");
            item.SetTriggersUpdate(true);
            if (!item.IsSuccess() || !p_pSettingsList->Append(&item))
            {
                g_Log(logLevelError, "X264 Plugin :: Failed to populate the button entry");
                return errFail;
            }
        }

        return errNone;
    }

private:
    void InitDefaults()
    {
        m_EncPreset = 1;
        m_Tune = 1;
        m_Profile = 0;
        m_NumPasses = 1;
        m_QualityMode = X264_RC_CQP;
        m_QP = 25;
        m_BitRate = 0;
    }

    StatusCode RenderGeneral(HostListRef* p_pSettingsList)
    {
        if (0)
        {
            HostUIConfigEntryRef item("x264_lbl_general");
            item.MakeLabel("General Settings");

            if (!item.IsSuccess() || !p_pSettingsList->Append(&item))
            {
                g_Log(logLevelError, "X264 Plugin :: Failed to populate general label entry");
                return errFail;
            }
        }

        // Markers selection
        if (m_CommonProps.GetContainer().size() >= 32)
        {
            // plugin container in string "plugin_UUID:container_UUID" or "container_UUID" with UUID 32 characters
            // or "mov", "mp4" etc if non-plugin container
            HostUIConfigEntryRef item("x264_enc_markers");
            item.MakeMarkerColorSelector("Chapter Marker", "Marker 1", m_MarkerColor);
            if (!item.IsSuccess() || !p_pSettingsList->Append(&item))
            {
                g_Log(logLevelError, "X264 Plugin :: Failed to populate encoder preset UI entry");
                assert(false);
                return errFail;
            }
        }

        // Preset combobox
        {
            HostUIConfigEntryRef item("x264_enc_preset");

            std::vector<std::string> textsVec;
            std::vector<int> valuesVec;

            int32_t curVal = 1;
            const char* const* pPresets = x264_preset_names;
            while (*pPresets != 0)
            {
                valuesVec.push_back(curVal++);
                textsVec.push_back(*pPresets);
                ++pPresets;
            }

            item.MakeComboBox("Encoder Preset", textsVec, valuesVec, m_EncPreset);
            if (!item.IsSuccess() || !p_pSettingsList->Append(&item))
            {
                g_Log(logLevelError, "X264 Plugin :: Failed to populate encoder preset UI entry");
                return errFail;
            }
        }

        // Tune combobox
        {
            HostUIConfigEntryRef item("x264_tune");

            std::vector<std::string> textsVec;
            std::vector<int> valuesVec;

            int32_t curVal = 1;
            const char* const* pPresets = x264_tune_names;
            while (*pPresets != 0)
            {
                valuesVec.push_back(curVal++);
                textsVec.push_back(*pPresets);
                ++pPresets;
            }

            item.MakeComboBox("Tune", textsVec, valuesVec, m_Tune);
            if (!item.IsSuccess() || !p_pSettingsList->Append(&item))
            {
                g_Log(logLevelError, "X264 Plugin :: Failed to populate tune UI entry");
                return errFail;
            }
        }

        // Profile combobox
        {
            HostUIConfigEntryRef item("x264_profile");

            std::vector<std::string> textsVec;
            std::vector<int> valuesVec;

            textsVec.push_back("Auto");
            valuesVec.push_back(3);
            textsVec.push_back("Baseline");
            valuesVec.push_back(1);
            textsVec.push_back("Main");
            valuesVec.push_back(2);
            textsVec.push_back("High");
            valuesVec.push_back(3);
            textsVec.push_back("High 422");
            valuesVec.push_back(4);

            item.MakeComboBox("h264 Profile", textsVec, valuesVec, m_Profile);
            if (!item.IsSuccess() || !p_pSettingsList->Append(&item))
            {
                g_Log(logLevelError, "X264 Plugin :: Failed to populate profile UI entry");
                return errFail;
            }
        }

        return errNone;
    }

    StatusCode RenderQuality(HostListRef* p_pSettingsList)
    {
        if (0)
        {
            HostUIConfigEntryRef item("x264_lbl_quality");
            item.MakeLabel("Quality Settings");

            if (!item.IsSuccess() || !p_pSettingsList->Append(&item))
            {
                g_Log(logLevelError, "X264 Plugin :: Failed to populate quality label entry");
                return errFail;
            }
        }

        {
            HostUIConfigEntryRef item("x264_num_passes");

            std::vector<std::string> textsVec;
            std::vector<int> valuesVec;

            textsVec.push_back("1-Pass");
            valuesVec.push_back(1);
            textsVec.push_back("2-Pass");
            valuesVec.push_back(2);

            item.MakeComboBox("Passes", textsVec, valuesVec, m_NumPasses);
            item.SetTriggersUpdate(true);
            if (!item.IsSuccess() || !p_pSettingsList->Append(&item))
            {
                g_Log(logLevelError, "X264 Plugin :: Failed to populate passes UI entry");
                return errFail;
            }
        }

        if (m_NumPasses < 2)
        {
            HostUIConfigEntryRef item("x264_q_mode");

            std::vector<std::string> textsVec;
            std::vector<int> valuesVec;

            textsVec.push_back("Constant Quality");
            valuesVec.push_back(X264_RC_CQP);
            textsVec.push_back("Constant Rate Factor");
            valuesVec.push_back(X264_RC_CRF);

            textsVec.push_back("Variable Rate");
            valuesVec.push_back(X264_RC_ABR);

            item.MakeRadioBox("Quality Control", textsVec, valuesVec, GetQualityMode());
            item.SetTriggersUpdate(true);

            if (!item.IsSuccess() || !p_pSettingsList->Append(&item))
            {
                g_Log(logLevelError, "X264 Plugin :: Failed to populate quality UI entry");
                return errFail;
            }
        }

        {
            HostUIConfigEntryRef item("x264_qp");
            const char* pLabel = NULL;
            if (m_QP < 17)
            {
                pLabel = "(high)";
            }
            else if (m_QP < 34)
            {
                pLabel = "(medium)";
            }
            else
            {
                pLabel = "(low)";
            }
            item.MakeSlider("Factor", pLabel, m_QP, 1, 51, 25);
            item.SetTriggersUpdate(true);
            item.SetHidden((m_QualityMode == X264_RC_ABR) || (m_NumPasses > 1));
            if (!item.IsSuccess() || !p_pSettingsList->Append(&item))
            {
                g_Log(logLevelError, "X264 Plugin :: Failed to populate qp slider UI entry");
                return errFail;
            }
        }

        {
            HostUIConfigEntryRef item("x264_bitrate");
            item.MakeSlider("Bit Rate", "KBps", m_BitRate, 100, 3000, 1);
            item.SetHidden((m_QualityMode != X264_RC_ABR) && (m_NumPasses < 2));

            if (!item.IsSuccess() || !p_pSettingsList->Append(&item))
            {
                g_Log(logLevelError, "X264 Plugin :: Failed to populate bitrate slider UI entry");
                return errFail;
            }
        }

        return errNone;
    }

public:
    int32_t GetNumPasses()
    {
        return m_NumPasses;
    }

    const char* GetEncPreset() const
    {
        return x264_preset_names[m_EncPreset];
    }

    const char* GetTune() const
    {
        return x264_tune_names[m_Tune];
    }

    const char* GetProfile() const
    {
        const char* pProfile = NULL;
        switch (m_Profile)
        {
            case 1:
                pProfile = x264_profile_names[0];
                break;
            case 2:
                pProfile = x264_profile_names[1];
                break;
            case 3:
                pProfile = x264_profile_names[2];
                break;
            case 4:
                pProfile = x264_profile_names[4];
            default:
                break;
        }

        return pProfile;
    }

    int32_t GetQualityMode() const
    {
        return (m_NumPasses == 2) ? X264_RC_ABR : m_QualityMode;
    }

    int32_t GetQP() const
    {
        return std::max<int>(0, m_QP);
    }

    int32_t GetBitRate() const
    {
        return m_BitRate * 8;
    }

    const std::string& GetMarkerColor() const
    {
        return m_MarkerColor;
    }

private:
    HostCodecConfigCommon m_CommonProps;
    std::string m_MarkerColor;
    int32_t m_EncPreset;
    int32_t m_Tune;
    int32_t m_Profile;

    int32_t m_NumPasses;
    int32_t m_QualityMode;
    int32_t m_QP;
    int32_t m_BitRate;
};

StatusCode X264Encoder::s_GetEncoderSettings(HostPropertyCollectionRef* p_pValues, HostListRef* p_pSettingsList)
{
    HostCodecConfigCommon commonProps;
    commonProps.Load(p_pValues);

    UISettingsController settings(commonProps);
    settings.Load(p_pValues);

    return settings.Render(p_pSettingsList);
}

StatusCode X264Encoder::s_RegisterCodecs(HostListRef* p_pList)
{
    // add x264 encoder
    HostPropertyCollectionRef codecInfo;
    if (!codecInfo.IsValid())
    {
        return errAlloc;
    }

    codecInfo.SetProperty(pIOPropUUID, propTypeUInt8, X264Encoder::s_UUID, 16);

    const char* pCodecName = "x264 Plugin";
    codecInfo.SetProperty(pIOPropName, propTypeString, pCodecName, strlen(pCodecName));

    const char* pCodecGroup = "Plugin AVC";
    codecInfo.SetProperty(pIOPropGroup, propTypeString, pCodecGroup, strlen(pCodecGroup));

    uint32_t val = 'avc1';
    codecInfo.SetProperty(pIOPropFourCC, propTypeUInt32, &val, 1);

    val = mediaVideo;
    codecInfo.SetProperty(pIOPropMediaType, propTypeUInt32, &val, 1);

    val = dirEncode;
    codecInfo.SetProperty(pIOPropCodecDirection, propTypeUInt32, &val, 1);

    val = clrUYVY;
    codecInfo.SetProperty(pIOPropColorModel, propTypeUInt32, &val, 1);

    // Optionally enable both Data Ranges, Video will be default for "Auto" thus "0" value goes first
    std::vector<uint8_t> dataRangeVec;
    dataRangeVec.push_back(0);
    dataRangeVec.push_back(1);
    codecInfo.SetProperty(pIOPropDataRange, propTypeUInt8, dataRangeVec.data(), dataRangeVec.size());

    uint8_t hSampling = 2;
    uint8_t vSampling = 1;
    codecInfo.SetProperty(pIOPropHSubsampling, propTypeUInt8, &hSampling, 1);
    codecInfo.SetProperty(pIOPropVSubsampling, propTypeUInt8, &vSampling, 1);

    val = 8;
    codecInfo.SetProperty(pIOPropBitDepth, propTypeUInt32, &val, 1);
    codecInfo.SetProperty(pIOPropBitsPerSample, propTypeUInt32, &val, 1);

    const uint32_t temp = 0;
    codecInfo.SetProperty(pIOPropTemporalReordering, propTypeUInt32, &temp, 1);

    const uint8_t fieldSupport = (fieldProgressive | fieldTop | fieldBottom);
    codecInfo.SetProperty(pIOPropFieldOrder, propTypeUInt8, &fieldSupport, 1);

    // fill supported containers, would one need the plugin container to handle the encoding internally,
    // just create a dummy passthrough codec which will pass the buffer for output unchanged
    // but if nothing extraordinary is required let Resolve trigger the codec encode function and pass the output buffer to the writer
    std::vector<std::string> containerVec;
    containerVec.push_back("mp4");
    containerVec.push_back("mov");
    containerVec.push_back("ad903d5702f24ac19dde8faca3488051");
    std::string valStrings;
    for (size_t i = 0; i < containerVec.size(); ++i)
    {
        valStrings.append(containerVec[i]);
        if (i < (containerVec.size() - 1))
        {
            valStrings.append(1, '\0');
        }
    }

    codecInfo.SetProperty(pIOPropContainerList, propTypeString, valStrings.c_str(), valStrings.size());

    if (!p_pList->Append(&codecInfo))
    {
        return errFail;
    }

    return errNone;
}

X264Encoder::X264Encoder()
    : m_pContext(0)
    , m_ColorModel(-1)
    , m_IsMultiPass(false)
    , m_PassesDone(0)
    , m_Error(errNone)
{
}

X264Encoder::~X264Encoder()
{
    if (m_pContext)
    {
        x264_encoder_close(m_pContext);
        m_pContext = 0;
    }
}

void X264Encoder::DoFlush()
{
    if (m_Error != errNone)
    {
        return;
    }

    StatusCode sts = DoProcess(NULL);
    while (sts == errNone)
    {
        sts = DoProcess(NULL);
    }

    ++m_PassesDone;

    if (!m_IsMultiPass || (m_PassesDone > 1))
    {
        // no need to do anything
        return;
    }

    if (m_PassesDone == 1)
    {
        // setup new pass
        SetupContext(true /* isFinalPass */);
    }
}

StatusCode X264Encoder::DoInit(HostPropertyCollectionRef* p_pProps)
{
    // fill average frame size if have byte rate
    uint32_t val = clrUYVY;
    p_pProps->SetProperty(pIOPropColorModel, propTypeUInt32, &val, 1);

    uint8_t hSampling = 2;
    uint8_t vSampling = 1;
    p_pProps->SetProperty(pIOPropHSubsampling, propTypeUInt8, &hSampling, 1);
    p_pProps->SetProperty(pIOPropVSubsampling, propTypeUInt8, &vSampling, 1);

    return errNone;
}

void X264Encoder::SetupContext(bool p_IsFinalPass)
{
    if (m_pContext)
    {
        x264_encoder_close(m_pContext);
        m_pContext = 0;
    }

    x264_param_t param;

    const char* pProfile = m_pSettings->GetProfile();
    m_ColorModel = (((pProfile != NULL) && (strcmp(pProfile, "high422") == 0)) ? X264_CSP_UYVY : X264_CSP_NV12);

    x264_param_default_preset(&param, m_pSettings->GetEncPreset(), m_pSettings->GetTune());
    param.i_csp              = m_ColorModel;
    param.i_width            = m_CommonProps.GetWidth();
    param.i_height           = m_CommonProps.GetHeight();
    param.b_vfr_input        = 0;
    param.i_bitdepth         = 8;
    param.i_bframe           = 2;
    param.i_bframe_adaptive  = X264_B_ADAPT_NONE;
    param.i_bframe_pyramid   = X264_B_PYRAMID_NORMAL;
    param.b_open_gop         = 1;
    param.i_keyint_max       = 12;
    param.i_fps_num          = m_CommonProps.GetFrameRateNum();
    param.i_fps_den          = m_CommonProps.GetFrameRateDen();
    param.b_repeat_headers   = 0;
    param.b_annexb           = 0;
    param.b_stitchable       = 1;
    param.vui.b_fullrange    = m_CommonProps.IsFullRange();

    if (strcmp(pProfile, "baseline") != 0)
    {
        const uint8_t fieldOrder = m_CommonProps.GetFieldOrder();
        param.b_interlaced = ((fieldOrder == fieldTop) || (fieldOrder == fieldBottom));
    }

    param.rc.i_rc_method = m_pSettings->GetQualityMode();
    if (!m_IsMultiPass && (param.rc.i_rc_method != X264_RC_ABR))
    {
        const int qp = m_pSettings->GetQP();

        param.rc.i_qp_constant = qp;
        param.rc.f_rf_constant = std::min<int>(50, qp);
        param.rc.f_rf_constant_max = std::min<int>(51, qp + 5);
    }
    else if (param.rc.i_rc_method == X264_RC_ABR)
    {
        param.rc.i_bitrate = m_pSettings->GetBitRate();
        param.rc.i_vbv_buffer_size = m_pSettings->GetBitRate();
        param.rc.i_vbv_max_bitrate = m_pSettings->GetBitRate();
    }

    if (m_IsMultiPass)
    {
        if (p_IsFinalPass && (m_PassesDone > 0))
        {
            param.rc.b_stat_read = 1;
            param.rc.b_stat_write = 0;
            x264_param_apply_fastfirstpass(&param);
        }
        else if (!p_IsFinalPass)
        {
            param.rc.b_stat_read = 0;
            param.rc.b_stat_write = 1;
        }

        param.rc.psz_stat_out = &s_TmpFileName[0];
        param.rc.psz_stat_in = &s_TmpFileName[0];
    }

    if (pProfile != NULL)
    {
        int resCode = x264_param_apply_profile(&param, pProfile);
        if (resCode != 0)
        {
            m_Error = errFail;
            return;
        }
    }

    m_pContext = x264_encoder_open(&param);
    m_Error = ((m_pContext != NULL) ? errNone : errFail);
}

StatusCode X264Encoder::DoOpen(HostBufferRef* p_pBuff)
{
    assert(m_pContext == NULL);

    m_CommonProps.Load(p_pBuff);

    const std::string& path = m_CommonProps.GetPath();
    if (!path.empty())
    {
        s_TmpFileName = path;
        s_TmpFileName.append(".pass.log");
    }
    else
    {
        s_TmpFileName = "/tmp/x264_multipass.log";
    }

    m_pSettings.reset(new UISettingsController(m_CommonProps));
    m_pSettings->Load(p_pBuff);

    uint8_t isMultiPass = 0;
    if (m_pSettings->GetNumPasses() == 2)
    {
        m_IsMultiPass = true;
        isMultiPass = 1;
    }

    // @TODO: Need to fill maximum output size pIOPropInitFrameBytes if know or otherwise can be larger than an image
    StatusCode sts = p_pBuff->SetProperty(pIOPropMultiPass, propTypeUInt8, &isMultiPass, 1);
    if (sts != errNone)
    {
        return sts;
    }

    // setup context for cookie
    SetupContext(true /* isFinalPass */);
    if (m_Error != errNone)
    {
        return m_Error;
    }

    x264_nal_t* pNals = 0;
    int numNals = 0;
    int hdrBytes = x264_encoder_headers(m_pContext, &pNals, &numNals);
    if (hdrBytes > 0)
    {
        std::vector<uint8_t> cookie;
        for (int i = 0; i < numNals; ++i)
        {
            if (pNals[i].i_type == NAL_SEI)
            {
                continue;
            }

            pNals[i].p_payload[0] = 0;
            pNals[i].p_payload[1] = 0;
            pNals[i].p_payload[2] = 0;
            pNals[i].p_payload[3] = 1;
            cookie.insert(cookie.end(), pNals[i].p_payload, pNals[i].p_payload + pNals[i].i_payload);
        }

        if (!cookie.empty())
        {
            p_pBuff->SetProperty(pIOPropMagicCookie, propTypeUInt8, &cookie[0], cookie.size());
            uint32_t fourCC = 0;
            p_pBuff->SetProperty(pIOPropMagicCookieType, propTypeUInt32, &fourCC, 1);
        }
    }

    uint32_t temporal = 2;
    p_pBuff->SetProperty(pIOPropTemporalReordering, propTypeUInt32, &temporal, 1);

    if (isMultiPass)
    {
        SetupContext(false /* isFinalPass */);
        if (m_Error != errNone)
        {
            return m_Error;
        }
    }

    return errNone;
}

StatusCode X264Encoder::DoProcess(HostBufferRef* p_pBuff)
{
    if (m_Error != errNone)
    {
        return m_Error;
    }

    const int numDelayedFrames = x264_encoder_delayed_frames(m_pContext);
    if (((p_pBuff == NULL) || !p_pBuff->IsValid()) && (numDelayedFrames == 0))
    {
        return errMoreData;
    }

    x264_picture_t outPic;
    x264_picture_init(&outPic);
    x264_nal_t* pNals = 0;
    int numNals = 0;
    int bytes = 0;
    int64_t pts = -1;
    if ((p_pBuff == NULL) || !p_pBuff->IsValid())
    {
        // flushing
        bytes = x264_encoder_encode(m_pContext, &pNals, &numNals, 0, &outPic);
    }
    else
    {
        char* pBuf = NULL;
        size_t bufSize = 0;
        if (!p_pBuff->LockBuffer(&pBuf, &bufSize))
        {
            g_Log(logLevelError, "X264 Plugin :: Failed to lock the buffer");
            return errFail;
        }

        if (pBuf == NULL || bufSize == 0)
        {
            g_Log(logLevelError, "X264 Plugin :: No data to encode");
            p_pBuff->UnlockBuffer();
            return errUnsupported;
        }

        uint32_t width = 0;
        uint32_t height = 0;
        if (!p_pBuff->GetUINT32(pIOPropWidth, width) || !p_pBuff->GetUINT32(pIOPropHeight, height))
        {
            g_Log(logLevelError, "X264 Plugin :: Width/Height not set when encoding the frame");
            return errNoParam;
        }

        if (!p_pBuff->GetINT64(pIOPropPTS, pts))
        {
            g_Log(logLevelError, "X264 Plugin :: PTS not set when encoding the frame");
            return errNoParam;
        }

        x264_picture_t inPic;
        x264_picture_init(&inPic);
        inPic.i_pts = pts;
        inPic.img.plane[1] = 0;
        inPic.img.plane[2] = 0;
        inPic.img.plane[3] = 0;
        inPic.img.i_csp = m_ColorModel;
        if (m_ColorModel == X264_CSP_UYVY)
        {
            inPic.img.i_plane = 1;
            inPic.img.i_stride[0] = width * 2;
            inPic.img.plane[0] = reinterpret_cast<uint8_t*>(const_cast<char*>(pBuf));
            bytes = x264_encoder_encode(m_pContext, &pNals, &numNals, &inPic, &outPic);
            p_pBuff->UnlockBuffer();
        }
        else
        {
            // repack
            std::vector<uint8_t> yPlane;
            yPlane.reserve(width * height);
            std::vector<uint8_t> uvPlane;
            uvPlane.reserve(width * height / 2);
            const uint8_t* pSrc = reinterpret_cast<uint8_t*>(const_cast<char*>(pBuf));

            for (int h = 0; h < height; ++h)
            {
                for (int w = 0; w < width; w += 2)
                {
                    yPlane.push_back(pSrc[1]);
                    yPlane.push_back(pSrc[3]);
                    if ((h % 2) == 0)
                    {
                        uvPlane.push_back(pSrc[0]);
                        uvPlane.push_back(pSrc[2]);
                    }

                    pSrc += 4;
                }
            }

            p_pBuff->UnlockBuffer();

            inPic.img.i_plane = 2;
            inPic.img.i_stride[0] = width;
            inPic.img.plane[0] = yPlane.data();
            inPic.img.i_stride[1] = width;
            inPic.img.plane[1] = uvPlane.data();
            bytes = x264_encoder_encode(m_pContext, &pNals, &numNals, &inPic, &outPic);
        }
    }

    if (bytes < 0)
    {
        return errFail;
    }
    else if (bytes == 0)
    {
        return errMoreData;
    }
    else if (m_IsMultiPass && (m_PassesDone == 0))
    {
        return errNone;
    }

    // fill the out buffer and info
    HostBufferRef outBuf(false);
    if (!outBuf.IsValid() || !outBuf.Resize(bytes))
    {
        return errAlloc;
    }

    char* pOutBuf = NULL;
    size_t outBufSize = 0;
    if (!outBuf.LockBuffer(&pOutBuf, &outBufSize))
    {
        return errAlloc;
    }

    assert(outBufSize == bytes);

    memcpy(pOutBuf, pNals[0].p_payload, bytes);

    int64_t ts = outPic.i_pts;
    outBuf.SetProperty(pIOPropPTS, propTypeInt64, &ts, 1);

    ts = outPic.i_dts;
    outBuf.SetProperty(pIOPropDTS, propTypeInt64, &ts, 1);

    uint8_t isKeyFrame = IS_X264_TYPE_I(outPic.i_type) ? 1 : 0;
    outBuf.SetProperty(pIOPropIsKeyFrame, propTypeUInt8, &isKeyFrame, 1);

    return m_pCallback->SendOutput(&outBuf);
}

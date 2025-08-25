#include "TemporalBlurPlugin.h"

#include <stdio.h>
#include <string>

#include "ofxsImageEffect.h"
#include "ofxsMultiThread.h"
#include "ofxsProcessing.h"
#include "ofxsLog.h"

#define kPluginName "TemporalBlur"
#define kPluginGrouping "OpenFX Sample"
#define kPluginDescription "TemporalBlur Sample plugin accessing neighbor frames"
#define kPluginIdentifier "com.OpenFXSample.TemporalBlur"
#define kPluginVersionMajor 1
#define kPluginVersionMinor 0

#define kSupportsTiles false
#define kSupportsMultiResolution false
#define kSupportsMultipleClipPARs false

////////////////////////////////////////////////////////////////////////////////

class TemporalBlurProcessor : public OFX::ImageProcessor
{
public:
    explicit TemporalBlurProcessor(OFX::ImageEffect& p_Instance);

    virtual void processImagesCUDA();
    virtual void processImagesOpenCL();
    virtual void processImagesMetal();
    virtual void multiThreadProcessImages(OfxRectI p_ProcWindow);

    void setSrcImgs(OFX::Image* p_PrevSrcImg, OFX::Image* p_CurrSrcImg, OFX::Image* p_NextSrcImg);
    void setParams(float p_Blend);

private:
    OFX::Image* _currSrcImg;
    OFX::Image* _prevSrcImg;
    OFX::Image* _nextSrcImg;
    float _blend;
};

TemporalBlurProcessor::TemporalBlurProcessor(OFX::ImageEffect& p_Instance)
    : OFX::ImageProcessor(p_Instance)
{
}

#define PROCESS_IMAGE_IMPL()                                         \
const OfxRectI& bounds = _currSrcImg->getBounds();                   \
const int width        = bounds.x2 - bounds.x1;                      \
const int height       = bounds.y2 - bounds.y1;                      \
                                                                     \
float* curInput  = static_cast<float*>(_currSrcImg->getPixelData()); \
float* prevInput = static_cast<float*>(_prevSrcImg->getPixelData()); \
float* nextInput = static_cast<float*>(_nextSrcImg->getPixelData()); \
float* output    = static_cast<float*>(_dstImg->getPixelData());

#ifndef __APPLE__
extern void RunCudaKernel(void* p_Stream, int p_Width, int p_Height, float p_Blend, const float* p_PrevInput, const float* p_CurrInput, const float* p_NextInput, float* p_Output);
#endif

void TemporalBlurProcessor::processImagesCUDA()
{
#ifndef __APPLE__
    PROCESS_IMAGE_IMPL()
    RunCudaKernel(_pCudaStream, width, height, _blend, prevInput, curInput, nextInput, output);
#endif
}

#ifdef __APPLE__
extern void RunMetalKernel(void* p_CmdQ, int p_Width, int p_Height, float p_Blend, const float* p_PrevInput, const float* p_CurrInput, const float* p_NextInput, float* p_Output);
#endif

void TemporalBlurProcessor::processImagesMetal()
{
#ifdef __APPLE__
    PROCESS_IMAGE_IMPL()
    RunMetalKernel(_pMetalCmdQ, width, height, _blend, prevInput, curInput, nextInput, output);
#endif
}

extern void RunOpenCLKernel(void* p_CmdQ, int p_Width, int p_Height, float p_Blend, const float* p_PrevInput, const float* p_CurrInput, const float* p_NextInput, float* p_Output);
void TemporalBlurProcessor::processImagesOpenCL()
{
    PROCESS_IMAGE_IMPL()
    RunOpenCLKernel(_pOpenCLCmdQ, width, height, _blend, prevInput, curInput, nextInput, output);
}

void TemporalBlurProcessor::multiThreadProcessImages(OfxRectI p_ProcWindow)
{
    for (int y = p_ProcWindow.y1; y < p_ProcWindow.y2; ++y)
    {
        if (_effect.abort()) break;

        float* dstPix = static_cast<float*>(_dstImg->getPixelAddress(p_ProcWindow.x1, y));

        for (int x = p_ProcWindow.x1; x < p_ProcWindow.x2; ++x)
        {
            float* currPix = static_cast<float*>(_currSrcImg ? _currSrcImg->getPixelAddress(x, y) : 0);
            float* prevPix = static_cast<float*>(_prevSrcImg ? _prevSrcImg->getPixelAddress(x, y) : 0);
            float* nextPix = static_cast<float*>(_nextSrcImg ? _nextSrcImg->getPixelAddress(x, y) : 0);

            // do we have a source image
            if (currPix)
            {
                for (int c = 0; c < 4; ++c)
                {
                    // Blend the inputs layer by layer in the order Black Screen, p_PrevInput, p_CurrInput and p_NextInput.
                    dstPix[c] = _blend * (_blend * (1.0f - _blend) * prevPix[c] + (1.0f - _blend) * currPix[c]) + (1.0f - _blend) * nextPix[c];
                }
            }
            else
            {
                // no src pixel here, be black and transparent
                for (int c = 0; c < 4; ++c)
                {
                    dstPix[c] = 0;
                }
            }

            // increment the dst pixel
            dstPix += 4;
        }
    }
}

void TemporalBlurProcessor::setSrcImgs(OFX::Image* p_PrevSrcImg, OFX::Image* p_CurrSrcImg, OFX::Image* p_NextSrcImg)
{
    _currSrcImg = p_CurrSrcImg;
    _prevSrcImg = p_PrevSrcImg;
    _nextSrcImg = p_NextSrcImg;
}

void TemporalBlurProcessor::setParams(float p_Blend)
{
    _blend = p_Blend;
}

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class TemporalBlurPlugin : public OFX::ImageEffect
{
public:
    explicit TemporalBlurPlugin(OfxImageEffectHandle p_Handle);

    /* Override the render */
    virtual void render(const OFX::RenderArguments& p_Args);

    /* Override the getFramesNeeded */
    virtual void getFramesNeeded(const OFX::FramesNeededArguments &pArgs, OFX::FramesNeededSetter &pFramesNeededSetter);

    /* Set up and run a processor */
    void setupAndProcess(TemporalBlurProcessor &p_TemporalBlur, const OFX::RenderArguments& p_Args);

private:
    // Does not own the following pointers
    OFX::Clip* m_DstClip;
    OFX::Clip* m_SrcClip;

    OFX::DoubleParam* m_BlendParam;
    OFX::IntParam* m_IntervalParam;
};

TemporalBlurPlugin::TemporalBlurPlugin(OfxImageEffectHandle p_Handle)
    : ImageEffect(p_Handle)
{
    m_DstClip = fetchClip(kOfxImageEffectOutputClipName);
    m_SrcClip = fetchClip(kOfxImageEffectSimpleSourceClipName);

    m_BlendParam    = fetchDoubleParam("blend");
    m_IntervalParam = fetchIntParam("interval");
}

// If the plugin wants change the frames needed on an input clip from the default values (which is the same as the frame to be renderred),
// it should do so by calling the OFX::FramesNeededSetter::setFramesNeeded function with the desired frame range.
void TemporalBlurPlugin::getFramesNeeded(const OFX::FramesNeededArguments &p_Args, OFX::FramesNeededSetter &p_FramesNeededSetter)
{
    const int interval = m_IntervalParam->getValueAtTime(p_Args.time);

    OfxRangeD frameRange;
    frameRange.min = p_Args.time - interval;
    frameRange.max = p_Args.time + interval;

    p_FramesNeededSetter.setFramesNeeded(*m_SrcClip, frameRange);
}

void TemporalBlurPlugin::render(const OFX::RenderArguments& p_Args)
{
    if ((m_DstClip->getPixelDepth() == OFX::eBitDepthFloat) && (m_DstClip->getPixelComponents() == OFX::ePixelComponentRGBA))
    {
        TemporalBlurProcessor temporalBlur(*this);
        setupAndProcess(temporalBlur, p_Args);
    }
    else
    {
        OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}

void TemporalBlurPlugin::setupAndProcess(TemporalBlurProcessor& p_TemporalBlur, const OFX::RenderArguments& p_Args)
{
    const int currFrameNum = p_Args.time;

    // Get the dst image
    std::unique_ptr<OFX::Image> dst(m_DstClip->fetchImage(currFrameNum));

    const OFX::BitDepthEnum dstBitDepth         = dst->getPixelDepth();
    const OFX::PixelComponentEnum dstComponents = dst->getPixelComponents();

    const int interval = m_IntervalParam->getValueAtTime(currFrameNum);

    // Get the src images
    std::unique_ptr<OFX::Image> prevSrc(m_SrcClip->fetchImage(currFrameNum - interval));
    std::unique_ptr<OFX::Image> src(m_SrcClip->fetchImage(currFrameNum));
    std::unique_ptr<OFX::Image> nextSrc(m_SrcClip->fetchImage(currFrameNum + interval));

    const OFX::BitDepthEnum srcBitDepth         = src->getPixelDepth();
    const OFX::PixelComponentEnum srcComponents = src->getPixelComponents();

    // Check to see if the bit depth and number of components are the same
    if ((srcBitDepth != dstBitDepth) || (srcComponents != dstComponents))
    {
        OFX::throwSuiteStatusException(kOfxStatErrValue);
    }

    // Set the images
    p_TemporalBlur.setDstImg(dst.get());

    p_TemporalBlur.setSrcImgs(prevSrc.get(), src.get(), nextSrc.get());

    // Setup OpenCL, CUDA and Metal Render arguments
    p_TemporalBlur.setGPURenderArgs(p_Args);

    // Set the render window
    p_TemporalBlur.setRenderWindow(p_Args.renderWindow);

    // Set the scales
    p_TemporalBlur.setParams(m_BlendParam->getValueAtTime(p_Args.time));

    // Call the base class process member, this will call the derived templated process code
    p_TemporalBlur.process();
}

////////////////////////////////////////////////////////////////////////////////
TemporalBlurPluginFactory::TemporalBlurPluginFactory()
    : OFX::PluginFactoryHelper<TemporalBlurPluginFactory>(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor)
{
}

void TemporalBlurPluginFactory::describe(OFX::ImageEffectDescriptor& p_Desc)
{
    // Basic labels
    p_Desc.setLabels(kPluginName, kPluginName, kPluginName);
    p_Desc.setPluginGrouping(kPluginGrouping);
    p_Desc.setPluginDescription(kPluginDescription);

    // Add the supported contexts, only filter at the moment
    p_Desc.addSupportedContext(OFX::eContextFilter);
    p_Desc.addSupportedContext(OFX::eContextGeneral);

    // Add supported pixel depths
    p_Desc.addSupportedBitDepth(OFX::eBitDepthFloat);

    // Set a few flags
    p_Desc.setSingleInstance(false);
    p_Desc.setHostFrameThreading(false);
    p_Desc.setSupportsMultiResolution(kSupportsMultiResolution);
    p_Desc.setSupportsTiles(kSupportsTiles);
    p_Desc.setTemporalClipAccess(true);
    p_Desc.setRenderTwiceAlways(false);
    p_Desc.setSupportsMultipleClipPARs(kSupportsMultipleClipPARs);

    // Setup OpenCL render capability flags
    p_Desc.setSupportsOpenCLRender(true);

    // Setup CUDA render capability flags on non-Apple system
#ifndef __APPLE__
    p_Desc.setSupportsCudaRender(true);
    p_Desc.setSupportsCudaStream(true);
#endif

    // Setup Metal render capability flags only on Apple system
#ifdef __APPLE__
    p_Desc.setSupportsMetalRender(true);
#endif
}

static OFX::DoubleParamDescriptor* DefineBlendParam(OFX::ImageEffectDescriptor& p_Desc, const std::string& p_Name, const std::string& p_Label,
                                                    const std::string& p_Hint, OFX::GroupParamDescriptor* p_Parent = NULL)
{
    OFX::DoubleParamDescriptor* param = p_Desc.defineDoubleParam(p_Name);
    param->setLabels(p_Label, p_Label, p_Label);
    param->setScriptName(p_Name);
    param->setHint(p_Hint);
    param->setDefault(0.5);
    param->setRange(0.0, 1.0);
    param->setIncrement(0.1);
    param->setDisplayRange(0.0, 1.0);
    param->setDoubleType(OFX::eDoubleTypeScale);

    if (p_Parent)
    {
        param->setParent(*p_Parent);
    }

    return param;
}

static OFX::IntParamDescriptor* DefineIntevalParam(OFX::ImageEffectDescriptor& p_Desc, const std::string& p_Name, const std::string& p_Label,
                                                   const std::string& p_Hint, OFX::GroupParamDescriptor* p_Parent = NULL)
{
    OFX::IntParamDescriptor* param = p_Desc.defineIntParam(p_Name);
    param->setLabels(p_Label, p_Label, p_Label);
    param->setScriptName(p_Name);
    param->setHint(p_Hint);
    param->setDefault(2);
    param->setRange(0, 5);
    param->setDisplayRange(0, 5);

    if (p_Parent)
    {
        param->setParent(*p_Parent);
    }

    return param;
}

void TemporalBlurPluginFactory::describeInContext(OFX::ImageEffectDescriptor& p_Desc, OFX::ContextEnum /*p_Context*/)
{
    // Create the mandated source clip in filter context
    OFX::ClipDescriptor* srcClip = p_Desc.defineClip(kOfxImageEffectSimpleSourceClipName);
    srcClip->addSupportedComponent(OFX::ePixelComponentRGBA);
    srcClip->setTemporalClipAccess(true);
    srcClip->setSupportsTiles(kSupportsTiles);
    srcClip->setIsMask(false);

    // Create the mandated output clip
    OFX::ClipDescriptor* dstClip = p_Desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(OFX::ePixelComponentRGBA);
    dstClip->addSupportedComponent(OFX::ePixelComponentAlpha);
    dstClip->setSupportsTiles(kSupportsTiles);

    // Create page to add UI controllers
    OFX::PageParamDescriptor* page = p_Desc.definePageParam("Controls");

    // Define slider params for blend and interval
    page->addChild(*DefineBlendParam(p_Desc, "blend", "Blend", "Blend level between frames"));
    page->addChild(*DefineIntevalParam(p_Desc, "interval", "Interval", "Interval between frames"));
}

OFX::ImageEffect* TemporalBlurPluginFactory::createInstance(OfxImageEffectHandle p_Handle, OFX::ContextEnum /*p_Context*/)
{
    return new TemporalBlurPlugin(p_Handle);
}

void OFX::Plugin::getPluginIDs(PluginFactoryArray& p_FactoryArray)
{
    static TemporalBlurPluginFactory temporalBlurPlugin;
    p_FactoryArray.push_back(&temporalBlurPlugin);
}

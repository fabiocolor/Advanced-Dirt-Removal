#include "DissolveTransitionPlugin.h"

#include <stdio.h>
#include <string>
#include <algorithm>

#include "ofxsImageEffect.h"
#include "ofxsMultiThread.h"
#include "ofxsProcessing.h"
#include "ofxsLog.h"

#define kPluginName "DissolveTransition"
#define kPluginGrouping "OpenFX Sample"
#define kPluginDescription "DissolveTransition Sample plugin"
#define kPluginIdentifier "com.OpenFXSample.DissolveTransition"
#define kPluginVersionMajor 1
#define kPluginVersionMinor 0

#define kSupportsTiles false
#define kSupportsMultiResolution false
#define kSupportsMultipleClipPARs false

////////////////////////////////////////////////////////////////////////////////

class DissolveTransitionProcessor : public OFX::ImageProcessor
{
public:
    explicit DissolveTransitionProcessor(OFX::ImageEffect& p_Instance);

    virtual void processImagesCUDA();
    virtual void processImagesOpenCL();
    virtual void processImagesMetal();
    virtual void multiThreadProcessImages(OfxRectI p_ProcWindow);

    void setSrcImgs(OFX::Image* p_SrcFromImg, OFX::Image* p_SrcToImg);
    void setParams(float p_Transition);

private:
    OFX::Image* _srcFromImg;
    OFX::Image* _srcToImg;
    float _transition;
};

DissolveTransitionProcessor::DissolveTransitionProcessor(OFX::ImageEffect& p_Instance)
    : OFX::ImageProcessor(p_Instance)
{
}

/** In the following dissolve transition implementaion, we handle clips with alpha channel.
  * If the clip doesn't have valid alpha channel its alpha value is default to 1.0.
  * The opacity values are premultiplied with clip's alpha values before passing to the plugin.
**/

#define PROCESS_IMAGE_IMPL()                                            \
const OfxRectI& bounds = _srcFromImg->getBounds();                      \
const int width        = bounds.x2 - bounds.x1;                         \
const int height       = bounds.y2 - bounds.y1;                         \
                                                                        \
float* srcFromInput = static_cast<float*>(_srcFromImg->getPixelData()); \
float* srcToInput   = static_cast<float*>(_srcToImg->getPixelData());   \
float* output       = static_cast<float*>(_dstImg->getPixelData());

#ifndef __APPLE__
extern void RunCudaKernel(void* p_Stream, int p_Width, int p_Height, float p_Transition, const float* p_SrcFomInput, const float* p_SrcToInput, float* p_Output);
#endif

void DissolveTransitionProcessor::processImagesCUDA()
{
#ifndef __APPLE__
    PROCESS_IMAGE_IMPL()
    RunCudaKernel(_pCudaStream, width, height, _transition, srcToInput, srcFromInput, output);
#endif
}

#ifdef __APPLE__
extern void RunMetalKernel(void* p_CmdQ, int p_Width, int p_Height, float p_Transition, const float* p_SrcFomInput, const float* p_SrcToInput, float* p_Output);
#endif

void DissolveTransitionProcessor::processImagesMetal()
{
#ifdef __APPLE__
    PROCESS_IMAGE_IMPL()
    RunMetalKernel(_pMetalCmdQ, width, height, _transition, srcToInput, srcFromInput, output);
#endif
}

extern void RunOpenCLKernel(void* p_CmdQ, int p_Width, int p_Height, float p_Transition, const float* p_SrcFomInput, const float* p_SrcToInput, float* p_Output);
void DissolveTransitionProcessor::processImagesOpenCL()
{
    PROCESS_IMAGE_IMPL()
    RunOpenCLKernel(_pOpenCLCmdQ, width, height, _transition, srcToInput, srcFromInput, output);
}

void DissolveTransitionProcessor::multiThreadProcessImages(OfxRectI p_ProcWindow)
{
    for (int y = p_ProcWindow.y1; y < p_ProcWindow.y2; ++y)
    {
        if (_effect.abort()) break;

        float* dstPix = static_cast<float*>(_dstImg->getPixelAddress(p_ProcWindow.x1, y));

        for (int x = p_ProcWindow.x1; x < p_ProcWindow.x2; ++x)
        {
            float* srcFromPix = static_cast<float*>(_srcFromImg ? _srcFromImg->getPixelAddress(x, y) : 0);
            float* srcToPix   = static_cast<float*>(_srcToImg ? _srcToImg->getPixelAddress(x, y) : 0);

            // do we have a source image
            if (srcFromPix)
            {
                const float alphaWeightFrom = srcFromPix[3] * (1.0f - _transition);
                const float alphaWeightTo   = srcToPix[3] * _transition;

                dstPix[3] = alphaWeightFrom + alphaWeightTo; // dissolve alpha channel

                for (int c = 0; c < 2; ++c)
                {
                    dstPix[c] = alphaWeightFrom * srcFromPix[c] + alphaWeightTo * srcToPix[c]; // dissolve RGB channels
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

void DissolveTransitionProcessor::setSrcImgs(OFX::Image* p_SrcFromImg, OFX::Image* p_SrcToImg)
{
    _srcFromImg = p_SrcToImg;
    _srcToImg   = p_SrcFromImg;
}

void DissolveTransitionProcessor::setParams(float p_Transition)
{
    _transition = p_Transition;
}

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class DissolveTransitionPlugin : public OFX::ImageEffect
{
public:
    explicit DissolveTransitionPlugin(OfxImageEffectHandle p_Handle);

    // Override the render
    virtual void render(const OFX::RenderArguments& p_Args);

    // Set up and run a processor
    void setupAndProcess(DissolveTransitionProcessor &p_DissolveTransition, const OFX::RenderArguments& p_Args);

private:
    // Does not own the following pointers
    OFX::Clip* m_DstClip;
    OFX::Clip* m_SrcFromClip;
    OFX::Clip* m_SrcToClip;

    OFX::DoubleParam* m_TransitionParam;
    OFX::DoubleParam* m_DissolveSpeedParam;
};

DissolveTransitionPlugin::DissolveTransitionPlugin(OfxImageEffectHandle p_Handle)
    : ImageEffect(p_Handle)
{
    m_DstClip = fetchClip(kOfxImageEffectOutputClipName);

    // OFX plugin with OFX::eContextTransition must have two compulsory input clips
    m_SrcFromClip = fetchClip(kOfxImageEffectTransitionSourceFromClipName);
    m_SrcToClip   = fetchClip(kOfxImageEffectTransitionSourceToClipName);

    // OFX plugin with OFX::eContextTransition must have kOfxImageEffectTransitionParamName ("Transition") parameter
    // It'll fetch the transition progress (which is affected by 4 UI elements: Start Ratio, End Ratio, Reverse and Ease Mode)
    m_TransitionParam = fetchDoubleParam(kOfxImageEffectTransitionParamName);

    // User custom define param
    m_DissolveSpeedParam = fetchDoubleParam("dissolveSpeed");
}

void DissolveTransitionPlugin::render(const OFX::RenderArguments& p_Args)
{
    if ((m_DstClip->getPixelDepth() == OFX::eBitDepthFloat) && (m_DstClip->getPixelComponents() == OFX::ePixelComponentRGBA))
    {
        DissolveTransitionProcessor dissolveTransition(*this);
        setupAndProcess(dissolveTransition, p_Args);
    }
    else
    {
        OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}

void DissolveTransitionPlugin::setupAndProcess(DissolveTransitionProcessor& p_DissolveTransition, const OFX::RenderArguments& p_Args)
{
    const int currFrameNum = p_Args.time;

    // Get the dst image
    std::unique_ptr<OFX::Image> dst(m_DstClip->fetchImage(currFrameNum));

    const OFX::BitDepthEnum dstBitDepth         = dst->getPixelDepth();
    const OFX::PixelComponentEnum dstComponents = dst->getPixelComponents();

    // Get the src images
    std::unique_ptr<OFX::Image> srcFrom(m_SrcFromClip->fetchImage(currFrameNum));
    std::unique_ptr<OFX::Image> srcTo(m_SrcToClip->fetchImage(currFrameNum));

    const OFX::BitDepthEnum srcFromBitDepth         = srcFrom->getPixelDepth();
    const OFX::PixelComponentEnum srcFromComponents = srcFrom->getPixelComponents();

    const OFX::BitDepthEnum srcToBitDepth         = srcTo->getPixelDepth();
    const OFX::PixelComponentEnum srcToComponents = srcTo->getPixelComponents();

    // Check to see if the bit depth and number of components are the same
    if ((srcFromBitDepth != dstBitDepth) || (srcFromComponents != dstComponents) || (srcFromBitDepth != srcToBitDepth) || (srcFromComponents != srcToComponents))
    {
        OFX::throwSuiteStatusException(kOfxStatErrValue);
    }

    // Set the images
    p_DissolveTransition.setDstImg(dst.get());

    p_DissolveTransition.setSrcImgs(srcFrom.get(), srcTo.get());

    // Setup OpenCL, CUDA and Metal Render arguments
    p_DissolveTransition.setGPURenderArgs(p_Args);

    // Set the render window
    p_DissolveTransition.setRenderWindow(p_Args.renderWindow);

    // Set the Transition progress
    const float transitionProgress = std::min(1.0, m_TransitionParam->getValueAtTime(currFrameNum) * m_DissolveSpeedParam->getValueAtTime(currFrameNum));
    p_DissolveTransition.setParams(transitionProgress);

    // Call the base class process member, this will call the derived templated process code
    p_DissolveTransition.process();
}

////////////////////////////////////////////////////////////////////////////////
DissolveTransitionPluginFactory::DissolveTransitionPluginFactory()
    : OFX::PluginFactoryHelper<DissolveTransitionPluginFactory>(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor)
{
}

void DissolveTransitionPluginFactory::describe(OFX::ImageEffectDescriptor& p_Desc)
{
    // Basic labels
    p_Desc.setLabels(kPluginName, kPluginName, kPluginName);
    p_Desc.setPluginGrouping(kPluginGrouping);
    p_Desc.setPluginDescription(kPluginDescription);

    // Add the supported Transition context
    p_Desc.addSupportedContext(OFX::eContextTransition);
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

static OFX::DoubleParamDescriptor* DefineDoubleParam(OFX::ImageEffectDescriptor& p_Desc, const std::string& p_Name, const std::string& p_Label,
                                                     const std::string& p_Hint, float p_Default, float p_MinRange, float p_MaxRange, OFX::GroupParamDescriptor* p_Parent = NULL)
{
    OFX::DoubleParamDescriptor* param = p_Desc.defineDoubleParam(p_Name);
    param->setLabels(p_Label, p_Label, p_Label);
    param->setScriptName(p_Name);
    param->setHint(p_Hint);
    param->setDefault(p_Default);
    param->setRange(p_MinRange, p_MaxRange);
    param->setDisplayRange(p_MinRange, p_MaxRange);
    param->setDoubleType(OFX::eDoubleTypeScale);

    if (p_Parent)
    {
        param->setParent(*p_Parent);
    }

    return param;
}

void DissolveTransitionPluginFactory::describeInContext(OFX::ImageEffectDescriptor& p_Desc, OFX::ContextEnum /*p_Context*/)
{
    // Create the mandated source clips in transition context
    OFX::ClipDescriptor* srcFromClip = p_Desc.defineClip(kOfxImageEffectTransitionSourceFromClipName);
    srcFromClip->addSupportedComponent(OFX::ePixelComponentRGBA);
    srcFromClip->setTemporalClipAccess(false);
    srcFromClip->setSupportsTiles(kSupportsTiles);
    srcFromClip->setIsMask(false);

    OFX::ClipDescriptor* srcToClip = p_Desc.defineClip(kOfxImageEffectTransitionSourceToClipName);
    srcToClip->addSupportedComponent(OFX::ePixelComponentRGBA);
    srcToClip->setTemporalClipAccess(false);
    srcToClip->setSupportsTiles(kSupportsTiles);
    srcToClip->setIsMask(false);

    // Create the mandated output clip
    OFX::ClipDescriptor* dstClip = p_Desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(OFX::ePixelComponentRGBA);
    dstClip->addSupportedComponent(OFX::ePixelComponentAlpha);
    dstClip->setSupportsTiles(kSupportsTiles);

    // Create page to add UI controllers
    OFX::PageParamDescriptor* page = p_Desc.definePageParam("Controls");

    // Define slider params for transition and dissolve speed
    // Required kOfxImageEffectTransitionParamName parameter for plugins supporting OFX::eContextTransition
    page->addChild(*DefineDoubleParam(p_Desc, kOfxImageEffectTransitionParamName, "Transition", "Transition progress", 0.0f, 0.0f, 1.0f));
    page->addChild(*DefineDoubleParam(p_Desc, "dissolveSpeed", "Dissolve Speed", "Dissolve Speed", 1.0f, 1.0f, 5.0f));
}

OFX::ImageEffect* DissolveTransitionPluginFactory::createInstance(OfxImageEffectHandle p_Handle, OFX::ContextEnum /*p_Context*/)
{
    return new DissolveTransitionPlugin(p_Handle);
}

void OFX::Plugin::getPluginIDs(PluginFactoryArray& p_FactoryArray)
{
    static DissolveTransitionPluginFactory dissolveTransitionPlugin;
    p_FactoryArray.push_back(&dissolveTransitionPlugin);
}

#ifndef _ofxImageEffectExt_h_
#define _ofxImageEffectExt_h_

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Says whether the clip is for thumbnail.
   - Type - int X 1
   - Property Set - clip instance (read only)
   - Valid Values - This must be one of 0 or 1
 */
#define kOfxImageClipPropThumbnail "kOfxImageClipPropThumbnail"

/** @brief Indicates which Resolve Page we are currently at
   - Type - string X 1
   - Property Set - inArgs property set of the kOfxActionCreateInstance action
   - Default - "Color"
   - Valid Values - This must be "Edit", "Color" or "Fusion"
 */
#define kOfxImageEffectPropResolvePage "OfxImageEffectPropResolvePage"

/** @brief Indicates a plugin output does not depend on location or neighbours of a given pixel.
If the plugin is with no spatial awareness, it will be executed during LUT generation. Otherwise,
it will be bypassed during LUT generation.

    - Type - string X 1
    - Property Set - plugin descriptor (read/write)
    - Default - "false"
    - Valid Values - This must be one of
      - "false"  - the plugin is with spatial awareness, it will be bypassed during LUT generation
      - "true"   - the plugin is with no spatial awareness, it will be executed during LUT generation
 */
#define kOfxImageEffectPropNoSpatialAwareness "OfxImageEffectPropNoSpatialAwareness"

/** @brief Indicates the source file path. This source file image will be used as input to the created plugin.
   - Type - string X 1
   - Property Set - image effect instance (read only)
   - Default - ""
 */
#define kOfxImageEffectPropSrcFilePath "OfxImageEffectPropSrcFilePath"

/** @brief Indicates the source frame that is being rendered
   - Type - int X 1
   - Property Set - inArgs property set of the kOfxImageEffectActionRender action
 */
#define kOfxImageEffectPropSrcFrame "OfxImageEffectPropSrcFrame"

#ifdef __cplusplus
}
#endif

#endif

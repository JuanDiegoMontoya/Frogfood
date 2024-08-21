#include "Color.h.glsl"
#include "Resources.h.glsl"

FVOG_DECLARE_ARGUMENTS(CalibrateHdrArguments)
{
  FVOG_UINT32 outputImageIndex;
  FVOG_FLOAT displayTargetNits;
};

#ifndef __cplusplus
FVOG_DECLARE_STORAGE_IMAGES(image2D);

void main()
{
  const ivec2 gid = ivec2(gl_GlobalInvocationID.xy);

  if (any(greaterThanEqual(gid, imageSize(Fvog_image2D(outputImageIndex)))))
  {
    return;
  }

  // HDR: Emit 10000 nits for 50% checkerboard. The other 50% should be displayTargetNits.
  // If they approximately match to a viewer, the max display brightness has been found.
  // Note 1: while the peak brightness of most monitors is <<10000 nits, some smoothly approach that peak
  // as the input reaches 10000, so any calibration will be somewhat wrong on those monitors.
  // Note 2: the test image should take up a small percentage of the screen (2-10%) as the peak brightness
  // generally cannot be sustained on the whole display. Therefore, scene content should aim to have peak 
  // brightness cover only a small portion of the screen as well.

  vec3 color = vec3(1); // 10k nits
  if ((gid.x + gid.y) % 2 == 0)
  {
    color = color_PQ_OETF(vec3(displayTargetNits / 10000.0));
  }

  imageStore(Fvog_image2D(outputImageIndex), gid, vec4(color, 1.0));
}
#endif
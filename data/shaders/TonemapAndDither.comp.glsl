#version 450 core

#extension GL_GOOGLE_include_directive : enable

#include "Resources.h.glsl"

layout(local_size_x = 8, local_size_y = 8) in;

//layout(binding = 0) uniform sampler2D s_sceneColor;
//layout(binding = 1) uniform sampler2D s_noise;
FVOG_DECLARE_SAMPLED_IMAGES(texture2D);
FVOG_DECLARE_SAMPLED_IMAGES(texture3D);
FVOG_DECLARE_SAMPLERS;

FVOG_DECLARE_ARGUMENTS(TonemapArguments)
{
  FVOG_UINT32 sceneColorIndex;
  FVOG_UINT32 noiseIndex;
  FVOG_UINT32 nearestSamplerIndex;
  FVOG_UINT32 linearClampSamplerIndex;

  FVOG_UINT32 exposureIndex;
  FVOG_UINT32 tonemapUniformsIndex;
  FVOG_UINT32 outputImageIndex;

  FVOG_UINT32 tonyMcMapfaceIndex;
  FVOG_UINT32 tonemapper; // 0 = AgX, 1 = Tony
};

//layout(std140, binding = 0) uniform ExposureBuffer
FVOG_DECLARE_STORAGE_BUFFERS(restrict readonly ExposureBuffer)
{
  float exposure;
} exposureBuffers[];

#define d_exposureBuffer exposureBuffers[exposureIndex]

//layout(std140, binding = 1) uniform TonemapUniformBuffer
FVOG_DECLARE_STORAGE_BUFFERS(restrict readonly TonemapUniformBuffer)
{
  float saturation;
  float agxDsLinearSection;
  float peak;
  float compression;
  uint enableDithering;
} uniformsBuffers[];

#define d_uniforms uniformsBuffers[tonemapUniformsIndex]

//layout(binding = 0) uniform writeonly image2D i_output;
FVOG_DECLARE_STORAGE_IMAGES(image2D);

// AgX implementation from here: https://www.shadertoy.com/view/Dt3XDr
vec3 xyYToXYZ(vec3 xyY)
{
  float Y = xyY.z;
  float X = (xyY.x * Y) / xyY.y;
  float Z = ((1.0f - xyY.x - xyY.y) * Y) / xyY.y;

  return vec3(X, Y, Z);
}

vec3 Unproject(vec2 xy)
{
  return xyYToXYZ(vec3(xy.x, xy.y, 1));				
}

mat3 PrimariesToMatrix(vec2 xy_red, vec2 xy_green, vec2 xy_blue, vec2 xy_white)
{
  vec3 XYZ_red = Unproject(xy_red);
  vec3 XYZ_green = Unproject(xy_green);
  vec3 XYZ_blue = Unproject(xy_blue);
  vec3 XYZ_white = Unproject(xy_white);

  mat3 temp = mat3(XYZ_red.x,	  1.0, XYZ_red.z,
                    XYZ_green.x, 1.f, XYZ_green.z,
                    XYZ_blue.x,  1.0, XYZ_blue.z);
  vec3 scale = inverse(temp) * XYZ_white;

  return mat3(XYZ_red * scale.x, XYZ_green * scale.y, XYZ_blue * scale.z);
}

mat3 ComputeCompressionMatrix(vec2 xyR, vec2 xyG, vec2 xyB, vec2 xyW, float compression)
{
  float scale_factor = 1.0 / (1.0 - compression);
  vec2 R = mix(xyW, xyR, scale_factor);
  vec2 G = mix(xyW, xyG, scale_factor);
  vec2 B = mix(xyW, xyB, scale_factor);
  vec2 W = xyW;

  return PrimariesToMatrix(R, G, B, W);
}

float DualSection(float x, float linear, float peak)
{
	// Length of linear section
	float S = (peak * linear);
	if (x < S) {
		return x;
	} else {
		float C = peak / (peak - S);
		return peak - (peak - S) * exp((-C * (x - S)) / peak);
	}
}

vec3 DualSection(vec3 x, float linear, float peak)
{
	x.x = DualSection(x.x, linear, peak);
	x.y = DualSection(x.y, linear, peak);
	x.z = DualSection(x.z, linear, peak);
	return x;
}

vec3 AgX_DS(vec3 color_srgb, float exposure, float saturation, float linear, float peak, float compression)
{
  vec3 workingColor = max(color_srgb, 0.0f) * pow(2.0, exposure);

  mat3 sRGB_to_XYZ = PrimariesToMatrix(vec2(0.64, 0.33),
                                       vec2(0.3, 0.6), 
                                       vec2(0.15, 0.06), 
                                       vec2(0.3127, 0.3290));
  mat3 adjusted_to_XYZ = ComputeCompressionMatrix(vec2(0.64,0.33),
                                                  vec2(0.3,0.6), 
                                                  vec2(0.15,0.06), 
                                                  vec2(0.3127, 0.3290), compression);
  mat3 XYZ_to_adjusted = inverse(adjusted_to_XYZ);
  mat3 sRGB_to_adjusted = sRGB_to_XYZ * XYZ_to_adjusted;

  workingColor = sRGB_to_adjusted * workingColor;
  workingColor = clamp(DualSection(workingColor, linear, peak), 0.0, 1.0);
  
  vec3 luminanceWeight = vec3(0.2126729,  0.7151522,  0.0721750);
  vec3 desaturation = vec3(dot(workingColor, luminanceWeight));
  workingColor = mix(desaturation, workingColor, saturation);
  workingColor = clamp(workingColor, 0.0, 1.0);

  workingColor = inverse(sRGB_to_adjusted) * workingColor;

  return workingColor;
}

// Tony McMapface from https://github.com/h3r2tic/tony-mc-mapface/blob/main/shader/tony_mc_mapface.hlsl
vec3 TonyMcMapface(vec3 color_srgb, float exposure)
{
  vec3 workingColor = max(color_srgb, 0.0f) * pow(2.0, exposure);
  vec3 encoded = workingColor / (1.0 + workingColor);

  vec3 dims = vec3(textureSize(Fvog_texture3D(tonyMcMapfaceIndex), 0));
  vec3 uv = encoded * ((dims - 1.0) / dims) + 0.5 / dims;

  return texture(Fvog_sampler3D(tonyMcMapfaceIndex, linearClampSamplerIndex), uv).rgb;
}

// sRGB OETF
vec3 linear_to_nonlinear_srgb(vec3 linearColor)
{
  bvec3 cutoff = lessThan(linearColor, vec3(0.0031308));
  vec3 higher = vec3(1.055) * pow(linearColor, vec3(1.0 / 2.4)) - vec3(0.055);
  vec3 lower = linearColor * vec3(12.92);

  return mix(higher, lower, cutoff);
}

vec3 apply_dither(vec3 color, vec2 uv)
{
  vec2 uvNoise = uv * (vec2(textureSize(Fvog_sampler2D(sceneColorIndex, nearestSamplerIndex), 0)) / vec2(textureSize(Fvog_sampler2D(noiseIndex, nearestSamplerIndex), 0)));
  vec3 noiseSample = textureLod(Fvog_sampler2D(noiseIndex, nearestSamplerIndex), uvNoise, 0).rgb;
  return color + vec3((noiseSample - 0.5) / 255.0);
}

void main()
{
  const ivec2 gid = ivec2(gl_GlobalInvocationID.xy);
  const ivec2 targetDim = imageSize(Fvog_image2D(outputImageIndex));

  if (any(greaterThanEqual(gid, targetDim)))
  {
    return;
  }

  const vec2 uv = (vec2(gid) + 0.5) / targetDim;
  
  vec3 hdrColor = textureLod(Fvog_sampler2D(sceneColorIndex, nearestSamplerIndex), uv, 0).rgb;
  vec3 ldrColor = hdrColor;
  
  if (tonemapper == 0)
  {
    ldrColor = AgX_DS(hdrColor, d_exposureBuffer.exposure, d_uniforms.saturation, d_uniforms.agxDsLinearSection, d_uniforms.peak, d_uniforms.compression);
  }
  if (tonemapper == 1)
  {
    ldrColor = TonyMcMapface(hdrColor, d_exposureBuffer.exposure);
  }

  vec3 srgbColor = linear_to_nonlinear_srgb(ldrColor);

  vec3 ditheredColor = srgbColor;
  
  if (bool(d_uniforms.enableDithering))
  {
    ditheredColor = apply_dither(srgbColor, uv);
  }

  imageStore(Fvog_image2D(outputImageIndex), gid, vec4(ditheredColor, 1.0));
}
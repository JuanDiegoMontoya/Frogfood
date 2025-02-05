#include "../Resources.h.glsl"
#include "../GlobalUniforms.h.glsl"
#include "../Math.h.glsl"
#include "../Utility.h.glsl"
#include "../Hash.h.glsl"
#include "../Config.shared.h"

#include "Voxels.h.glsl"

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 o_albedo;
layout(location = 1) out vec4 o_normal;
layout(location = 2) out vec4 o_indirectIlluminance;
layout(location = 3) out vec4 o_radiance;

FVOG_DECLARE_ARGUMENTS(PushConstants)
{
	Voxels voxels;
	FVOG_UINT32 uniformBufferIndex;
	Texture2D noiseTexture;
}pc;

#define uniforms perFrameUniformsBuffers[pc.uniformBufferIndex]

void main()
{
	vx_Init(pc.voxels);
	const vec3 rayDir = normalize(UnprojectUV_ZO(0.99, uv, uniforms.invViewProj) - uniforms.cameraPos.xyz);
	const vec3 rayPos = uniforms.cameraPos.xyz;
	
	vec3 albedo = {0, 0, 0};
	vec3 normal = {0, 0, 0};
	vec3 indirectIlluminance = {0, 0, 0};
	vec3 radiance = {0, 0, 0};

	HitSurfaceParameters hit;
	//if (vx_TraceRaySimple(rayPos, rayDir, 5120, hit))
	if (vx_TraceRayMultiLevel(rayPos, rayDir, 512, hit))
	{
		albedo = GetHitAlbedo(hit);
		normal = hit.flatNormalWorld;

		// Shadow
		const vec3 sunDir = normalize(vec3(.7, 1, .3));
		const float NoL = max(0, dot(hit.flatNormalWorld, sunDir));
		radiance += albedo * NoL * TraceSunRay(hit.positionWorld + hit.flatNormalWorld * 1e-4, sunDir);
		radiance += GetHitEmission(hit);

		const ivec2 gid = ivec2(gl_FragCoord.xy);

		if (g_voxels.numLights > 0)
		{
			uint randState = PCG_Hash(gid.y + PCG_Hash(gid.x));
			// Local light NEE
			const uint lightIndex = PCG_RandU32(randState) % g_voxels.numLights;
			const float lightPdf = 1.0 / g_voxels.numLights;
			GpuLight light = lightsBuffers[g_voxels.lightBufferIdx].lights[lightIndex];

			const float visibility = GetPunctualLightVisibility(hit.positionWorld + hit.flatNormalWorld * 0.0001, lightIndex);
			if (visibility > 0)
			{
				Surface surface;
				surface.albedo = GetHitAlbedo(hit);
				surface.normal = hit.flatNormalWorld;
				surface.position = hit.positionWorld;
				indirectIlluminance += visibility * EvaluatePunctualLightLambert(light, surface, COLOR_SPACE_sRGB_LINEAR) / lightPdf;
			}
		}

		const uint samples = 1;
		const uint bounces = 2;
		indirectIlluminance += TraceIndirectLighting(gid, hit.positionWorld + hit.flatNormalWorld * 1e-4, hit.flatNormalWorld, samples, bounces, pc.noiseTexture);

		const vec4 posClip = uniforms.viewProj * vec4(hit.positionWorld, 1.0);
		gl_FragDepth = posClip.z / posClip.w;
	}
	else
	{
		albedo = rayDir * .5 + .5;
		radiance = albedo;
		gl_FragDepth = FAR_DEPTH;
	}

	//o_color += vec3(gTopLevelBricksTraversed / 8, gBottomLevelBricksTraversed / 64, gVoxelsTraversed / 512);
	//o_color = vec3(gTopLevelBricksTraversed / 8);
	//o_color = vec3(gBottomLevelBricksTraversed / 64);
	//o_color = vec3(gVoxelsTraversed / 512);

	//o_color = o_color / (1 + o_color); // Reinhard
	//imageStore(pc.outputImage, gid, vec4(pow(o_color, vec3(1/2.2)), 1));
	//o_color = vec4(pow(color, vec3(1 / 2.2)), 1);
	o_albedo = vec4(albedo, 1);
	o_normal = vec4(normal, 1);
	o_indirectIlluminance = vec4(indirectIlluminance, 1);
	o_radiance = vec4(radiance, 1);
}
#include "../Resources.h.glsl"
#include "../GlobalUniforms.h.glsl"
#include "../Math.h.glsl"
#include "../Utility.h.glsl"
#include "../Hash.h.glsl"

#include "Voxels.h.glsl"

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 o_albedo;
layout(location = 1) out vec4 o_normal;
layout(location = 2) out vec4 o_illuminance;

FVOG_DECLARE_ARGUMENTS(PushConstants)
{
	Voxels voxels;
	FVOG_UINT32 uniformBufferIndex;
}pc;

#define uniforms perFrameUniformsBuffers[pc.uniformBufferIndex]

void main()
{
	vx_Init(pc.voxels);
	const vec3 rayDir = normalize(UnprojectUV_ZO(0.99, uv, uniforms.invViewProj) - uniforms.cameraPos.xyz);
	const vec3 rayPos = uniforms.cameraPos.xyz;
	
	vec3 albedo = {0, 0, 0};
	vec3 normal = {0, 0, 0};
	vec3 illuminance = {0, 0, 0};

	HitSurfaceParameters hit;
	//if (vx_TraceRaySimple(rayPos, rayDir, 5120, hit))
	if (vx_TraceRayMultiLevel(rayPos, rayDir, 512, hit))
	{
		albedo = GetHitAlbedo(hit);
		normal = hit.flatNormalWorld;

		// Shadow
		illuminance += TraceSunRay(hit.positionWorld + hit.flatNormalWorld * 1e-4);

		const uint samples = 1;
		const uint bounces = 2;
		illuminance += TraceIndirectLighting(ivec2(gl_FragCoord), hit.positionWorld + hit.flatNormalWorld * 1e-4, hit.flatNormalWorld, samples, bounces);

		const vec4 posClip = uniforms.viewProj * vec4(hit.positionWorld, 1.0);
		gl_FragDepth = posClip.z / posClip.w;
	}
	else
	{
		albedo = rayDir * .5 + .5;
		illuminance = vec3(1);
		gl_FragDepth = 0;
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
	o_illuminance = vec4(illuminance, 1);
}
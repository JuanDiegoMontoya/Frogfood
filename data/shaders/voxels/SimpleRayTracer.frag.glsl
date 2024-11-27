#include "../Resources.h.glsl"
#include "../GlobalUniforms.h.glsl"
#include "../Math.h.glsl"
#include "../Utility.h.glsl"
#include "../Hash.h.glsl"

#include "Voxels.h.glsl"

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 o_color;

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
	
	vec3 color = {0, 0, 0};

	HitSurfaceParameters hit;
	//if (vx_TraceRaySimple(rayPos, rayDir, 5120, hit))
	if (vx_TraceRayMultiLevel(rayPos, rayDir, 512, hit))
	{
		const vec3 albedo = GetHitAlbedo(hit);
		color += albedo;

		// Shadow
		color *= TraceSunRay(hit.positionWorld);

		color += albedo * TraceIndirectLighting(ivec2(gl_FragCoord), hit.positionWorld + hit.flatNormalWorld * 1e-4, hit.flatNormalWorld);

		const vec4 posClip = uniforms.viewProj * vec4(hit.positionWorld, 1.0);
		gl_FragDepth = posClip.z / posClip.w;
	}
	else
	{
		color += rayDir * .5 + .5;
		gl_FragDepth = 0;
	}

	//o_color += vec3(gTopLevelBricksTraversed / 8, gBottomLevelBricksTraversed / 64, gVoxelsTraversed / 512);
	//o_color = vec3(gTopLevelBricksTraversed / 8);
	//o_color = vec3(gBottomLevelBricksTraversed / 64);
	//o_color = vec3(gVoxelsTraversed / 512);

	//o_color = o_color / (1 + o_color); // Reinhard
	//imageStore(pc.outputImage, gid, vec4(pow(o_color, vec3(1/2.2)), 1));
	o_color = vec4(pow(color, vec3(1 / 2.2)), 1);
}
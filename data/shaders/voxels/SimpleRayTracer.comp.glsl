#include "../Resources.h.glsl"
#include "../GlobalUniforms.h.glsl"
#include "../Math.h.glsl"
#include "../Utility.h.glsl"
#include "../Hash.h.glsl"

const int BL_BRICK_SIDE_LENGTH = 8;
const int CELLS_PER_BL_BRICK   = BL_BRICK_SIDE_LENGTH * BL_BRICK_SIDE_LENGTH * BL_BRICK_SIDE_LENGTH;

const int TL_BRICK_SIDE_LENGTH = 8;
const int CELLS_PER_TL_BRICK   = TL_BRICK_SIDE_LENGTH * TL_BRICK_SIDE_LENGTH * TL_BRICK_SIDE_LENGTH;

const int VOXELS_PER_TL_BRICK      = CELLS_PER_TL_BRICK * CELLS_PER_BL_BRICK * CELLS_PER_TL_BRICK;
const int TL_BRICK_VOXELS_PER_SIDE = TL_BRICK_SIDE_LENGTH * BL_BRICK_SIDE_LENGTH;

#define voxel_t uint8_t

struct BottomLevelBrick
{
	voxel_t voxels[512];
};

struct BottomLevelBrickPtr
{
	uint8_t voxelsDoBeAllSame;
	uint bottomLevelBrickIndexOrVoxelIfAllSame;
};

struct TopLevelBrick
{
	BottomLevelBrickPtr bricks[512];
};

struct TopLevelBrickPtr
{
	uint8_t voxelsDoBeAllSame;
	uint topLevelBrickIndexOrVoxelIfAllSame;
};

FVOG_DECLARE_STORAGE_BUFFERS(TopLevelBrickPtrs)
{
	TopLevelBrickPtr topLevelPtrs[];
}topLevelPtrsBuffers[];

FVOG_DECLARE_STORAGE_BUFFERS(TopLevelBricks)
{
	TopLevelBrick topLevelBricks[];
}topLevelBricksBuffers[];

FVOG_DECLARE_STORAGE_BUFFERS(BottomLevelBricks)
{
	BottomLevelBrick bottomLevelBricks[];
}bottomLevelBricksBuffers[];

FVOG_DECLARE_ARGUMENTS(PushConstants)
{
	FVOG_IVEC3 topLevelBricksDims;
	FVOG_UINT32 topLevelBrickPtrsBaseIndex;
	FVOG_IVEC3 dimensions;
	FVOG_UINT32 bufferIdx;
	FVOG_UINT32 uniformBufferIndex;
	Image2D outputImage;
}pc;

#define uniforms perFrameUniformsBuffers[pc.uniformBufferIndex]

#define TOP_LEVEL_PTRS topLevelPtrsBuffers[pc.bufferIdx].topLevelPtrs
#define TOP_LEVEL_BRICKS topLevelBricksBuffers[pc.bufferIdx].topLevelBricks
#define BOTTOM_LEVEL_BRICKS bottomLevelBricksBuffers[pc.bufferIdx].bottomLevelBricks

struct GridHierarchyCoords
{
	ivec3 topLevel;
	ivec3 bottomLevel;
	ivec3 localVoxel;
};

GridHierarchyCoords GetCoordsOfVoxelAt(ivec3 voxelCoord)
{
  const ivec3 topLevelCoord    = voxelCoord / TL_BRICK_VOXELS_PER_SIDE;
  const ivec3 bottomLevelCoord = (voxelCoord / BL_BRICK_SIDE_LENGTH) % TL_BRICK_SIDE_LENGTH;
  const ivec3 localVoxelCoord  = voxelCoord % BL_BRICK_SIDE_LENGTH;

  //assert(glm::all(glm::lessThan(topLevelCoord, topLevelBricksDims_)));
  //assert(glm::all(glm::lessThan(bottomLevelCoord, glm::ivec3(TL_BRICK_SIDE_LENGTH))));
  //assert(glm::all(glm::lessThan(localVoxelCoord, glm::ivec3(BL_BRICK_SIDE_LENGTH))));

  return GridHierarchyCoords(topLevelCoord, bottomLevelCoord, localVoxelCoord);
}

int FlattenTopLevelBrickCoord(ivec3 coord)
{
  return (coord.z * pc.topLevelBricksDims.x * pc.topLevelBricksDims.y) + (coord.y * pc.topLevelBricksDims.x) + coord.x;
}

int FlattenBottomLevelBrickCoord(ivec3 coord)
{
  return (coord.z * TL_BRICK_SIDE_LENGTH * TL_BRICK_SIDE_LENGTH) + (coord.y * TL_BRICK_SIDE_LENGTH) + coord.x;
}

int FlattenVoxelCoord(ivec3 coord)
{
  return (coord.z * BL_BRICK_SIDE_LENGTH * BL_BRICK_SIDE_LENGTH) + (coord.y * BL_BRICK_SIDE_LENGTH) + coord.x;
}

voxel_t GetVoxelAt(ivec3 voxelCoord)
{
  //assert(glm::all(glm::greaterThanEqual(voxelCoord, glm::ivec3(0))));
  //assert(glm::all(glm::lessThan(voxelCoord, dimensions_)));

  GridHierarchyCoords coords = GetCoordsOfVoxelAt(voxelCoord);
  //auto [topLevelCoord, bottomLevelCoord, localVoxelCoord] = GetCoordsOfVoxelAt(voxelCoord);

  const uint topLevelIndex = FlattenTopLevelBrickCoord(coords.topLevel);
  //assert(topLevelIndex < numTopLevelBricks_);
  //const auto& topLevelBrickPtr = buffer.GetBase<TopLevelBrickPtr>()[topLevelBrickPtrsBaseIndex + topLevelIndex];
  TopLevelBrickPtr topLevelBrickPtr = TOP_LEVEL_PTRS[pc.topLevelBrickPtrsBaseIndex + topLevelIndex];

  if (topLevelBrickPtr.voxelsDoBeAllSame != 0)
  {
    return voxel_t(topLevelBrickPtr.topLevelBrickIndexOrVoxelIfAllSame);
  }

  const uint bottomLevelIndex = FlattenBottomLevelBrickCoord(coords.bottomLevel);
  //assert(bottomLevelIndex < CELLS_PER_TL_BRICK);
  //auto& bottomLevelBrickPtr = buffer.GetBase<TopLevelBrick>()[topLevelBrickPtr.topLevelBrick].bricks[bottomLevelIndex];
  BottomLevelBrickPtr bottomLevelBrickPtr = TOP_LEVEL_BRICKS[topLevelBrickPtr.topLevelBrickIndexOrVoxelIfAllSame].bricks[bottomLevelIndex];

  if (bottomLevelBrickPtr.voxelsDoBeAllSame != 0)
  {
    return voxel_t(bottomLevelBrickPtr.bottomLevelBrickIndexOrVoxelIfAllSame);
  }

  const uint localVoxelIndex = FlattenVoxelCoord(coords.localVoxel);
  //assert(localVoxelIndex < CELLS_PER_BL_BRICK);
  //return buffer.GetBase<BottomLevelBrick>()[bottomLevelBrickPtr.bottomLevelBrick].voxels[localVoxelIndex];
  return BOTTOM_LEVEL_BRICKS[bottomLevelBrickPtr.bottomLevelBrickIndexOrVoxelIfAllSame].voxels[localVoxelIndex];
}

struct HitSurfaceParameters
{
  vec3 voxelPosition;
  vec3 positionWorld;
  vec3 flatNormalWorld;
  vec2 texCoord;
};

bool vx_TraceRayMultiLevel(vec3 rayPosition, vec3 rayDirection, float tMax, out HitSurfaceParameters hit)
{
	return false;
}

bool vx_TraceRaySimple(vec3 rayPosition, vec3 rayDirection, float tMax, out HitSurfaceParameters hit)
{
	// https://www.shadertoy.com/view/X3BXDd
	vec3 mapPos = floor(rayPosition); // integer cell coordinate of initial cell
    
    const vec3 deltaDist = 1.0 / abs(rayDirection); // ray length required to step from one cell border to the next in x, y and z directions

    const vec3 S = vec3(step(0.0, rayDirection)); // S is rayDir non-negative? 0 or 1
    const vec3 stepDir = 2 * S - 1; // Step sign
    
    // if 1./abs(rayDir[i]) is inf, then rayDir[i] is 0., but then S = step(0., rayDir[i]) is 1
    // so S cannot be 0. while deltaDist is inf, and stepDir * fract(pos) can never be 1.
    // Therefore we should not have to worry about getting NaN here :)
    
	// initial distance to cell sides, then relative difference between traveled sides
    vec3 sideDist = (S - stepDir * fract(rayPosition)) * deltaDist;   // alternative: //sideDist = (S-stepDir * (pos - map)) * deltaDist;
    
    for(int i = 0; i < tMax; i++)
    {
        // Decide which way to go!
        vec4 conds = step(sideDist.xxyy, sideDist.yzzx); // same as vec4(sideDist.xxyy <= sideDist.yzzx);
        
        // This mimics the if, elseif and else clauses
        // * is 'and', 1.-x is negation
        vec3 cases = vec3(0);
        cases.x = conds.x * conds.y;                    // if       x dir
        cases.y = (1.0 - cases.x) * conds.z * conds.w;  // else if  y dir
        cases.z = (1.0 - cases.x) * (1.0 - cases.y);    // else     z dir
        
        // usually would have been:     sideDist += cases * deltaDist;
        // but this gives NaN when  cases[i] * deltaDist[i]  becomes  0. * inf 
        // This gives NaN result in a component that should not have been affected,
        // so we instead give negative results for inf by mapping 'cases' to +/- 1
        // and then clamp negative values to zero afterwards, giving the correct result! :)
        sideDist += max((2.0 * cases - 1.0) * deltaDist, 0.0);
        
        mapPos += cases * stepDir;
        
		// Putting the exit condition down here implicitly skips the first voxel
        if(all(greaterThanEqual(mapPos, vec3(0))) && all(lessThan(mapPos, ivec3(pc.dimensions))) && GetVoxelAt(ivec3(mapPos)) != 0)
        {
			const vec3 p = mapPos + 0.5 - stepDir * 0.5; // Point on axis plane
			const vec3 normal = vec3(ivec3(vec3(cases))) * -vec3(stepDir);

			// Solve ray plane intersection equation: dot(n, ro + t * rd - p) = 0.
			// for t :
			const float t = (dot(normal, p - rayPosition)) / dot(normal, rayDirection);
			const vec3 hitWorldPos = rayPosition + rayDirection * t;
			const vec3 uvw = hitWorldPos - mapPos; // Don't use fract here
			
			// Ugly, hacky way to get texCoord
			vec2 texCoord = {0, 0};
			if (normal.x > 0) texCoord = vec2(1 - uvw.z, uvw.y);
			if (normal.x < 0) texCoord = vec2(uvw.z, uvw.y);
			if (normal.y > 0) texCoord = vec2(uvw.x, 1 - uvw.z); // Arbitrary
			if (normal.y < 0) texCoord = vec2(uvw.x, uvw.z);
			if (normal.z > 0) texCoord = vec2(uvw.x, uvw.y);
			if (normal.z < 0) texCoord = vec2(1 - uvw.x, uvw.y);

			hit.voxelPosition = ivec3(mapPos);
			hit.positionWorld = hitWorldPos;
			hit.texCoord = texCoord;
			hit.flatNormalWorld = normal;
			return true;
        }
    }

	return false;
}

layout(local_size_x = 8, local_size_y = 8) in;
void main()
{
	const ivec2 gid = ivec2(gl_GlobalInvocationID.xy);
	const ivec2 outputSize = imageSize(pc.outputImage);
	if (any(greaterThanEqual(gid, outputSize)))
	{
		return;
	}

	const vec2 uv = (vec2(gid) + 0.5) / outputSize;
	
	const vec3 rayDir = normalize(UnprojectUV_ZO(0.0001, uv, uniforms.invViewProj) - uniforms.cameraPos.xyz);
	const vec3 rayPos = uniforms.cameraPos.xyz;
	
	HitSurfaceParameters hit;
	if (vx_TraceRaySimple(rayPos, rayDir, 512, hit))
	{
		//imageStore(pc.outputImage, gid, vec4(hsv_to_rgb(vec3(MM_Hash3(hit.voxelPosition), 0.55, 0.8)), 1));
		//imageStore(pc.outputImage, gid, vec4(mod(abs(hit.positionWorld + .5), 1), 1));
		//imageStore(pc.outputImage, gid, vec4(hit.flatNormalWorld * .5 + .5, 1));
		imageStore(pc.outputImage, gid, vec4(hit.texCoord, 0, 1));
	}
	else
	{
		imageStore(pc.outputImage, gid, vec4(rayDir * .5 + .5, 1));
	}
}
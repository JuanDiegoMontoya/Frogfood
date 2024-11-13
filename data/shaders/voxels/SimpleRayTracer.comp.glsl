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

TopLevelBrickPtr GetTopLevelBrickPtrAt(ivec3 voxelCoord)
{
  GridHierarchyCoords coords = GetCoordsOfVoxelAt(voxelCoord);
  const uint topLevelIndex = FlattenTopLevelBrickCoord(coords.topLevel);
  return TOP_LEVEL_PTRS[pc.topLevelBrickPtrsBaseIndex + topLevelIndex];
}

// NOTE: does not check for validity of topLevelBrickIndexOrVoxelIfAllSame
// TODO: remove after more optimal multi-level traversal is implemented.
BottomLevelBrickPtr GetBottomLevelBrickPtrAt(ivec3 voxelCoord)
{
  GridHierarchyCoords coords = GetCoordsOfVoxelAt(voxelCoord);
  const uint topLevelIndex = FlattenTopLevelBrickCoord(coords.topLevel);
  TopLevelBrickPtr topLevelBrickPtr = TOP_LEVEL_PTRS[pc.topLevelBrickPtrsBaseIndex + topLevelIndex];
  const uint bottomLevelIndex = FlattenBottomLevelBrickCoord(coords.bottomLevel);
  return TOP_LEVEL_BRICKS[topLevelBrickPtr.topLevelBrickIndexOrVoxelIfAllSame].bricks[bottomLevelIndex];
}

voxel_t GetVoxelAt(ivec3 voxelCoord)
{
  GridHierarchyCoords coords = GetCoordsOfVoxelAt(voxelCoord);

  const uint topLevelIndex = FlattenTopLevelBrickCoord(coords.topLevel);
  TopLevelBrickPtr topLevelBrickPtr = TOP_LEVEL_PTRS[pc.topLevelBrickPtrsBaseIndex + topLevelIndex];

  if (topLevelBrickPtr.voxelsDoBeAllSame != 0)
  {
    return voxel_t(topLevelBrickPtr.topLevelBrickIndexOrVoxelIfAllSame);
  }

  const uint bottomLevelIndex = FlattenBottomLevelBrickCoord(coords.bottomLevel);
  BottomLevelBrickPtr bottomLevelBrickPtr = TOP_LEVEL_BRICKS[topLevelBrickPtr.topLevelBrickIndexOrVoxelIfAllSame].bricks[bottomLevelIndex];

  if (bottomLevelBrickPtr.voxelsDoBeAllSame != 0)
  {
    return voxel_t(bottomLevelBrickPtr.bottomLevelBrickIndexOrVoxelIfAllSame);
  }

  const uint localVoxelIndex = FlattenVoxelCoord(coords.localVoxel);
  return BOTTOM_LEVEL_BRICKS[bottomLevelBrickPtr.bottomLevelBrickIndexOrVoxelIfAllSame].voxels[localVoxelIndex];
}

struct HitSurfaceParameters
{
	voxel_t voxel;
	vec3 voxelPosition;
	vec3 positionWorld;
	vec3 flatNormalWorld;
	vec2 texCoord;
};

float gTopLevelBricksTraversed = 0;
float gBottomLevelBricksTraversed = 0;
float gVoxelsTraversed = 0;

// Ray position in (0, BL_BRICK_SIDE_LENGTH)
bool vx_TraceRayVoxels(vec3 rayPosition, vec3 rayDirection, BottomLevelBrickPtr bottomLevelBrickPtr)
{
	rayPosition = clamp(rayPosition, vec3(0.0001), vec3(7.9999));
	vec3 mapPos = floor(rayPosition);
    const vec3 deltaDist = 1.0 / abs(rayDirection);
    const vec3 S = vec3(step(0.0, rayDirection));
    const vec3 stepDir = 2 * S - 1;
    vec3 sideDist = (S - stepDir * fract(rayPosition)) * deltaDist;
	vec3 cases = vec3(0);

    while (all(greaterThanEqual(mapPos, vec3(0))) && all(lessThan(mapPos, ivec3(BL_BRICK_SIDE_LENGTH))))
    {
		gVoxelsTraversed++;
		const uint localVoxelIndex = FlattenVoxelCoord(ivec3(mapPos));
		voxel_t voxel = BOTTOM_LEVEL_BRICKS[bottomLevelBrickPtr.bottomLevelBrickIndexOrVoxelIfAllSame].voxels[localVoxelIndex];
		if (voxel != 0)
		{
			return true;
		}

        vec4 conds = step(sideDist.xxyy, sideDist.yzzx); // same as vec4(sideDist.xxyy <= sideDist.yzzx);
        
        cases.x = conds.x * conds.y;                    // if       x dir
        cases.y = (1.0 - cases.x) * conds.z * conds.w;  // else if  y dir
        cases.z = (1.0 - cases.x) * (1.0 - cases.y);    // else     z dir
        
        sideDist += max((2.0 * cases - 1.0) * deltaDist, 0.0);
        
        mapPos += cases * stepDir;
	}

	return false;
}

// Ray position in (0, TL_BRICK_SIDE_LENGTH)
bool vx_TraceRayBottomLevelBricks(vec3 rayPosition, vec3 rayDirection, TopLevelBrickPtr topLevelBrickPtr)
{
	rayPosition = clamp(rayPosition, vec3(0.0001), vec3(7.9999));
	vec3 mapPos = floor(rayPosition);
    const vec3 deltaDist = 1.0 / abs(rayDirection);
    const vec3 S = vec3(step(0.0, rayDirection));
    const vec3 stepDir = 2 * S - 1;
    vec3 sideDist = (S - stepDir * fract(rayPosition)) * deltaDist;
	vec3 cases = vec3(0);

    for (int i = 0; all(greaterThanEqual(mapPos, vec3(0))) && all(lessThan(mapPos, ivec3(TL_BRICK_SIDE_LENGTH))); i++)
    {
		const uint bottomLevelIndex = FlattenBottomLevelBrickCoord(ivec3(mapPos));
		BottomLevelBrickPtr bottomLevelBrickPtr = TOP_LEVEL_BRICKS[topLevelBrickPtr.topLevelBrickIndexOrVoxelIfAllSame].bricks[bottomLevelIndex];
		if (!(bottomLevelBrickPtr.voxelsDoBeAllSame == 1 && bottomLevelBrickPtr.bottomLevelBrickIndexOrVoxelIfAllSame == 0))
		{
			gBottomLevelBricksTraversed++;
			const vec3 p = mapPos + 0.5 - stepDir * 0.5; // Point on axis plane
			const vec3 normal = vec3(ivec3(vec3(cases))) * -vec3(stepDir);
			// Degenerate if ray starts inside a homogeneous bottom-level brick
			const float t = (dot(normal, p - rayPosition)) / dot(normal, rayDirection);
			const vec3 hitWorldPos = rayPosition + rayDirection * t;
			vec3 uvw = hitWorldPos - mapPos;
			
			if (i == 0)
			{
				uvw = (rayPosition - mapPos);
			}

           vec3 mini = ((mapPos-rayPosition) + 0.5 - 0.5*vec3(stepDir))*deltaDist;
           float d = max (mini.x, max (mini.y, mini.z));
           vec3 intersect = rayPosition + rayDirection*d;
           vec3 uv3d = intersect - mapPos;

			if (bottomLevelBrickPtr.voxelsDoBeAllSame == 1)
			{
				return true;
			}

			if (vx_TraceRayVoxels(uvw * BL_BRICK_SIDE_LENGTH, rayDirection, bottomLevelBrickPtr))
			{
				return true;
			}
		}

        vec4 conds = step(sideDist.xxyy, sideDist.yzzx); // same as vec4(sideDist.xxyy <= sideDist.yzzx);
        
        cases.x = conds.x * conds.y;                    // if       x dir
        cases.y = (1.0 - cases.x) * conds.z * conds.w;  // else if  y dir
        cases.z = (1.0 - cases.x) * (1.0 - cases.y);    // else     z dir
        
        sideDist += max((2.0 * cases - 1.0) * deltaDist, 0.0);
        
        mapPos += cases * stepDir;
	}

	return false;
}

// Trace ray that traverses top-level grids.
// TODO: output the hit TopLevelBrickPtr to reduce reads in subsequent granular traces.
bool vx_TraceRayTopLevelBricks(vec3 rayPosition, vec3 rayDirection, float tMax)
{
	rayPosition /= TL_BRICK_VOXELS_PER_SIDE;
	vec3 mapPos = floor(rayPosition);
    const vec3 deltaDist = 1.0 / abs(rayDirection);
    const vec3 S = vec3(step(0.0, rayDirection));
    const vec3 stepDir = 2 * S - 1;
    vec3 sideDist = (S - stepDir * fract(rayPosition)) * deltaDist;
	
	vec3 cases = sideDist;

    for (int i = 0; i < 5; i++)
    {
		// For the top level, traversal outside the map area is ok, just skip
        if(all(greaterThanEqual(mapPos, vec3(0))) && all(lessThan(mapPos, pc.topLevelBricksDims)))
		{
			gTopLevelBricksTraversed++;
			const uint topLevelIndex = FlattenTopLevelBrickCoord(ivec3(mapPos));
			TopLevelBrickPtr topLevelBrickPtr = TOP_LEVEL_PTRS[pc.topLevelBrickPtrsBaseIndex + topLevelIndex];
			if (!(topLevelBrickPtr.voxelsDoBeAllSame == 1 && topLevelBrickPtr.topLevelBrickIndexOrVoxelIfAllSame == 0)) // If brick is not all air
			{
				const vec3 p = mapPos + 0.5 - stepDir * 0.5; // Point on axis plane
				const vec3 normal = vec3(ivec3(vec3(cases))) * -vec3(stepDir);
				// Degenerate if ray starts inside a homogeneous top-level brick
				const float t = (dot(normal, p - rayPosition)) / dot(normal, rayDirection);
				const vec3 hitWorldPos = rayPosition + rayDirection * t; // TODO?????
				vec3 uvw = hitWorldPos - mapPos; // Don't use fract here

				if (i == 0)
				{
					uvw = (rayPosition - mapPos);
				}

				if (topLevelBrickPtr.voxelsDoBeAllSame == 1)
				{
					return true;
				}

				if (vx_TraceRayBottomLevelBricks(uvw * TL_BRICK_SIDE_LENGTH, rayDirection, topLevelBrickPtr))
				{
					return true;
				}
			}
		}

        vec4 conds = step(sideDist.xxyy, sideDist.yzzx); // same as vec4(sideDist.xxyy <= sideDist.yzzx);
        
        cases.x = conds.x * conds.y;                    // if       x dir
        cases.y = (1.0 - cases.x) * conds.z * conds.w;  // else if  y dir
        cases.z = (1.0 - cases.x) * (1.0 - cases.y);    // else     z dir
        
        sideDist += max((2.0 * cases - 1.0) * deltaDist, 0.0);
        
        mapPos += cases * stepDir;
	}

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
        if(all(greaterThanEqual(mapPos, vec3(0))) && all(lessThan(mapPos, ivec3(pc.dimensions))))
        {
			const voxel_t voxel = GetVoxelAt(ivec3(mapPos));
			if (voxel != 0)
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

				hit.voxel = voxel;
				hit.voxelPosition = ivec3(mapPos);
				hit.positionWorld = hitWorldPos;
				hit.texCoord = texCoord;
				hit.flatNormalWorld = normal;
				return true;
			}
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
	
	vec3 o_color = {0, 0, 0};

	//HitSurfaceParameters hit;
	//if (vx_TraceRayMultiLevel2(rayPos, rayDir, 512, hit))
	if (vx_TraceRayTopLevelBricks(rayPos, rayDir, 512))
	{
		//imageStore(pc.outputImage, gid, vec4(hsv_to_rgb(vec3(MM_Hash3(hit.voxelPosition), 0.55, 0.8)), 1));
		//imageStore(pc.outputImage, gid, vec4(mod(abs(hit.positionWorld + .5), 1), 1));
		//imageStore(pc.outputImage, gid, vec4(hit.flatNormalWorld * .5 + .5, 1));
		//imageStore(pc.outputImage, gid, vec4(hit.texCoord, 0, 1));
		o_color += vec3(0.5);
	}
	else
	{
		o_color += rayDir * .5 + .5;
	}
	o_color += vec3(gTopLevelBricksTraversed / 8, gBottomLevelBricksTraversed / 64, gVoxelsTraversed / 512);
	//o_color = vec3(gTopLevelBricksTraversed / 8);
	//o_color = vec3(gBottomLevelBricksTraversed / 64);
	//o_color = vec3(gVoxelsTraversed / 512);

	imageStore(pc.outputImage, gid, vec4(o_color, 1));
}
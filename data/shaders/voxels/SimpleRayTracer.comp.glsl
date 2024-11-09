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
	
	const vec3 rayDir = normalize(UnprojectUV_ZO(0.99, uv, uniforms.invViewProj) - uniforms.cameraPos.xyz);
	const vec3 rayPos = uniforms.cameraPos.xyz;
	
	ivec3 mapPos = ivec3(floor(rayPos + 0.));

	vec3 deltaDist = abs(vec3(length(rayDir)) / rayDir);
	
	ivec3 rayStep = ivec3(sign(rayDir));

	vec3 sideDist = (sign(rayDir) * (vec3(mapPos) - rayPos) + (sign(rayDir) * 0.5) + 0.5) * deltaDist; 
	
	bvec3 mask;
	
	bool hit = false;
	for (int i = 0; i < 2048; i++)
	{
		if (all(greaterThanEqual(mapPos, ivec3(0))) && all(lessThan(mapPos, pc.dimensions)) && GetVoxelAt(mapPos) != 0)
		{
			hit = true;
			break;
		}

        mask = lessThanEqual(sideDist.xyz, min(sideDist.yzx, sideDist.zxy));
		
		//All components of mask are false except for the corresponding largest component
		//of sideDist, which is the axis along which the ray should be incremented.
		
		sideDist += vec3(mask) * deltaDist;
		mapPos += ivec3(vec3(mask)) * rayStep;
	}

	if (hit)
	{
		imageStore(pc.outputImage, gid, vec4(hsv_to_rgb(vec3(MM_Hash3(mapPos), 0.55, 0.8)), 1));
	}
	else
	{
		imageStore(pc.outputImage, gid, vec4(rayDir * .5 + .5, 1));
	}
}
#ifndef VOXEL_H_GLSL
#define VOXEL_H_GLSL
#include "../GlobalUniforms.h.glsl"
#include "../Hash.h.glsl"
#include "../Light.h.glsl"
#include "../Math.h.glsl"
#include "../Pbr.h.glsl"
#include "../Resources.h.glsl"
#include "../Utility.h.glsl"


const int BL_BRICK_SIDE_LENGTH = 8;
const int CELLS_PER_BL_BRICK   = BL_BRICK_SIDE_LENGTH * BL_BRICK_SIDE_LENGTH * BL_BRICK_SIDE_LENGTH;

const int TL_BRICK_SIDE_LENGTH = 8;
const int CELLS_PER_TL_BRICK   = TL_BRICK_SIDE_LENGTH * TL_BRICK_SIDE_LENGTH * TL_BRICK_SIDE_LENGTH;

const int VOXELS_PER_TL_BRICK      = CELLS_PER_TL_BRICK * CELLS_PER_BL_BRICK * CELLS_PER_TL_BRICK;
const int TL_BRICK_VOXELS_PER_SIDE = TL_BRICK_SIDE_LENGTH * BL_BRICK_SIDE_LENGTH;

#define voxel_t uint

struct OccupancyBitmask
{
  uint bitmask[16];
};

struct BottomLevelBrick
{
  OccupancyBitmask occupancy;
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
}
topLevelPtrsBuffers[];

FVOG_DECLARE_STORAGE_BUFFERS(TopLevelBricks)
{
  TopLevelBrick topLevelBricks[];
}
topLevelBricksBuffers[];

FVOG_DECLARE_STORAGE_BUFFERS(BottomLevelBricks)
{
  BottomLevelBrick bottomLevelBricks[];
}
bottomLevelBricksBuffers[];

#define HAS_BASE_COLOR_TEXTURE       (1 << 0)
#define HAS_EMISSION_TEXTURE         (1 << 1)
#define RANDOMIZE_TEXCOORDS_ROTATION (1 << 2)
#define IS_INVISIBLE                 (1 << 3)

struct GpuVoxelMaterial
{
  FVOG_UINT32 materialFlags;
  Texture2D baseColorTexture;
  FVOG_VEC3 baseColorFactor;
  Texture2D emissionTexture;
  FVOG_VEC3 emissionFactor;
};

FVOG_DECLARE_STORAGE_BUFFERS_2(VoxelMaterials)
{
  GpuVoxelMaterial materials[];
}
voxelMaterialsBuffers[];

FVOG_DECLARE_STORAGE_BUFFERS_2(Lights)
{
  GpuLight lights[];
}
lightsBuffers[];

struct Voxels
{
  FVOG_IVEC3 topLevelBricksDims;
  FVOG_UINT32 topLevelBrickPtrsBaseIndex;
  FVOG_IVEC3 dimensions;
  FVOG_UINT32 bufferIdx;
  FVOG_UINT32 materialBufferIdx;
  Sampler voxelSampler;
  FVOG_UINT32 numLights;
  FVOG_UINT32 lightBufferIdx;
};

Voxels g_voxels;

#define TOP_LEVEL_PTRS      topLevelPtrsBuffers[g_voxels.bufferIdx].topLevelPtrs
#define TOP_LEVEL_BRICKS    topLevelBricksBuffers[g_voxels.bufferIdx].topLevelBricks
#define BOTTOM_LEVEL_BRICKS bottomLevelBricksBuffers[g_voxels.bufferIdx].bottomLevelBricks

// DO NOT USE- glslang generates inefficient code
bool OccupancyBitmask_Get(OccupancyBitmask occupancy, uint index)
{
  return bool(occupancy.bitmask[index / 32u] & (1u << index % 32u));
}

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

  // assert(glm::all(glm::lessThan(topLevelCoord, topLevelBricksDims_)));
  // assert(glm::all(glm::lessThan(bottomLevelCoord, glm::ivec3(TL_BRICK_SIDE_LENGTH))));
  // assert(glm::all(glm::lessThan(localVoxelCoord, glm::ivec3(BL_BRICK_SIDE_LENGTH))));

  return GridHierarchyCoords(topLevelCoord, bottomLevelCoord, localVoxelCoord);
}

int FlattenTopLevelBrickCoord(ivec3 coord)
{
  return (coord.z * g_voxels.topLevelBricksDims.x * g_voxels.topLevelBricksDims.y) + (coord.y * g_voxels.topLevelBricksDims.x) + coord.x;
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
  const uint topLevelIndex   = FlattenTopLevelBrickCoord(coords.topLevel);
  return TOP_LEVEL_PTRS[g_voxels.topLevelBrickPtrsBaseIndex + topLevelIndex];
}

// NOTE: does not check for validity of topLevelBrickIndexOrVoxelIfAllSame
// TODO: remove after more optimal multi-level traversal is implemented.
BottomLevelBrickPtr GetBottomLevelBrickPtrAt(ivec3 voxelCoord)
{
  GridHierarchyCoords coords        = GetCoordsOfVoxelAt(voxelCoord);
  const uint topLevelIndex          = FlattenTopLevelBrickCoord(coords.topLevel);
  TopLevelBrickPtr topLevelBrickPtr = TOP_LEVEL_PTRS[g_voxels.topLevelBrickPtrsBaseIndex + topLevelIndex];
  const uint bottomLevelIndex       = FlattenBottomLevelBrickCoord(coords.bottomLevel);
  return TOP_LEVEL_BRICKS[topLevelBrickPtr.topLevelBrickIndexOrVoxelIfAllSame].bricks[bottomLevelIndex];
}

voxel_t GetVoxelAt(ivec3 voxelCoord)
{
  GridHierarchyCoords coords = GetCoordsOfVoxelAt(voxelCoord);

  const uint topLevelIndex          = FlattenTopLevelBrickCoord(coords.topLevel);
  TopLevelBrickPtr topLevelBrickPtr = TOP_LEVEL_PTRS[g_voxels.topLevelBrickPtrsBaseIndex + topLevelIndex];

  if (topLevelBrickPtr.voxelsDoBeAllSame != 0)
  {
    return voxel_t(topLevelBrickPtr.topLevelBrickIndexOrVoxelIfAllSame);
  }

  const uint bottomLevelIndex             = FlattenBottomLevelBrickCoord(coords.bottomLevel);
  BottomLevelBrickPtr bottomLevelBrickPtr = TOP_LEVEL_BRICKS[topLevelBrickPtr.topLevelBrickIndexOrVoxelIfAllSame].bricks[bottomLevelIndex];

  if (bottomLevelBrickPtr.voxelsDoBeAllSame != 0)
  {
    return voxel_t(bottomLevelBrickPtr.bottomLevelBrickIndexOrVoxelIfAllSame);
  }

  const uint localVoxelIndex = FlattenVoxelCoord(coords.localVoxel);
  return BOTTOM_LEVEL_BRICKS[bottomLevelBrickPtr.bottomLevelBrickIndexOrVoxelIfAllSame].voxels[localVoxelIndex];
}

bool GetOccupancyAt(ivec3 voxelCoord)
{
  GridHierarchyCoords coords = GetCoordsOfVoxelAt(voxelCoord);

  const uint topLevelIndex          = FlattenTopLevelBrickCoord(coords.topLevel);
  TopLevelBrickPtr topLevelBrickPtr = TOP_LEVEL_PTRS[g_voxels.topLevelBrickPtrsBaseIndex + topLevelIndex];

  if (topLevelBrickPtr.voxelsDoBeAllSame != 0)
  {
    return voxel_t(topLevelBrickPtr.topLevelBrickIndexOrVoxelIfAllSame) != 0;
  }

  const uint bottomLevelIndex             = FlattenBottomLevelBrickCoord(coords.bottomLevel);
  BottomLevelBrickPtr bottomLevelBrickPtr = TOP_LEVEL_BRICKS[topLevelBrickPtr.topLevelBrickIndexOrVoxelIfAllSame].bricks[bottomLevelIndex];

  if (bottomLevelBrickPtr.voxelsDoBeAllSame != 0)
  {
    return voxel_t(bottomLevelBrickPtr.bottomLevelBrickIndexOrVoxelIfAllSame) != 0;
  }

  const uint localVoxelIndex = FlattenVoxelCoord(coords.localVoxel);
  return bool(BOTTOM_LEVEL_BRICKS[bottomLevelBrickPtr.bottomLevelBrickIndexOrVoxelIfAllSame].occupancy.bitmask[localVoxelIndex / 32u] & (1u << localVoxelIndex % 32u));
}

struct HitSurfaceParameters
{
  voxel_t voxel;
  vec3 voxelPosition;
  vec3 positionWorld;
  vec3 flatNormalWorld;
  vec2 texCoords;
};

float gTopLevelBricksTraversed    = 0;
float gBottomLevelBricksTraversed = 0;
float gVoxelsTraversed            = 0;

#define EPSILON 1e-5

struct vx_InitialDDAState
{
  vec3 deltaDist;
  vec3 S;
  vec3 stepDir;
};

vec2 vx_GetTexCoords(vec3 normal, vec3 uvw)
{
  // Ugly, hacky way to get texCoords, but squirreled away in a function
  if (normal.x > 0)
    return vec2(1 - uvw.z, uvw.y);
  if (normal.x < 0)
    return vec2(uvw.z, uvw.y);
  if (normal.y > 0)
    return vec2(uvw.x, 1 - uvw.z); // Arbitrary
  if (normal.y < 0)
    return vec2(uvw.x, uvw.z);
  if (normal.z > 0)
    return vec2(uvw.x, uvw.y);
  return vec2(1 - uvw.x, uvw.y);
}

bool vx_IsVisible(voxel_t voxel)
{
  if (voxel == 0)
  {
    return false;
  }
  GpuVoxelMaterial material = voxelMaterialsBuffers[g_voxels.materialBufferIdx].materials[voxel];
  return !bool(material.materialFlags & IS_INVISIBLE);
}

// Ray position in (0, BL_BRICK_SIDE_LENGTH)
bool vx_TraceRayVoxels(vec3 rayPosition, vec3 rayDirection, BottomLevelBrickPtr bottomLevelBrickPtr, vx_InitialDDAState init, vec3 cases, out HitSurfaceParameters hit)
{
  rayPosition          = clamp(rayPosition, vec3(EPSILON), vec3(BL_BRICK_SIDE_LENGTH - EPSILON));
  vec3 mapPos          = floor(rayPosition);
  const vec3 deltaDist = init.deltaDist;
  const vec3 S         = init.S;
  const vec3 stepDir   = init.stepDir;
  vec3 sideDist        = (S - stepDir * fract(rayPosition)) * deltaDist;
  
  for (int i = 0; all(greaterThanEqual(mapPos, vec3(0))) && all(lessThan(mapPos, vec3(BL_BRICK_SIDE_LENGTH))); i++)
  {
    gVoxelsTraversed++;
    const uint localVoxelIndex = FlattenVoxelCoord(ivec3(mapPos));
    // Calling OccupancyBitmask_Get here destroys perf, presumably because the optimizer can't remove the array copy.
    //const bool occupancy = OccupancyBitmask_Get(BOTTOM_LEVEL_BRICKS[bottomLevelBrickPtr.bottomLevelBrickIndexOrVoxelIfAllSame].occupancy, localVoxelIndex);
    const bool occupancy = bool(BOTTOM_LEVEL_BRICKS[bottomLevelBrickPtr.bottomLevelBrickIndexOrVoxelIfAllSame].occupancy.bitmask[localVoxelIndex / 32u] & (1u << localVoxelIndex % 32u));
    if (occupancy)
    {
      const voxel_t voxel = BOTTOM_LEVEL_BRICKS[bottomLevelBrickPtr.bottomLevelBrickIndexOrVoxelIfAllSame].voxels[localVoxelIndex];
      const vec3 p      = mapPos + 0.5 - stepDir * 0.5;
      const vec3 normal = vec3(ivec3(vec3(cases))) * -vec3(stepDir);

      const float t    = (dot(normal, p - rayPosition)) / dot(normal, rayDirection);
      vec3 hitWorldPos = rayPosition + rayDirection * t;
      vec3 uvw         = hitWorldPos - mapPos;

      if (i == 0)
      {
        uvw         = rayPosition - mapPos;
        hitWorldPos = rayPosition;
      }

      hit.voxel           = voxel;
      hit.voxelPosition   = ivec3(mapPos);
      hit.positionWorld   = hitWorldPos;
      hit.texCoords       = vx_GetTexCoords(normal, uvw);
      hit.flatNormalWorld = normal;
      return true;
    }

    vec4 conds = step(sideDist.xxyy, sideDist.yzzx); // same as vec4(sideDist.xxyy <= sideDist.yzzx);

    cases.x = conds.x * conds.y;                   // if       x dir
    cases.y = (1.0 - cases.x) * conds.z * conds.w; // else if  y dir
    cases.z = (1.0 - cases.x) * (1.0 - cases.y);   // else     z dir

    sideDist += max((2.0 * cases - 1.0) * deltaDist, 0.0);

    mapPos += cases * stepDir;
  }

  return false;
}

// Ray position in (0, TL_BRICK_SIDE_LENGTH)
bool vx_TraceRayBottomLevelBricks(vec3 rayPosition, vec3 rayDirection, TopLevelBrickPtr topLevelBrickPtr, vx_InitialDDAState init, vec3 cases, out HitSurfaceParameters hit)
{
  rayPosition          = clamp(rayPosition, vec3(EPSILON), vec3(TL_BRICK_SIDE_LENGTH - EPSILON));
  vec3 mapPos          = floor(rayPosition);
  const vec3 deltaDist = init.deltaDist;
  const vec3 S         = init.S;
  const vec3 stepDir   = init.stepDir;
  vec3 sideDist        = (S - stepDir * fract(rayPosition)) * deltaDist;

  for (int i = 0; all(greaterThanEqual(mapPos, vec3(0))) && all(lessThan(mapPos, ivec3(TL_BRICK_SIDE_LENGTH))); i++)
  {
    const uint bottomLevelIndex             = FlattenBottomLevelBrickCoord(ivec3(mapPos));
    BottomLevelBrickPtr bottomLevelBrickPtr = TOP_LEVEL_BRICKS[topLevelBrickPtr.topLevelBrickIndexOrVoxelIfAllSame].bricks[bottomLevelIndex];
    if (!(bottomLevelBrickPtr.voxelsDoBeAllSame == 1 && !vx_IsVisible(bottomLevelBrickPtr.bottomLevelBrickIndexOrVoxelIfAllSame)))
    {
      gBottomLevelBricksTraversed++;
      const vec3 p      = mapPos + 0.5 - stepDir * 0.5; // Point on axis plane
      const vec3 normal = vec3(ivec3(vec3(cases))) * -vec3(stepDir);
      const float t     = (dot(normal, p - rayPosition)) / dot(normal, rayDirection);
      vec3 hitWorldPos  = rayPosition + rayDirection * t;
      vec3 uvw          = hitWorldPos - mapPos;

      if (i == 0)
      {
        uvw         = rayPosition - mapPos;
        hitWorldPos = rayPosition;
      }

      if (bottomLevelBrickPtr.voxelsDoBeAllSame == 1)
      {
        hit.voxel = voxel_t(bottomLevelBrickPtr.bottomLevelBrickIndexOrVoxelIfAllSame);
        // Hack, but seems to work
        hit.voxelPosition   = ivec3(hitWorldPos * BL_BRICK_SIDE_LENGTH - EPSILON);
        hit.positionWorld   = hitWorldPos * BL_BRICK_SIDE_LENGTH;
        hit.texCoords       = fract(vx_GetTexCoords(normal, uvw) * BL_BRICK_SIDE_LENGTH); // Might behave poorly
        hit.flatNormalWorld = normal;
        return true;
      }

      if (vx_TraceRayVoxels(uvw * BL_BRICK_SIDE_LENGTH, rayDirection, bottomLevelBrickPtr, init, cases, hit))
      {
        hit.voxelPosition += mapPos * BL_BRICK_SIDE_LENGTH;
        hit.positionWorld += mapPos * BL_BRICK_SIDE_LENGTH;
        return true;
      }
    }

    vec4 conds = step(sideDist.xxyy, sideDist.yzzx); // same as vec4(sideDist.xxyy <= sideDist.yzzx);

    cases.x = conds.x * conds.y;                   // if       x dir
    cases.y = (1.0 - cases.x) * conds.z * conds.w; // else if  y dir
    cases.z = (1.0 - cases.x) * (1.0 - cases.y);   // else     z dir

    sideDist += max((2.0 * cases - 1.0) * deltaDist, 0.0);

    mapPos += cases * stepDir;
  }

  return false;
}

// Trace ray that traverses top-level grids, then lower-level grids when there's a hit.
bool vx_TraceRayMultiLevel(vec3 rayPosition, vec3 rayDirection, float tMax, out HitSurfaceParameters hit)
{
  rayPosition /= TL_BRICK_VOXELS_PER_SIDE;
  vec3 mapPos          = floor(rayPosition);
  const vec3 deltaDist = 1.0 / abs(rayDirection);
  const vec3 S         = vec3(step(0.0, rayDirection));
  const vec3 stepDir   = 2 * S - 1;
  vec3 sideDist        = (S - stepDir * fract(rayPosition)) * deltaDist;

  // Cache these computations
  vx_InitialDDAState init;
  init.deltaDist = deltaDist;
  init.S         = S;
  init.stepDir   = stepDir;

  vec3 cases = sideDist;

  bool hasHit = false;
  for (int i = 0; i < tMax; i++)
  {
    // For the top level, traversal outside the map area is ok, just skip
    if (all(greaterThanEqual(mapPos, vec3(0))) && all(lessThan(mapPos, g_voxels.topLevelBricksDims)))
    {
      gTopLevelBricksTraversed++;
      const uint topLevelIndex          = FlattenTopLevelBrickCoord(ivec3(mapPos));
      TopLevelBrickPtr topLevelBrickPtr = TOP_LEVEL_PTRS[g_voxels.topLevelBrickPtrsBaseIndex + topLevelIndex];
      if (!(topLevelBrickPtr.voxelsDoBeAllSame == 1 && !vx_IsVisible(topLevelBrickPtr.topLevelBrickIndexOrVoxelIfAllSame))) // If brick is not all invisible
      {
        const vec3 p      = mapPos + 0.5 - stepDir * 0.5; // Point on axis plane
        const vec3 normal = vec3(ivec3(vec3(cases))) * -vec3(stepDir);
        // Degenerate if ray starts inside a homogeneous top-level brick
        const float t    = (dot(normal, p - rayPosition)) / dot(normal, rayDirection);
        vec3 hitWorldPos = rayPosition + rayDirection * t;
        vec3 uvw         = hitWorldPos - mapPos; // Don't use fract here

        if (i == 0)
        {
          uvw         = rayPosition - mapPos;
          hitWorldPos = rayPosition;
        }

        if (topLevelBrickPtr.voxelsDoBeAllSame == 1)
        {
          hit.voxel = voxel_t(topLevelBrickPtr.topLevelBrickIndexOrVoxelIfAllSame);
          // Hack, but seems to work
          hit.voxelPosition   = ivec3(hitWorldPos * TL_BRICK_VOXELS_PER_SIDE - EPSILON);
          hit.positionWorld   = hitWorldPos * TL_BRICK_VOXELS_PER_SIDE;
          hit.texCoords       = fract(vx_GetTexCoords(normal, uvw) * TL_BRICK_VOXELS_PER_SIDE); // Might behave poorly
          hit.flatNormalWorld = normal;
          hasHit              = true;
          break;
        }

        if (vx_TraceRayBottomLevelBricks(uvw * TL_BRICK_SIDE_LENGTH, rayDirection, topLevelBrickPtr, init, cases, hit))
        {
          hit.voxelPosition += mapPos * TL_BRICK_VOXELS_PER_SIDE;
          hit.positionWorld += mapPos * TL_BRICK_VOXELS_PER_SIDE;
          hasHit = true;
          break;
        }
      }
    }

    vec4 conds = step(sideDist.xxyy, sideDist.yzzx); // same as vec4(sideDist.xxyy <= sideDist.yzzx);

    cases.x = conds.x * conds.y;                   // if       x dir
    cases.y = (1.0 - cases.x) * conds.z * conds.w; // else if  y dir
    cases.z = (1.0 - cases.x) * (1.0 - cases.y);   // else     z dir

    sideDist += max((2.0 * cases - 1.0) * deltaDist, 0.0);

    mapPos += cases * stepDir;
  }

  if (hasHit)
  {
    GpuVoxelMaterial material = voxelMaterialsBuffers[g_voxels.materialBufferIdx].materials[hit.voxel];
    if (true && bool(material.materialFlags & RANDOMIZE_TEXCOORDS_ROTATION))
    {
      // Random quarter turn
      const float cos90[4] = {1, 0, -1, 0};
      const uint quarters  = uint(10000 * MM_Hash3(hit.voxelPosition + hit.flatNormalWorld * 0.5));
      const float cosTheta = cos90[quarters % 4];
      const float sinTheta = cos90[(quarters + 1) % 4];
      const mat2 rot       = {{cosTheta, sinTheta}, {-sinTheta, cosTheta}};
      hit.texCoords -= .5;
      hit.texCoords = rot * hit.texCoords;
      hit.texCoords += 0.5;
    }
  }
  return hasHit;
}

bool vx_TraceRaySimple(vec3 rayPosition, vec3 rayDirection, float tMax, out HitSurfaceParameters hit)
{
  // https://www.shadertoy.com/view/X3BXDd
  vec3 mapPos = floor(rayPosition); // integer cell coordinate of initial cell

  const vec3 deltaDist = 1.0 / abs(rayDirection); // ray length required to step from one cell border to the next in x, y and z directions

  const vec3 S       = vec3(step(0.0, rayDirection)); // S is rayDir non-negative? 0 or 1
  const vec3 stepDir = 2 * S - 1;                     // Step sign

  // if 1./abs(rayDir[i]) is inf, then rayDir[i] is 0., but then S = step(0., rayDir[i]) is 1
  // so S cannot be 0. while deltaDist is inf, and stepDir * fract(pos) can never be 1.
  // Therefore we should not have to worry about getting NaN here :)

  // initial distance to cell sides, then relative difference between traveled sides
  vec3 sideDist = (S - stepDir * fract(rayPosition)) * deltaDist; // alternative: //sideDist = (S-stepDir * (pos - map)) * deltaDist;

  for (int i = 0; i < tMax; i++)
  {
    // Decide which way to go!
    vec4 conds = step(sideDist.xxyy, sideDist.yzzx); // same as vec4(sideDist.xxyy <= sideDist.yzzx);

    // This mimics the if, elseif and else clauses
    // * is 'and', 1.-x is negation
    vec3 cases = vec3(0);
    cases.x    = conds.x * conds.y;                   // if       x dir
    cases.y    = (1.0 - cases.x) * conds.z * conds.w; // else if  y dir
    cases.z    = (1.0 - cases.x) * (1.0 - cases.y);   // else     z dir

    // usually would have been:     sideDist += cases * deltaDist;
    // but this gives NaN when  cases[i] * deltaDist[i]  becomes  0. * inf
    // This gives NaN result in a component that should not have been affected,
    // so we instead give negative results for inf by mapping 'cases' to +/- 1
    // and then clamp negative values to zero afterwards, giving the correct result! :)
    sideDist += max((2.0 * cases - 1.0) * deltaDist, 0.0);

    mapPos += cases * stepDir;

    // Putting the exit condition down here implicitly skips the first voxel
    if (all(greaterThanEqual(mapPos, vec3(0))) && all(lessThan(mapPos, ivec3(g_voxels.dimensions))))
    {
      const bool occupancy = GetOccupancyAt(ivec3(mapPos));
      if (occupancy)
      {
        const voxel_t voxel = GetVoxelAt(ivec3(mapPos));
        const vec3 p      = mapPos + 0.5 - stepDir * 0.5; // Point on axis plane
        const vec3 normal = vec3(ivec3(vec3(cases))) * -vec3(stepDir);

        // Solve ray plane intersection equation: dot(n, ro + t * rd - p) = 0.
        // for t :
        const float t          = (dot(normal, p - rayPosition)) / dot(normal, rayDirection);
        const vec3 hitWorldPos = rayPosition + rayDirection * t;
        const vec3 uvw         = hitWorldPos - mapPos; // Don't use fract here

        hit.voxel           = voxel;
        hit.voxelPosition   = ivec3(mapPos);
        hit.positionWorld   = hitWorldPos;
        hit.texCoords       = vx_GetTexCoords(normal, uvw);
        hit.flatNormalWorld = normal;
        return true;
      }
    }
  }

  return false;
}

vec3 GetHitAlbedo(HitSurfaceParameters hit)
{
  GpuVoxelMaterial material = voxelMaterialsBuffers[g_voxels.materialBufferIdx].materials[hit.voxel];
  vec3 albedo               = material.baseColorFactor;
  if (bool(material.materialFlags & HAS_BASE_COLOR_TEXTURE))
  {
    albedo *= textureLod(material.baseColorTexture, g_voxels.voxelSampler, hit.texCoords, 0).rgb;
  }
  return albedo;
  // return vec3(hsv_to_rgb(vec3(MM_Hash3(ivec3(hit.voxelPosition) % 3), 0.55, 0.8)));
  // return vec3(mod(abs(hit.positionWorld + .5), 1));
  // return vec3(hit.positionWorld / 64);
  // return vec3(hit.flatNormalWorld * .5 + .5);
  // return vec3(hit.texCoords, 0);
  // return vec3(0.5);
}

vec3 GetHitEmission(HitSurfaceParameters hit)
{
  GpuVoxelMaterial material = voxelMaterialsBuffers[g_voxels.materialBufferIdx].materials[hit.voxel];
  vec3 emission             = material.emissionFactor;
  if (bool(material.materialFlags & HAS_EMISSION_TEXTURE))
  {
    const vec4 texEmission = textureLod(material.emissionTexture, g_voxels.voxelSampler, hit.texCoords, 0);
    emission *= texEmission.rgb * texEmission.a;
  }
  return emission;
  // if (ivec3(hit.voxelPosition / 3) % 4 == ivec3(0))
  //  if (distance(vec3(hit.voxelPosition), vec3(20, 10, 40)) < 5)
  //  {
  //      return vec3(5);
  //  }

  // return vec3(0);
}

float TraceSunRay(vec3 rayPosition, vec3 sunDir)
{
  HitSurfaceParameters hit2;
  if (vx_TraceRayMultiLevel(rayPosition, sunDir, 64, hit2))
  {
    return 0;
  }

  return 1;
}

float GetPunctualLightVisibility(vec3 surfacePos, uint lightIndex)
{
  const GpuLight light  = lightsBuffers[g_voxels.lightBufferIdx].lights[lightIndex];
  const float lightDist = distance(surfacePos, light.position);

  HitSurfaceParameters hit;
  if (vx_TraceRayMultiLevel(surfacePos, normalize(light.position - surfacePos), lightDist * 3, hit))
  {
    const float hitDist = distance(hit.positionWorld, surfacePos);
    if (hitDist < lightDist)
    {
      return 0.0;
    }
  }
  return 1.0;
}

vec3 TraceIndirectLighting(ivec2 gid, vec3 rayPosition, vec3 normal, uint samples, uint bounces, Texture2D noiseTexture)
{
  vec3 indirectIlluminance = {0, 0, 0};

  // This state must be independent of the state used for sampling a direction
  // uint randState = PCG_Hash(shadingUniforms.frameNumber + PCG_Hash(gid.y + PCG_Hash(gid.x)));
  uint randState = PCG_Hash(PCG_Hash(gid.y + PCG_Hash(gid.x)));

  // All pixels should have the same sequence
  // uint noiseOffsetState = PCG_Hash(shadingUniforms.frameNumber);
  uint noiseOffsetState = 12340;

  vec3 currentAlbedo = vec3(1);
  for (uint ptSample = 0; ptSample < samples; ptSample++)
  {
    // These additional sources of randomness are useful when the noise texture is a low resolution
    // const vec2 perSampleNoise = shadingUniforms.random + Hammersley(ptSample, shadingUniforms.numGiBounces);
    vec2 perSampleNoise = Hammersley(ptSample, samples);

    // vec3 prevRayDir = -fragToCameraDir;
    vec3 curRayPos = rayPosition;
    // Surface curSurface = surface;

    vec3 throughput = {1, 1, 1};
    for (uint bounce = 0; bounce < bounces; bounce++)
    {
      const ivec2 noiseOffset       = ivec2(PCG_RandU32(noiseOffsetState), PCG_RandU32(noiseOffsetState));
      const vec2 noiseTextureSample = texelFetch(noiseTexture, (gid + noiseOffset) % textureSize(noiseTexture, 0), 0).xy;
      // const vec2 noiseTextureSample = {PCG_RandFloat(randState, 0, 1), PCG_RandFloat(randState, 0, 1)};
      const vec2 xi        = fract(perSampleNoise + noiseTextureSample);
      const vec3 curRayDir = normalize(map_to_unit_hemisphere_cosine_weighted(xi, normal));
      // return curRayDir * .5 + .5;
      const float cos_theta = clamp(dot(normal, curRayDir), 0, 1);
      if (cos_theta <= 0) // Terminate path
      {
        break;
      }
      const float pdf = cosine_weighted_hemisphere_PDF(cos_theta);
      ASSERT_MSG(isfinite(pdf), "PDF is not finite!\n");
      // const vec3 brdf_over_pdf = curSurface.albedo / M_PI / pdf; // Lambertian
      const vec3 brdf_over_pdf = currentAlbedo / M_PI / pdf; // Lambertian
      // const vec3 brdf_over_pdf = BRDF(-prevRayDir, curRayDir, curSurface) / pdf; // Cook-Torrance

      throughput *= cos_theta * brdf_over_pdf;

      HitSurfaceParameters hit;
      if (vx_TraceRayMultiLevel(curRayPos, curRayDir, 16, hit))
      {
        indirectIlluminance += throughput * GetHitEmission(hit);

        // prevRayDir = curRayDir;
        curRayPos     = hit.positionWorld + hit.flatNormalWorld * 0.0001;
        normal        = hit.flatNormalWorld;
        currentAlbedo = GetHitAlbedo(hit);

        // curSurface.albedo = hit.albedo.rgb;
        // curSurface.normal = hit.smoothNormalWorld;
        // curSurface.position = hit.positionWorld;
        // curSurface.metallic = hit.metallic;
        // curSurface.perceptualRoughness = hit.roughness;
        // curSurface.reflectance = 0.5;
        // curSurface.f90 = 1.0;

        // Sun NEE. Direct illumination is handled outside this loop, so no further attenuation is required.
        //   const uint numSunShadowRays = 1;
        //   const float sunShadow = ShadowSunRayTraced(hit.positionWorld + hit.flatNormalWorld * 0.0001,
        //     -shadingUniforms.sunDir.xyz,
        //     hit.smoothNormalWorld,
        //     shadowUniforms.rtSunDiameterRadians,
        //     xi,
        //     numSunShadowRays);

        // TODO: merge divergent ray tracing in these branches (perf is literally halved when there's even one local light)
        const uint lightIndex = PCG_RandU32(randState) % (g_voxels.numLights + 1);
        const float lightPdf  = 1.0 / (g_voxels.numLights + 1);
        if (lightIndex == 0)
        {
          const vec3 sunDir     = normalize(vec3(.7, 1, .3));
          const float sunShadow = TraceSunRay(hit.positionWorld + hit.flatNormalWorld * 1e-4, sunDir);

          // indirectIlluminance += sunColor_internal_space *
          indirectIlluminance += throughput *
                                // BRDF(-curRayDir, -shadingUniforms.sunDir.xyz, curSurface) *
                                //(curSurface.albedo / M_PI) *
                                (currentAlbedo / M_PI) * clamp(dot(hit.flatNormalWorld, sunDir), 0.0, 1.0) * sunShadow * 10000 /
                                // sunShadow /
                                solid_angle_mapping_PDF(radians(0.5));
        }
        else
        {
          // Local light NEE
          GpuLight light = lightsBuffers[g_voxels.lightBufferIdx].lights[lightIndex - 1];

          const float visibility = GetPunctualLightVisibility(hit.positionWorld + hit.flatNormalWorld * 0.0001, lightIndex - 1);
          if (visibility > 0)
          {
            Surface surface;
            surface.albedo   = GetHitAlbedo(hit);
            surface.normal   = hit.flatNormalWorld;
            surface.position = hit.positionWorld;
            indirectIlluminance += throughput * visibility * EvaluatePunctualLightLambert(light, surface, COLOR_SPACE_sRGB_LINEAR) / lightPdf;
          }
        }
      }
      else
      {
        // Miss, add sky contribution
        //   const vec3 skyEmittance = color_convert_src_to_dst(shadingUniforms.skyIlluminance.rgb * shadingUniforms.skyIlluminance.a,
        //     COLOR_SPACE_sRGB_LINEAR,
        //     shadingUniforms.shadingInternalColorSpace);
        // const vec3 skyEmittance = {.1, .3, .5};
        const vec3 skyEmittance = curRayDir * .5 + .5;
        indirectIlluminance += skyEmittance * throughput;
        break;
      }
    }
  }

  return indirectIlluminance / samples;
}

void vx_Init(Voxels voxels)
{
  g_voxels = voxels;
}

#endif // VOXEL_H_GLSL
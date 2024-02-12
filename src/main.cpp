/* 
 *
 * TODO rendering:
 * Core:
 * [X] glTF loader
 * [X] FSR 2
 * [X] PBR punctual lights
 * [ ] Skinned animation
 * [ ] Transparency
 *
 * Low-level
 * [X] Visibility buffer
 * [X] Frustum culling
 * [X] Hi-z occlusion culling
 * [ ] Clustered light culling
 * [-] Raster occlusion culling
 * [X] Multi-view
 * [X] Triangle culling: https://www.slideshare.net/gwihlidal/optimizing-the-graphics-pipeline-with-compute-gdc-2016
 *   [X] Back-facing
 *   [X] Zero-area
 *   [X] Small primitive (doesn't overlap pixel)
 *   [X] Frustum
 *
 * Atmosphere
 * [ ] Sky
 * [ ] Volumetric fog
 * [ ] Clouds
 *
 * Effects:
 * [X] Bloom
 *
 * Resolve:
 * [X] Auto exposure
 * [ ] Auto whitepoint
 * [ ] Local tonemapping
 * [ ] Purkinje shift
 *
 * Tryhard:
 * [ ] BVH builder
 * [ ] Path tracer
 * [ ] DDGI
 * [ ] Surfel GI
 *
 * TODO UI:
 * [X] Render into an ImGui window
 * [X] Install a font + header from here: https://github.com/juliettef/IconFontCppHeaders
 * [ ] Figure out an epic layout
 * [ ] Config file so stuff doesn't reset every time
 * [ ] Model browser
 * [ ] Command console
 *
 * Debugging:
 * [ ] Meshlet color view
 * [ ] Meshlet HZB LoD view
 * [ ] Frustum visualization for other views
 * [ ] Texture viewer (basically the one from RenderDoc):
 *   [ ] Register textures to list of viewable textures in GUI (perhaps just a vector of Texture*)
 *   [ ] Selector for range of displayable values
 *   [ ] Toggles for visible channels
 *   [ ] Selectors for mip level and array layer, if applicable
 *   [ ] Option to tint the window/image alpha to see the window behind it (i.e., overlay the image onto the main viewport)
 *   [ ] Toggle to scale the image to the viewport (would be helpful to view square textures like the hzb properly overlaid on the scene)
 *   [ ] Standard scroll zoom and drag controls
 */

//#include "FrogRenderer.h"

#include <tracy/Tracy.hpp>

#include <cstring>
#include <iostream>
#include <stdexcept>

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#include "FrogRenderer2.h"

int main()
{
  ZoneScoped;
  
  auto app = FrogRenderer2({
    .name = "FrogRender",
    .vsync = true,
  });
  app.Run();

  return 0;
}

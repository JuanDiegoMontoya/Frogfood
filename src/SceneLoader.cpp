#include "SceneLoader.h"
#include "Application.h"

#include "Fvog/detail/ApiToEnum2.h"
#include "Fvog/detail/Common.h"
#include "Fvog/Rendering2.h"

#include "Renderables.h"

#include "Fvog/detail/ApiToEnum2.h"

#include <tracy/Tracy.hpp>

#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/transform.hpp>

#include "ktx.h"

//#include FWOG_OPENGL_HEADER
#include <volk.h>

// #include <glm/gtx/string_cast.hpp>

#include "Fvog/Buffer2.h"

#include <stb_image.h>

#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/parser.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/types.hpp>

#include <meshoptimizer.h>

#include <algorithm>
#include <chrono>
#include <execution>
#include <filesystem>
#include <iostream>
#include <numeric>
#include <optional>
#include <ranges>
#include <span>
#include <stack>
#include <utility>

#define GL_CLAMP_TO_EDGE          0x812F
#define GL_MIRRORED_REPEAT        0x8370
#define GL_REPEAT                 0x2901
#define GL_LINEAR                 0x2601
#define GL_LINEAR_MIPMAP_LINEAR   0x2703
#define GL_LINEAR_MIPMAP_NEAREST  0x2701
#define GL_NEAREST                0x2600
#define GL_NEAREST_MIPMAP_LINEAR  0x2702
#define GL_NEAREST_MIPMAP_NEAREST 0x2700
#define GL_UNSIGNED_BYTE          0x1401

namespace Utility
{
  namespace // helpers
  {
    enum class ImageUsage
    {
      BASE_COLOR,
      METALLIC_ROUGHNESS,
      NORMAL,
      OCCLUSION,
      EMISSION,
    };

    // Converts a Vulkan BCn VkFormat name to Fwog

    // Converts a format to the sRGB version of itself, for use in a texture view
    Fvog::Format FormatToSrgb(Fvog::Format format)
    {
      switch (format)
      {
      case Fvog::Format::BC1_RGBA_UNORM: return Fvog::Format::BC1_RGBA_SRGB;
      case Fvog::Format::BC1_RGB_UNORM:  return Fvog::Format::BC1_RGB_SRGB;
      case Fvog::Format::BC2_RGBA_UNORM: return Fvog::Format::BC2_RGBA_SRGB;
      case Fvog::Format::BC3_RGBA_UNORM: return Fvog::Format::BC3_RGBA_SRGB;
      case Fvog::Format::BC7_RGBA_UNORM: return Fvog::Format::BC7_RGBA_SRGB;
      case Fvog::Format::R8G8B8A8_UNORM: return Fvog::Format::R8G8B8A8_SRGB;
      default: return format;
      }
    }

    glm::vec2 signNotZero(glm::vec2 v)
    {
      return glm::vec2((v.x >= 0.0f) ? +1.0f : -1.0f, (v.y >= 0.0f) ? +1.0f : -1.0f);
    }

    glm::vec2 float32x3_to_oct(glm::vec3 v)
    {
      glm::vec2 p = glm::vec2{v.x, v.y} * (1.0f / (abs(v.x) + abs(v.y) + abs(v.z)));
      return (v.z <= 0.0f) ? ((1.0f - glm::abs(glm::vec2{p.y, p.x})) * signNotZero(p)) : p;
    }

    auto ConvertGlAddressMode(uint32_t wrap)
    {
      switch (wrap)
      {
      case GL_CLAMP_TO_EDGE: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
      case GL_MIRRORED_REPEAT: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
      case GL_REPEAT: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
      default: assert(false); return VK_SAMPLER_ADDRESS_MODE_REPEAT;
      }
    }

    auto ConvertGlFilterMode(uint32_t filter)
    {
      switch (filter)
      {
      case GL_LINEAR_MIPMAP_LINEAR:  //[[fallthrough]]
      case GL_LINEAR_MIPMAP_NEAREST: //[[fallthrough]]
      case GL_LINEAR: return VK_FILTER_LINEAR;
      case GL_NEAREST_MIPMAP_LINEAR:  //[[fallthrough]]
      case GL_NEAREST_MIPMAP_NEAREST: //[[fallthrough]]
      case GL_NEAREST: return VK_FILTER_NEAREST;
      default: assert(false); return VK_FILTER_LINEAR;
      }
    }

    auto GetGlMipmapFilter(uint32_t minFilter)
    {
      switch (minFilter)
      {
      case GL_LINEAR_MIPMAP_LINEAR: [[fallthrough]];
      case GL_NEAREST_MIPMAP_LINEAR: return VK_SAMPLER_MIPMAP_MODE_LINEAR;
      case GL_LINEAR_MIPMAP_NEAREST: [[fallthrough]];
      case GL_NEAREST_MIPMAP_NEAREST: return VK_SAMPLER_MIPMAP_MODE_NEAREST;
      case GL_LINEAR: [[fallthrough]];
      case GL_NEAREST: [[fallthrough]];
      default: assert(false); return VK_SAMPLER_MIPMAP_MODE_LINEAR;
      }
    }

    uint64_t ImageToBufferSize(Fvog::Format format, Fvog::Extent3D extent)
    {
      if (Fvog::detail::FormatIsBlockCompressed(format))
      {
        return Fvog::detail::BlockCompressedImageSize(format, extent.width, extent.height, extent.depth);
      }

      return extent.width * extent.height * extent.depth * Fvog::detail::FormatStorageSize(format);
    }

    std::vector<Fvog::Texture> LoadImages(Fvog::Device& device, const fastgltf::Asset& asset)
    {
      ZoneScoped;

      auto imageUsages = std::vector<ImageUsage>(asset.images.size(), ImageUsage::BASE_COLOR);

      // Determine how each image is used so we can transcode to the proper format.
      // Assumption: each image has exactly one usage, or is used for both metallic-roughness AND occlusion (which is handled in LoadImages()).
      {
        ZoneScopedN("Determine Image Uses");
        for (const auto& material : asset.materials)
        {
          if (material.pbrData.baseColorTexture && asset.textures[material.pbrData.baseColorTexture->textureIndex].imageIndex)
          {
            imageUsages[*asset.textures[material.pbrData.baseColorTexture->textureIndex].imageIndex] = ImageUsage::BASE_COLOR;
          }
          if (material.normalTexture && asset.textures[material.normalTexture->textureIndex].imageIndex)
          {
            imageUsages[*asset.textures[material.normalTexture->textureIndex].imageIndex] = ImageUsage::NORMAL;
          }
          if (material.pbrData.metallicRoughnessTexture && asset.textures[material.pbrData.metallicRoughnessTexture->textureIndex].imageIndex)
          {
            imageUsages[*asset.textures[material.pbrData.metallicRoughnessTexture->textureIndex].imageIndex] = ImageUsage::METALLIC_ROUGHNESS;
          }
          if (material.occlusionTexture && asset.textures[material.occlusionTexture->textureIndex].imageIndex)
          {
            imageUsages[*asset.textures[material.occlusionTexture->textureIndex].imageIndex] = ImageUsage::OCCLUSION;
          }
          if (material.emissiveTexture && asset.textures[material.emissiveTexture->textureIndex].imageIndex)
          {
            imageUsages[*asset.textures[material.emissiveTexture->textureIndex].imageIndex] = ImageUsage::EMISSION;
          }
        }
      }

      struct RawImageData
      {
        // Used for ktx and non-ktx images alike
        std::unique_ptr<std::byte[]> encodedPixelData = {};
        std::size_t encodedPixelSize = 0;

        bool isKtx = false;
        Fvog::Format formatIfKtx;
        int width = 0;
        int height = 0;
        int pixel_type = GL_UNSIGNED_BYTE;
        int bits = 8;
        int components = 0;
        std::string name;

        // Non-ktx. Raw decoded pixel data
        std::unique_ptr<unsigned char[], decltype([](unsigned char* p) { stbi_image_free(p); })> data = {};

        // ktx
        std::unique_ptr<ktxTexture2, decltype([](ktxTexture2* p) { ktxTexture_Destroy(ktxTexture(p)); })> ktx = {};
      };
      
      auto MakeRawImageData = [](const void* data, std::size_t dataSize, fastgltf::MimeType mimeType, std::string_view name) -> RawImageData
      {
        assert(mimeType == fastgltf::MimeType::JPEG || 
               mimeType == fastgltf::MimeType::PNG ||
               mimeType == fastgltf::MimeType::KTX2 ||
               mimeType == fastgltf::MimeType::GltfBuffer);
        auto dataCopy = std::make_unique<std::byte[]>(dataSize);
        std::copy_n(static_cast<const std::byte*>(data), dataSize, dataCopy.get());

        return RawImageData{
          .encodedPixelData = std::move(dataCopy),
          .encodedPixelSize = dataSize,
          .isKtx = mimeType == fastgltf::MimeType::KTX2,
          .name = std::string(name),
        };
      };

      const auto indices = std::ranges::iota_view((size_t)0, asset.images.size());

      // Load and decode image data locally, in parallel
      auto rawImageData = std::vector<RawImageData>(asset.images.size());

      std::transform(
        std::execution::par,
        indices.begin(),
        indices.end(),
        rawImageData.begin(),
        [&](size_t index)
        {
          ZoneScopedN("Load Image");
          const fastgltf::Image& image = asset.images[index];
          if (image.name.empty())
          {
            constexpr std::string_view unnamed = "Unnamed image";
            ZoneName(unnamed.data(), unnamed.size());
          }
          else
          {
            ZoneName(image.name.c_str(), image.name.size());
          }

          auto rawImage = [&]
          {
            if (const auto* filePath = std::get_if<fastgltf::sources::URI>(&image.data))
            {
              assert(filePath->fileByteOffset == 0); // We don't support file offsets
              assert(filePath->uri.isLocalPath());   // We're only capable of loading local files
        
              auto fileData = Application::LoadBinaryFile(filePath->uri.path());
        
              return MakeRawImageData(fileData.first.get(), fileData.second, filePath->mimeType, image.name);
            }
        
            if (const auto* vector = std::get_if<fastgltf::sources::Vector>(&image.data))
            {
              return MakeRawImageData(vector->bytes.data(), vector->bytes.size(), vector->mimeType, image.name);
            }
        
            if (const auto* view = std::get_if<fastgltf::sources::BufferView>(&image.data))
            {
              auto& bufferView = asset.bufferViews[view->bufferViewIndex];
              auto& buffer = asset.buffers[bufferView.bufferIndex];
              if (const auto* vector = std::get_if<fastgltf::sources::Vector>(&buffer.data))
              {
                return MakeRawImageData(vector->bytes.data() + bufferView.byteOffset, bufferView.byteLength, view->mimeType, image.name);
              }
            }
            
            return RawImageData{};
          }();
        
          if (rawImage.isKtx)
          {
            ZoneScopedN("Decode KTX 2");
            ktxTexture2* ktx{};
            if (auto result = ktxTexture2_CreateFromMemory(reinterpret_cast<const ktx_uint8_t*>(rawImage.encodedPixelData.get()),
                                                           rawImage.encodedPixelSize,
                                                           KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT,
                                                           &ktx);
                result != KTX_SUCCESS)
            {
              assert(false);
            }
            
            ktx_transcode_fmt_e ktxTranscodeFormat{};
            
            switch (imageUsages[index])
            {
            case ImageUsage::BASE_COLOR:
              rawImage.formatIfKtx = Fvog::Format::BC7_RGBA_UNORM;
              ktxTranscodeFormat = KTX_TTF_BC7_RGBA;
              break;
            // Occlusion and metallicRoughness _may_ be encoded within the same image, so we have to use at least an RGB format here.
            // In the event where these textures are guaranteed to be separate, we can use BC4 and BC5 for better quality.
            case ImageUsage::OCCLUSION: [[fallthrough]];
            case ImageUsage::METALLIC_ROUGHNESS:
              rawImage.formatIfKtx = Fvog::Format::BC7_RGBA_UNORM;
              ktxTranscodeFormat = KTX_TTF_BC7_RGBA;
              break;
            // The glTF spec states that normal textures must be encoded with three channels, even though the third could be trivially reconstructed.
            // libktx is incapable of decoding XYZ normal maps to BC5 as their alpha channel is mapped to BC5's G channel, so we are stuck with this.
            case ImageUsage::NORMAL:
              rawImage.formatIfKtx = Fvog::Format::BC7_RGBA_UNORM;
              ktxTranscodeFormat = KTX_TTF_BC7_RGBA;
              break;
            // TODO: evaluate whether BC7 is necessary here.
            case ImageUsage::EMISSION:
              rawImage.formatIfKtx = Fvog::Format::BC7_RGBA_UNORM;
              ktxTranscodeFormat = KTX_TTF_BC7_RGBA;
              break;
            }

            // If the image needs is in a supercompressed encoding, transcode it to a desired format
            if (ktxTexture2_NeedsTranscoding(ktx))
            {
              ZoneScopedN("Transcode KTX 2 Texture");
              if (auto result = ktxTexture2_TranscodeBasis(ktx, ktxTranscodeFormat, KTX_TF_HIGH_QUALITY); result != KTX_SUCCESS)
              {
                assert(false);
              }
            }
            else
            {
              // Use the format that the image is already in
              //rawImage.formatIfKtx = VkBcFormatToFwog(ktx->vkFormat);
              rawImage.formatIfKtx = Fvog::detail::VkToFormat(static_cast<VkFormat>(ktx->vkFormat));
            }
        
            rawImage.width = ktx->baseWidth;
            rawImage.height = ktx->baseHeight;
            rawImage.components = ktxTexture2_GetNumComponents(ktx);
            rawImage.ktx.reset(ktx);
          }
          else
          {
            ZoneScopedN("Decode JPEG/PNG");
            int x, y, comp;
            auto* pixels = stbi_load_from_memory(reinterpret_cast<const unsigned char*>(rawImage.encodedPixelData.get()),
                                                 static_cast<int>(rawImage.encodedPixelSize),
                                                 &x,
                                                 &y,
                                                 &comp,
                                                 4);
        
            assert(pixels != nullptr);
        
            rawImage.width = x;
            rawImage.height = y;
            // rawImage.components = comp;
            rawImage.components = 4; // If forced 4 components
            rawImage.data.reset(pixels);
          }

          return rawImage;
        });

      struct ImageUploadInfo
      {
        size_t imageIndex;
        uint32_t level;
        Fvog::Extent3D extent;
        const void* data;
        size_t bufferOffset;
        size_t size;
      };
      size_t currentBufferOffset = 0;

      auto imageUploadInfos = std::vector<ImageUploadInfo>();
      imageUploadInfos.reserve(rawImageData.size());

      // Upload image data to GPU
      auto loadedImages = std::vector<Fvog::Texture>();
      loadedImages.reserve(rawImageData.size()); // This .reserve() is critical for iterator stability

      auto imagesToBarrier = std::vector<Fvog::Texture*>();
      imagesToBarrier.reserve(rawImageData.size());

      constexpr size_t BATCH_SIZE = 1'000'000'000;
      auto stagingBuffer = Fvog::Buffer(device, {.size = BATCH_SIZE, .flag = Fvog::BufferFlagThingy::MAP_SEQUENTIAL_WRITE}, "Scene Loader Staging Buffer");

      auto flushImageUploads = [&] {
        ZoneScopedN("Flush Image Uploads");

        // Recreate staging buffer if it's too small
        if (currentBufferOffset + imageUploadInfos.back().size > stagingBuffer.SizeBytes())
        {
          stagingBuffer = Fvog::Buffer(device,
            {.size = VkDeviceSize((currentBufferOffset + imageUploadInfos.back().size) * 1.5), .flag = Fvog::BufferFlagThingy::MAP_SEQUENTIAL_WRITE},
            "Scene Loader Staging Buffer");
        }

        // Fire off copies in one batch
        device.ImmediateSubmit([&](VkCommandBuffer commandBuffer)
        {
          auto ctx = Fvog::Context(device, commandBuffer);
          for (auto* loadedImage : imagesToBarrier)
          {
            ctx.ImageBarrierDiscard(*loadedImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
          }

          {
            ZoneScopedN("Memcpy to buffer");
            std::for_each(std::execution::par,
              imageUploadInfos.begin(),
              imageUploadInfos.end(),
              [&](const ImageUploadInfo& imageUpload)
              { std::memcpy(static_cast<std::byte*>(stagingBuffer.GetMappedMemory()) + imageUpload.bufferOffset, imageUpload.data, imageUpload.size); });
          }

          for (const auto& imageUpload : imageUploadInfos)
          {
            vkCmdCopyBufferToImage2(commandBuffer, Fvog::detail::Address(VkCopyBufferToImageInfo2{
              .sType = VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2,
              .srcBuffer = stagingBuffer.Handle(),
              .dstImage = loadedImages[imageUpload.imageIndex].Image(),
              .dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
              .regionCount = 1,
              .pRegions = Fvog::detail::Address(VkBufferImageCopy2{
                .sType = VK_STRUCTURE_TYPE_BUFFER_IMAGE_COPY_2,
                .bufferOffset = imageUpload.bufferOffset,
                .bufferRowLength = 0,
                .bufferImageHeight = 0,
                .imageSubresource = VkImageSubresourceLayers{
                  .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                  .mipLevel = imageUpload.level,
                  .layerCount = 1,
                },
                .imageExtent = {imageUpload.extent.width, imageUpload.extent.height, imageUpload.extent.depth},
              }),
            }));
          }
        });
      };

      // Create image objects
      for (const auto& image : rawImageData)
      {
        VkExtent2D dims = {static_cast<uint32_t>(image.width), static_cast<uint32_t>(image.height)};
        auto name = image.name.empty() ? "Loaded Material" : image.name;
        constexpr auto usage = Fvog::TextureUsage::READ_ONLY;

        // Upload KTX2 compressed image
        if (image.isKtx)
        {
          ZoneScopedN("Upload BCn Image");
          auto* ktx = image.ktx.get();

          auto textureData = Fvog::CreateTexture2DMip(device, dims, image.formatIfKtx, ktx->numLevels, usage, name);

          for (uint32_t level = 0; level < ktx->numLevels; level++)
          {
            size_t offset{};
            ktxTexture_GetImageOffset(ktxTexture(ktx), level, 0, 0, &offset);

            uint32_t width = std::max(dims.width >> level, 1u);
            uint32_t height = std::max(dims.height >> level, 1u);

            // Update compressed image
            const auto size = ImageToBufferSize(textureData.GetCreateInfo().format, textureData.GetCreateInfo().extent);

            imageUploadInfos.emplace_back(ImageUploadInfo {
              .imageIndex   = loadedImages.size(),
              .level        = level,
              .extent       = {width, height, 1},
              .data         = ktx->pData + offset,
              .bufferOffset = currentBufferOffset,
              .size         = size,
            });

            currentBufferOffset += size;
          }

          loadedImages.emplace_back(std::move(textureData));
        }
        else // Upload raw image data and generate mipmap
        {
          ZoneScopedN("Upload 8-BPP Image");
          assert(image.components == 4);
          assert(image.pixel_type == GL_UNSIGNED_BYTE);
          assert(image.bits == 8);

          // TODO: use R8G8_UNORM for normal maps
          auto textureData = Fvog::CreateTexture2DMip(device,
                                                      dims,
                                                      Fvog::Format::R8G8B8A8_UNORM,
                                                      //uint32_t(1 + floor(log2(glm::max(dims.width, dims.height)))),
                                                      1,
                                                      usage,
                                                      name);

          // Update uncompressed image
          // TODO: generate mipmaps
          //textureData.GenMipmaps();

          const auto size = ImageToBufferSize(textureData.GetCreateInfo().format, textureData.GetCreateInfo().extent);

          imageUploadInfos.emplace_back(ImageUploadInfo{
            .imageIndex   = loadedImages.size(),
            .level        = 0,
            .extent       = {dims.width, dims.height, 1},
            .data         = image.data.get(),
            .bufferOffset = currentBufferOffset,
            .size         = size,
          });

          currentBufferOffset += size;

          loadedImages.emplace_back(std::move(textureData));
        }

        // The most recently-created image needs a barrier.
        imagesToBarrier.emplace_back(&loadedImages.back());

        // Flush upload after batch size is exceeded
        if (currentBufferOffset >= BATCH_SIZE)
        {
          flushImageUploads();

          imageUploadInfos.clear();

          // Reset offset for next batch.
          currentBufferOffset = 0;
        }
      }

      if (!imageUploadInfos.empty())
      {
        flushImageUploads();
      }

      // Transition every loaded image to READ_ONLY
      device.ImmediateSubmit(
        [&](VkCommandBuffer commandBuffer)
        {
          auto ctx = Fvog::Context(device, commandBuffer);
          for (auto& loadedImage : loadedImages)
          {
            ctx.ImageBarrier(loadedImage, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);
          }
        });


      return loadedImages;
    }

    glm::mat4 NodeToMat4(const fastgltf::Node& node)
    {
      glm::mat4 transform{1};

      if (auto* trs = std::get_if<fastgltf::Node::TRS>(&node.transform))
      {
        // Note: do not use glm::make_quat because glm and glTF use different quaternion component layouts (wxyz vs xyzw)!
        auto rotation = glm::quat{trs->rotation[3], trs->rotation[0], trs->rotation[1], trs->rotation[2]};
        const auto scale = glm::make_vec3(trs->scale.data());
        const auto translation = glm::make_vec3(trs->translation.data());

        const glm::mat4 rotationMat = glm::mat4_cast(rotation);

        // T * R * S
        transform = glm::scale(glm::translate(translation) * rotationMat, scale);
      }
      else if (auto* mat = std::get_if<fastgltf::Node::TransformMatrix>(&node.transform))
      {
        transform = glm::make_mat4(mat->data());
      }
      // else node has identity transform

      return transform;
    }
  } // namespace

  std::vector<Render::Vertex> ConvertVertexBufferFormat(const fastgltf::Asset& model,
                                                std::size_t positionAccessorIndex,
                                                std::size_t normalAccessorIndex,
                                                std::optional<std::size_t> texcoordAccessorIndex)
  {
    ZoneScoped;
    std::vector<glm::vec3> positions;
    auto& positionAccessor = model.accessors[positionAccessorIndex];
    positions.resize(positionAccessor.count);
    fastgltf::iterateAccessorWithIndex<glm::vec3>(model, positionAccessor, [&](glm::vec3 position, std::size_t idx) { positions[idx] = position; });

    std::vector<glm::vec3> normals;
    auto& normalAccessor = model.accessors[normalAccessorIndex];
    normals.resize(normalAccessor.count);
    fastgltf::iterateAccessorWithIndex<glm::vec3>(model, normalAccessor, [&](glm::vec3 normal, std::size_t idx) { normals[idx] = normal; });

    std::vector<glm::vec2> texcoords;

    // Textureless meshes will use factors instead of textures
    if (texcoordAccessorIndex.has_value())
    {
      auto& texcoordAccessor = model.accessors[texcoordAccessorIndex.value()];
      texcoords.resize(texcoordAccessor.count);
      fastgltf::iterateAccessorWithIndex<glm::vec2>(model, texcoordAccessor, [&](glm::vec2 texcoord, std::size_t idx) { texcoords[idx] = texcoord; });
    }
    else
    {
      // If no texcoord attribute, fill with empty texcoords to keep everything consistent and happy
      texcoords.resize(positions.size(), {});
    }

    assert(positions.size() == normals.size() && positions.size() == texcoords.size());

    std::vector<Render::Vertex> vertices;
    vertices.resize(positions.size());

    for (size_t i = 0; i < positions.size(); i++)
    {
      vertices[i] = {positions[i], glm::packSnorm2x16(float32x3_to_oct(normals[i])), texcoords[i]};
    }

    return vertices;
  }

  std::vector<Render::index_t> ConvertIndexBufferFormat(const fastgltf::Asset& model, std::size_t indicesAccessorIndex)
  {
    ZoneScoped;
    auto indices   = std::vector<Render::index_t>();
    auto& accessor = model.accessors[indicesAccessorIndex];
    indices.resize(accessor.count);
    fastgltf::iterateAccessorWithIndex<Render::index_t>(model, accessor, [&](Render::index_t index, size_t idx) { indices[idx] = index; });
    return indices;
  }

  std::vector<Render::Material> LoadMaterials([[maybe_unused]] Fvog::Device& device, const fastgltf::Asset& model, std::span<Fvog::Texture> images)
  {
    ZoneScoped;
    auto LoadSampler = [](const fastgltf::Sampler& sampler)
    {
      Fvog::SamplerCreateInfo samplerState{};

      samplerState.addressModeU = ConvertGlAddressMode((GLint)sampler.wrapS);
      samplerState.addressModeV = ConvertGlAddressMode((GLint)sampler.wrapT);
      if (sampler.minFilter.has_value())
      {
        samplerState.minFilter = ConvertGlFilterMode((GLint)sampler.minFilter.value());
        samplerState.mipmapMode = GetGlMipmapFilter((GLint)sampler.minFilter.value());
        samplerState.maxAnisotropy = 16;
        //if (samplerState.minFilter != Fwog::Filter::NONE)
        //{
        //  samplerState.anisotropy = Fwog::SampleCount::SAMPLES_16;
        //}
      }
      if (sampler.magFilter.has_value())
      {
        samplerState.magFilter = ConvertGlFilterMode((GLint)sampler.magFilter.value());
      }

      return samplerState;
    };

    std::vector<Render::Material> materials;

    for (const auto& loaderMaterial : model.materials)
    {
      Render::Material material;

      if (loaderMaterial.occlusionTexture.has_value())
      {
        material.gpuMaterial.flags |= Render::MaterialFlagBit::HAS_OCCLUSION_TEXTURE;
        auto occlusionTextureIndex = loaderMaterial.occlusionTexture->textureIndex;
        const auto& occlusionTexture = model.textures[occlusionTextureIndex];
        auto& image = images[occlusionTexture.imageIndex.value()];
        auto name = occlusionTexture.name.empty() ? "Occlusion" : occlusionTexture.name;
        auto view = image.CreateFormatView(image.GetCreateInfo().format, name.c_str());
        material.occlusionTextureSampler = {
          std::move(view),
          //LoadSampler(model.samplers[occlusionTexture.samplerIndex.value()]),
        };

        material.gpuMaterial.occlusionTextureIndex = material.occlusionTextureSampler->texture.GetSampledResourceHandle().index;
      }

      if (loaderMaterial.emissiveTexture.has_value())
      {
        material.gpuMaterial.flags |= Render::MaterialFlagBit::HAS_EMISSION_TEXTURE;
        auto emissiveTextureIndex = loaderMaterial.emissiveTexture->textureIndex;
        const auto& emissiveTexture = model.textures[emissiveTextureIndex];
        auto& image = images[emissiveTexture.imageIndex.value()];
        auto name = emissiveTexture.name.empty() ? "Emissive" : emissiveTexture.name;
        auto view = image.CreateFormatView(FormatToSrgb(image.GetCreateInfo().format), name.c_str());
        material.emissiveTextureSampler = {
          std::move(view),
          //LoadSampler(model.samplers[emissiveTexture.samplerIndex.value()]),
        };

        material.gpuMaterial.emissionTextureIndex = material.emissiveTextureSampler->texture.GetSampledResourceHandle().index;
      }

      if (loaderMaterial.normalTexture.has_value())
      {
        material.gpuMaterial.flags |= Render::MaterialFlagBit::HAS_NORMAL_TEXTURE;
        auto normalTextureIndex = loaderMaterial.normalTexture->textureIndex;
        const auto& normalTexture = model.textures[normalTextureIndex];
        auto& image = images[normalTexture.imageIndex.value()];
        auto name = normalTexture.name.empty() ? "Normal Map" : normalTexture.name;
        auto view = image.CreateFormatView(image.GetCreateInfo().format, name.c_str());
        material.normalTextureSampler = {
          std::move(view),
          //LoadSampler(model.samplers[normalTexture.samplerIndex.value()]),
        };
        material.gpuMaterial.normalXyScale = loaderMaterial.normalTexture->scale;

        material.gpuMaterial.normalTextureIndex = material.normalTextureSampler->texture.GetSampledResourceHandle().index;
      }
      
      if (loaderMaterial.pbrData.baseColorTexture.has_value())
      {
        material.gpuMaterial.flags |= Render::MaterialFlagBit::HAS_BASE_COLOR_TEXTURE;
        auto baseColorTextureIndex = loaderMaterial.pbrData.baseColorTexture->textureIndex;
        const auto& baseColorTexture = model.textures[baseColorTextureIndex];
        auto& image = images[baseColorTexture.imageIndex.value()];
        auto name = baseColorTexture.name.empty() ? "Base Color" : baseColorTexture.name;
        auto view = image.CreateFormatView(FormatToSrgb(image.GetCreateInfo().format), name.c_str());
        material.albedoTextureSampler = {
          std::move(view),
          //LoadSampler(model.samplers[baseColorTexture.samplerIndex.value()]),
        };
        
        material.gpuMaterial.baseColorTextureIndex = material.albedoTextureSampler->texture.GetSampledResourceHandle().index;
      }

      if (loaderMaterial.pbrData.metallicRoughnessTexture.has_value())
      {
        material.gpuMaterial.flags |= Render::MaterialFlagBit::HAS_METALLIC_ROUGHNESS_TEXTURE;
        auto metallicRoughnessTextureIndex = loaderMaterial.pbrData.metallicRoughnessTexture->textureIndex;
        const auto& metallicRoughnessTexture = model.textures[metallicRoughnessTextureIndex];
        auto& image = images[metallicRoughnessTexture.imageIndex.value()];
        auto name = metallicRoughnessTexture.name.empty() ? "MetallicRoughness" : metallicRoughnessTexture.name;
        auto view = image.CreateFormatView(image.GetCreateInfo().format, name.c_str());
        material.metallicRoughnessTextureSampler = {
          std::move(view),
          //LoadSampler(model.samplers[metallicRoughnessTexture.samplerIndex.value()]),
        };

        material.gpuMaterial.metallicRoughnessTextureIndex = material.metallicRoughnessTextureSampler->texture.GetSampledResourceHandle().index;
      }

      material.gpuMaterial.baseColorFactor = glm::make_vec4(loaderMaterial.pbrData.baseColorFactor.data());
      material.gpuMaterial.metallicFactor = loaderMaterial.pbrData.metallicFactor;
      material.gpuMaterial.roughnessFactor = loaderMaterial.pbrData.roughnessFactor;

      material.gpuMaterial.emissiveFactor = glm::make_vec3(loaderMaterial.emissiveFactor.data());
      material.gpuMaterial.alphaCutoff = loaderMaterial.alphaCutoff;
      material.gpuMaterial.emissiveStrength = loaderMaterial.emissiveStrength.value_or(1.0f);
      materials.emplace_back(std::move(material));
    }

    return materials;
  }

  // Corresponds to a glTF primitive. In other words, it's the mesh data that corresponds to a draw call.
  struct RawMesh
  {
    std::vector<Render::Vertex> vertices;
    std::vector<Render::index_t> indices;
    Render::Box3D boundingBox;
  };

  struct NodeTempData
  {
    struct Indices
    {
      size_t rawMeshIndex;
      size_t materialIndex;
    };
    std::vector<Indices> indices;
  };

  struct LoadModelResult
  {
    std::vector<std::unique_ptr<LoadModelNode>> nodes;
    // This is a semi-hacky way to extend `nodes` without changing the node type, so we can still splice our nodes onto the main scene's
    //std::vector<NodeTempData> tempData;
    std::vector<RawMesh> rawMeshes;
    std::vector<Render::Material> materials;
    std::vector<Fvog::Texture> images;
  };

  std::optional<LoadModelResult> LoadModelFromFileBase(Fvog::Device& device, std::filesystem::path path, glm::mat4 rootTransform, bool skipMaterials)
  {
    ZoneScoped;

    const auto extension = path.extension();
    const auto isText = extension == ".gltf";
    const auto isBinary = extension == ".glb";
    assert(!(isText && isBinary)); // Sanity check

    if (!isText && !isBinary)
    {
      return std::nullopt;
    }

    auto maybeAsset = [&]() -> fastgltf::Expected<fastgltf::Asset>
    {
      ZoneScopedN("Parse glTF");
      using fastgltf::Extensions;
      constexpr auto gltfExtensions = Extensions::KHR_texture_basisu | Extensions::KHR_mesh_quantization | Extensions::EXT_meshopt_compression |
                                      Extensions::KHR_lights_punctual | Extensions::KHR_materials_emissive_strength;
      auto parser = fastgltf::Parser(gltfExtensions);

      auto data = fastgltf::GltfDataBuffer();
      data.loadFromFile(path);

      constexpr auto options = fastgltf::Options::LoadExternalBuffers | fastgltf::Options::LoadExternalImages | fastgltf::Options::LoadGLBBuffers;
      if (isBinary)
      {
        return parser.loadBinaryGLTF(&data, path.parent_path(), options);
      }

      return parser.loadGLTF(&data, path.parent_path(), options);
    }();

    if (auto err = maybeAsset.error(); err != fastgltf::Error::None)
    {
      std::cout << "glTF error: " << static_cast<uint64_t>(err) << '\n';
      return std::nullopt;
    }

    const auto& asset = maybeAsset.get();

    // Let's not deal with glTFs containing multiple scenes right now
    assert(asset.scenes.size() == 1);

    // Load images and boofers
    auto images = std::vector<Fvog::Texture>();
    auto materials = std::vector<Render::Material>();

    LoadModelResult scene;

    if (!skipMaterials)
    {
      images = LoadImages(device, asset);
      materials = LoadMaterials(device, asset, images);
      std::ranges::move(materials, std::back_inserter(scene.materials));
      std::ranges::move(images, std::back_inserter(scene.images));
    }

    struct AccessorIndices
    {
      std::optional<std::size_t> positionsIndex;
      std::optional<std::size_t> normalsIndex;
      std::optional<std::size_t> texcoordsIndex;
      std::optional<std::size_t> indicesIndex;

      auto operator<=>(const AccessorIndices&) const = default;
    };

    auto uniqueAccessorCombinations = std::vector<std::pair<AccessorIndices, std::size_t>>();

    // <node*, global transform>
    struct StackElement
    {
      LoadModelNode* sceneNode{};
      const fastgltf::Node* gltfNode{};
    };
    std::stack<StackElement> nodeStack;

    // Create the root node for this scene
    std::array<float, 16> rootTransformArray{};
    std::copy_n(&rootTransform[0][0], 16, rootTransformArray.data());
    std::array<float, 3> rootScaleArray{};
    std::array<float, 4> rootRotationArray{};
    std::array<float, 3> rootTranslationArray{};
    fastgltf::decomposeTransformMatrix(rootTransformArray, rootScaleArray, rootRotationArray, rootTranslationArray);
    const auto rootTranslation = glm::make_vec3(rootTranslationArray.data());
    const auto rootRotation = glm::quat{rootRotationArray[3], rootRotationArray[0], rootRotationArray[1], rootRotationArray[2]};
    const auto rootScale = glm::make_vec3(rootScaleArray.data());

    LoadModelNode* rootNode = scene.nodes.emplace_back(std::make_unique<LoadModelNode>(path.stem().string(), rootTranslation, rootRotation, rootScale)).get();

    // All nodes referenced in the scene MUST be root nodes
    for (auto nodeIndex : asset.scenes[0].nodeIndices)
    {
      const auto& assetNode = asset.nodes[nodeIndex];
      const auto name = assetNode.name.empty() ? std::string("Node") : std::string(assetNode.name);
      LoadModelNode* sceneNode = scene.nodes.emplace_back(std::make_unique<LoadModelNode>(name, rootTranslation, rootRotation, rootScale)).get();
      rootNode->children.emplace_back(sceneNode);
      nodeStack.emplace(sceneNode, &assetNode);
    }

    while (!nodeStack.empty())
    {
      decltype(nodeStack)::value_type top = nodeStack.top();
      const auto& [node, gltfNode] = top;
      nodeStack.pop();

      const glm::mat4 localTransform = NodeToMat4(*gltfNode);

      std::array<float, 16> localTransformArray{};
      std::copy_n(&localTransform[0][0], 16, localTransformArray.data());
      std::array<float, 3> scaleArray{};
      std::array<float, 4> rotationArray{};
      std::array<float, 3> translationArray{};
      fastgltf::decomposeTransformMatrix(localTransformArray, scaleArray, rotationArray, translationArray);

      node->translation = glm::make_vec3(translationArray.data());
      node->rotation = {rotationArray[3], rotationArray[0], rotationArray[1], rotationArray[2]};
      node->scale = glm::make_vec3(scaleArray.data());

      for (auto childNodeIndex : gltfNode->children)
      {
        const auto& assetNode = asset.nodes[childNodeIndex];
        const auto name = assetNode.name.empty() ? std::string("Node") : std::string(assetNode.name);
        auto& childSceneNode   = scene.nodes.emplace_back(std::make_unique<LoadModelNode>(name));
        node->children.emplace_back(childSceneNode.get());
        nodeStack.emplace(childSceneNode.get(), &assetNode);
      }

      if (gltfNode->meshIndex.has_value())
      {
        // Load each primitive in the mesh
        for (const fastgltf::Mesh& mesh = asset.meshes[gltfNode->meshIndex.value()]; const auto& primitive : mesh.primitives)
        {
          AccessorIndices accessorIndices;
          if (auto it = primitive.findAttribute("POSITION"); it != primitive.attributes.end())
          {
            accessorIndices.positionsIndex = it->second;
          }
          else
          {
            //FWOG_UNREACHABLE;
            assert(false);
          }

          if (auto it = primitive.findAttribute("NORMAL"); it != primitive.attributes.end())
          {
            accessorIndices.normalsIndex = it->second;
          }
          else
          {
            // TODO: calculate normal
            //FWOG_UNREACHABLE;
            assert(false);
          }

          if (auto it = primitive.findAttribute("TEXCOORD_0"); it != primitive.attributes.end())
          {
            accessorIndices.texcoordsIndex = it->second;
          }
          else
          {
            // Okay, texcoord can be safely missing
          }

          assert(primitive.indicesAccessor.has_value() && "Non-indexed meshes are not supported");
          accessorIndices.indicesIndex = primitive.indicesAccessor;

          size_t rawMeshIndex = uniqueAccessorCombinations.size();

          // Only emplace and increment counter if combo does not exist
          if (auto it = std::ranges::find_if(uniqueAccessorCombinations, [&](const auto& p) { return p.first == accessorIndices; }); it == uniqueAccessorCombinations.end())
          {
            uniqueAccessorCombinations.emplace_back(accessorIndices, rawMeshIndex);
          }
          else
          {
            // Combo already exists
            rawMeshIndex = it->second;
          }

          auto materialId = primitive.materialIndex.has_value() ? uint32_t(primitive.materialIndex.value()) : 0;

          if (skipMaterials)
          {
            materialId = 0;
          }

          node->meshes.emplace_back(rawMeshIndex, materialId);
        }
      }

      // Deduplicating lights is not a concern (they are small and quick to decode), so we load them here for convenience.
      if (gltfNode->lightIndex.has_value())
      {
        const auto& light = asset.lights[*gltfNode->lightIndex];

        Render::GpuLight gpuLight{};

        if (light.type == fastgltf::LightType::Directional)
        {
          gpuLight.type = Render::LightType::DIRECTIONAL;
        }
        else if (light.type == fastgltf::LightType::Spot)
        {
          gpuLight.type = Render::LightType::SPOT;
        }
        else
        {
          gpuLight.type = Render::LightType::POINT;
        }

        gpuLight.color = glm::make_vec3(light.color.data());
        gpuLight.intensity = light.intensity;
        // If not present, range is infinite
        gpuLight.range = light.range.value_or(std::numeric_limits<float>::infinity());
        gpuLight.innerConeAngle = light.innerConeAngle.value_or(0);
        gpuLight.outerConeAngle = light.outerConeAngle.value_or(0);

        node->light = gpuLight;
      }
    }

    scene.rawMeshes.resize(uniqueAccessorCombinations.size());
    
    std::transform(
      std::execution::par,
      uniqueAccessorCombinations.begin(),
      uniqueAccessorCombinations.end(),
      scene.rawMeshes.begin(),
      [&](const auto& keyValue) -> RawMesh
      {
        ZoneScopedN("Convert vertices and indices");
        const auto& [accessorIndices, _] = keyValue;
        auto vertices = ConvertVertexBufferFormat(asset, accessorIndices.positionsIndex.value(), accessorIndices.normalsIndex.value(), accessorIndices.texcoordsIndex);
        auto indices = ConvertIndexBufferFormat(asset, accessorIndices.indicesIndex.value());

        const auto& positionAccessor = asset.accessors[accessorIndices.positionsIndex.value()];

        glm::vec3 bboxMin{};
        if (auto* dv = std::get_if<std::pmr::vector<double>>(&positionAccessor.min))
        {
          bboxMin = {(*dv)[0], (*dv)[1], (*dv)[2]};
        }
        if (auto* iv = std::get_if<std::pmr::vector<int64_t>>(&positionAccessor.min))
        {
          bboxMin = {(*iv)[0], (*iv)[1], (*iv)[2]};
        }

        glm::vec3 bboxMax{};
        if (auto* dv = std::get_if<std::pmr::vector<double>>(&positionAccessor.max))
        {
          bboxMax = {(*dv)[0], (*dv)[1], (*dv)[2]};
        }
        if (auto* iv = std::get_if<std::pmr::vector<int64_t>>(&positionAccessor.max))
        {
          bboxMax = {(*iv)[0], (*iv)[1], (*iv)[2]};
        }

        return RawMesh{
          .vertices = std::move(vertices),
          .indices = std::move(indices),
          .boundingBox = {.min = bboxMin, .max = bboxMax},
        };
      });

    std::cout << "Loaded glTF: " << path << '\n';

    return scene;
  }

  LoadModelResultA LoadModelFromFileMeshlet(Fvog::Device& device, const std::filesystem::path& fileName, glm::mat4 rootTransform, bool skipMaterials)
  {
    ZoneScoped;
    ZoneText(fileName.string().c_str(), fileName.string().size());

    auto result = LoadModelResultA{};

    // First material is always default.
    result.materials.emplace_back(Render::GpuMaterial{
      .metallicFactor  = 0,
      .baseColorFactor = {0.5f, 0.5f, 0.5f, 0.5f},
    });

    auto loadedScene = LoadModelFromFileBase(device, fileName, rootTransform, skipMaterials);

    // Give each mesh a set of "default" meshlet instances.
    auto meshletInstances     = std::vector<Render::MeshletInstance>(loadedScene->rawMeshes.size());
    auto loadModelResult      = LoadModelResultA{};
    loadModelResult.materials = std::move(loadedScene->materials);
    loadModelResult.images    = std::move(loadedScene->images);
    loadModelResult.meshGeometries.resize(loadedScene->rawMeshes.size());

    auto meshIndices = std::vector<size_t>(loadedScene->rawMeshes.size());
    std::iota(meshIndices.begin(), meshIndices.end(), 0);

    std::for_each(
      std::execution::par,
      meshIndices.begin(),
      meshIndices.end(),
      [&] (size_t meshIdx) -> void
      {
        ZoneScopedN("Create meshlets for mesh");
        auto& mesh = loadedScene->rawMeshes[meshIdx];

        const auto maxMeshlets = meshopt_buildMeshletsBound(mesh.indices.size(), maxMeshletIndices, maxMeshletPrimitives);

        //MeshletIntermediateInfo meshletInfo;
        //auto& [rawMeshPtr, vertices, meshletIndices, meshletPrimitives, rawMeshlets] = meshletInfo;

        auto meshGeometry = MeshGeometry{};

        auto rawMeshlets = std::vector<meshopt_Meshlet>(maxMeshlets);
        
        meshGeometry.vertices = std::move(mesh.vertices);
        meshGeometry.indices.resize(maxMeshlets * maxMeshletIndices);
        meshGeometry.primitives.resize(maxMeshlets * maxMeshletPrimitives * 3);
        
        const auto meshletCount = [&]
        {
          ZoneScopedN("Build Meshlets");
          return meshopt_buildMeshlets(rawMeshlets.data(),
            meshGeometry.indices.data(),
            meshGeometry.primitives.data(),
            mesh.indices.data(),
            mesh.indices.size(),
            reinterpret_cast<const float*>(meshGeometry.vertices.data()),
            meshGeometry.vertices.size(),
            sizeof(Render::Vertex),
            maxMeshletIndices,
            maxMeshletPrimitives,
            meshletConeWeight);

          // Faster, but generates less efficient meshlets
          //{
          //  ZoneScopedN("Optimize Vertex Cache");
          //  meshopt_optimizeVertexCache(mesh.indices.data(), mesh.indices.data(), mesh.indices.size(), mesh.vertices.size());
          //}
          //return meshopt_buildMeshletsScan(rawMeshlets.data(),
          //  meshGeometry.indices.data(),
          //  meshGeometry.primitives.data(),
          //  mesh.indices.data(),
          //  mesh.indices.size(),
          //  meshGeometry.vertices.size(),
          //  maxMeshletIndices,
          //  maxMeshletPrimitives);
        }();

        // TODO: replace with rawMeshlets.back() AFTER moving rawMeshlets.resize() before this
        const auto& lastMeshlet = rawMeshlets[meshletCount - 1];
        meshGeometry.indices.resize(lastMeshlet.vertex_offset + lastMeshlet.vertex_count);
        meshGeometry.primitives.resize(lastMeshlet.triangle_offset + ((lastMeshlet.triangle_count * 3 + 3) & ~3));
        rawMeshlets.resize(meshletCount);
        meshGeometry.meshlets.reserve(meshletCount);

        for (const auto& meshlet : rawMeshlets)
        {
          auto min = glm::vec3(std::numeric_limits<float>::max());
          auto max = glm::vec3(std::numeric_limits<float>::lowest());
          for (uint32_t i = 0; i < meshlet.triangle_count * 3; ++i)
          {
            const auto& vertex = meshGeometry.vertices[meshGeometry.indices[meshlet.vertex_offset + meshGeometry.primitives[meshlet.triangle_offset + i]]];
            min                = glm::min(min, vertex.position);
            max                = glm::max(max, vertex.position);
          }
          
          meshGeometry.meshlets.emplace_back(Render::Meshlet{
            .vertexOffset    = 0,
            .indexOffset     = meshlet.vertex_offset,
            .primitiveOffset = meshlet.triangle_offset,
            .indexCount      = meshlet.vertex_count,
            .primitiveCount  = meshlet.triangle_count,
            .aabbMin         = {min.x, min.y, min.z},
            .aabbMax         = {max.x, max.y, max.z},
          });
        }

        loadModelResult.meshGeometries[meshIdx] = meshGeometry;
      });
    
    loadModelResult.rootNodes.emplace_back(loadedScene->nodes.front().get());
    //std::ranges::move(loadedScene->nodes, std::back_inserter(loadModelResult.nodes));
    loadModelResult.nodes = std::move(loadedScene->nodes);

    return loadModelResult;
  }

  glm::mat4 LoadModelNode::CalcLocalTransform() const noexcept
  {
    return glm::scale(glm::translate(translation) * glm::mat4_cast(rotation), scale);
  }
} // namespace Utility

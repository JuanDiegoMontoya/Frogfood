#include "SceneLoader.h"
#include "Application.h"

#include <tracy/Tracy.hpp>

#include <algorithm>
#include <chrono>
#include <execution>
#include <iostream>
#include <numeric>
#include <optional>
#include <ranges>
#include <span>
#include <stack>
#include <utility>

#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/transform.hpp>

#include "ktx.h"

#include FWOG_OPENGL_HEADER

#include <Fwog/Context.h>

// #include <glm/gtx/string_cast.hpp>

#include <stb_image.h>

#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/parser.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/types.hpp>

#include <meshoptimizer.h>

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
    Fwog::Format VkBcFormatToFwog(uint32_t vkFormat)
    {
      // https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkFormat.html
      switch (vkFormat)
      {
      case 131: return Fwog::Format::BC1_RGB_UNORM;
      case 132: return Fwog::Format::BC1_RGB_SRGB;
      case 133: return Fwog::Format::BC1_RGBA_UNORM;
      case 134: return Fwog::Format::BC1_RGBA_SRGB;
      case 135: return Fwog::Format::BC2_RGBA_UNORM;
      case 136: return Fwog::Format::BC2_RGBA_SRGB;
      case 137: return Fwog::Format::BC3_RGBA_UNORM;
      case 138: return Fwog::Format::BC3_RGBA_SRGB;
      case 139: return Fwog::Format::BC4_R_UNORM;
      case 140: return Fwog::Format::BC4_R_SNORM;
      case 141: return Fwog::Format::BC5_RG_UNORM;
      case 142: return Fwog::Format::BC5_RG_SNORM;
      case 143: return Fwog::Format::BC6H_RGB_UFLOAT;
      case 144: return Fwog::Format::BC6H_RGB_SFLOAT;
      case 145: return Fwog::Format::BC7_RGBA_UNORM;
      case 146: return Fwog::Format::BC7_RGBA_SRGB;
      default: FWOG_UNREACHABLE; return {};
      }
    }

    // Converts a format to the sRGB version of itself, for use in a texture view
    Fwog::Format FormatToSrgb(Fwog::Format format)
    {
      switch (format)
      {
      case Fwog::Format::BC1_RGBA_UNORM: return Fwog::Format::BC1_RGBA_SRGB;
      case Fwog::Format::BC1_RGB_UNORM: return Fwog::Format::BC1_RGB_SRGB;
      case Fwog::Format::BC2_RGBA_UNORM: return Fwog::Format::BC2_RGBA_SRGB;
      case Fwog::Format::BC3_RGBA_UNORM: return Fwog::Format::BC3_RGBA_SRGB;
      case Fwog::Format::BC7_RGBA_UNORM: return Fwog::Format::BC7_RGBA_SRGB;
      case Fwog::Format::R8G8B8A8_UNORM: return Fwog::Format::R8G8B8A8_SRGB;
      case Fwog::Format::R8G8B8_UNORM: return Fwog::Format::R8G8B8_SRGB;
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

    auto ConvertGlAddressMode(uint32_t wrap) -> Fwog::AddressMode
    {
      switch (wrap)
      {
      case GL_CLAMP_TO_EDGE: return Fwog::AddressMode::CLAMP_TO_EDGE;
      case GL_MIRRORED_REPEAT: return Fwog::AddressMode::MIRRORED_REPEAT;
      case GL_REPEAT: return Fwog::AddressMode::REPEAT;
      default: FWOG_UNREACHABLE; return Fwog::AddressMode::REPEAT;
      }
    }

    auto ConvertGlFilterMode(uint32_t filter) -> Fwog::Filter
    {
      switch (filter)
      {
      case GL_LINEAR_MIPMAP_LINEAR:  //[[fallthrough]]
      case GL_LINEAR_MIPMAP_NEAREST: //[[fallthrough]]
      case GL_LINEAR: return Fwog::Filter::LINEAR;
      case GL_NEAREST_MIPMAP_LINEAR:  //[[fallthrough]]
      case GL_NEAREST_MIPMAP_NEAREST: //[[fallthrough]]
      case GL_NEAREST: return Fwog::Filter::NEAREST;
      default: FWOG_UNREACHABLE; return Fwog::Filter::LINEAR;
      }
    }

    auto GetGlMipmapFilter(uint32_t minFilter) -> Fwog::Filter
    {
      switch (minFilter)
      {
      case GL_LINEAR_MIPMAP_LINEAR: //[[fallthrough]]
      case GL_NEAREST_MIPMAP_LINEAR: return Fwog::Filter::LINEAR;
      case GL_LINEAR_MIPMAP_NEAREST: //[[fallthrough]]
      case GL_NEAREST_MIPMAP_NEAREST: return Fwog::Filter::NEAREST;
      case GL_LINEAR: //[[fallthrough]]
      case GL_NEAREST: return Fwog::Filter::NONE;
      default: FWOG_UNREACHABLE; return Fwog::Filter::NONE;
      }
    }

    std::vector<Fwog::Texture> LoadImages(const fastgltf::Asset& asset, std::span<const ImageUsage> imageUsages)
    {
      ZoneScoped;
      struct RawImageData
      {
        // Used for ktx and non-ktx images alike
        std::unique_ptr<std::byte[]> encodedPixelData = {};
        std::size_t encodedPixelSize = 0;

        bool isKtx = false;
        Fwog::Format formatIfKtx;
        int width = 0;
        int height = 0;
        int pixel_type = GL_UNSIGNED_BYTE;
        int bits = 8;
        int components = 0;
        std::string name;

        // Non-ktx. Raw decoded pixel data
        std::unique_ptr<unsigned char[]> data = {};

        // ktx
        std::unique_ptr<ktxTexture2, decltype([](ktxTexture2* p) { ktxTexture_Destroy(ktxTexture(p)); })> ktx = {};
      };
      
      auto MakeRawImageData = [](const void* data, std::size_t dataSize, fastgltf::MimeType mimeType, std::string_view name) -> RawImageData
      {
        FWOG_ASSERT(mimeType == fastgltf::MimeType::JPEG || mimeType == fastgltf::MimeType::PNG || mimeType == fastgltf::MimeType::KTX2);
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
          ZoneName(image.name.c_str(), image.name.size());

          auto rawImage = [&]
          {
            if (const auto* filePath = std::get_if<fastgltf::sources::URI>(&image.data))
            {
              FWOG_ASSERT(filePath->fileByteOffset == 0); // We don't support file offsets
              FWOG_ASSERT(filePath->uri.isLocalPath());   // We're only capable of loading local files
        
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
              FWOG_UNREACHABLE;
            }
            
            ktx_transcode_fmt_e ktxTranscodeFormat{};
            
            switch (imageUsages[index])
            {
            case ImageUsage::BASE_COLOR:
              rawImage.formatIfKtx = Fwog::Format::BC7_RGBA_UNORM;
              ktxTranscodeFormat = KTX_TTF_BC7_RGBA;
              break;
            // Occlusion and metallicRoughness _may_ be encoded within the same image, so we have to use at least an RGB format here.
            // In the event where these textures are guaranteed to be separate, we can use BC4 and BC5 for better quality.
            case ImageUsage::OCCLUSION: [[fallthrough]];
            case ImageUsage::METALLIC_ROUGHNESS:
              rawImage.formatIfKtx = Fwog::Format::BC7_RGBA_UNORM;
              ktxTranscodeFormat = KTX_TTF_BC7_RGBA;
              break;
            // The glTF spec states that normal textures must be encoded with three channels, even though the third could be trivially reconstructed.
            // libktx is incapable of decoding XYZ normal maps to BC5 as their alpha channel is mapped to BC5's G channel, so we are stuck with this.
            case ImageUsage::NORMAL:
              rawImage.formatIfKtx = Fwog::Format::BC7_RGBA_UNORM;
              ktxTranscodeFormat = KTX_TTF_BC7_RGBA;
              break;
            // TODO: evaluate whether BC7 is necessary here.
            case ImageUsage::EMISSION:
              rawImage.formatIfKtx = Fwog::Format::BC7_RGBA_UNORM;
              ktxTranscodeFormat = KTX_TTF_BC7_RGBA;
              break;
            }

            // If the image needs is in a supercompressed encoding, transcode it to a desired format
            if (ktxTexture2_NeedsTranscoding(ktx))
            {
              ZoneScopedN("Transcode KTX 2 Texture");
              if (auto result = ktxTexture2_TranscodeBasis(ktx, ktxTranscodeFormat, KTX_TF_HIGH_QUALITY); result != KTX_SUCCESS)
              {
                FWOG_UNREACHABLE;
              }
            }
            else
            {
              // Use the format that the image is already in
              rawImage.formatIfKtx = VkBcFormatToFwog(ktx->vkFormat);
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
        
            FWOG_ASSERT(pixels != nullptr);
        
            rawImage.width = x;
            rawImage.height = y;
            // rawImage.components = comp;
            rawImage.components = 4; // If forced 4 components
            rawImage.data.reset(pixels);
          }

          return rawImage;
        });

      // Upload image data to GPU
      auto loadedImages = std::vector<Fwog::Texture>();
      loadedImages.reserve(rawImageData.size());

      for (const auto& image : rawImageData)
      {
        Fwog::Extent2D dims = {static_cast<uint32_t>(image.width), static_cast<uint32_t>(image.height)};

        // Upload KTX2 compressed image
        if (image.isKtx)
        {
          ZoneScopedN("Upload BCn Image");
          auto* ktx = image.ktx.get();

          auto textureData = Fwog::CreateTexture2DMip(dims, image.formatIfKtx, ktx->numLevels, image.name);

          for (uint32_t level = 0; level < ktx->numLevels; level++)
          {
            size_t offset{};
            ktxTexture_GetImageOffset(ktxTexture(ktx), level, 0, 0, &offset);

            uint32_t width = std::max(dims.width >> level, 1u);
            uint32_t height = std::max(dims.height >> level, 1u);

            textureData.UpdateCompressedImage({
              .level = level,
              .extent = {width, height, 1},
              .data = ktx->pData + offset,
            });
          }

          loadedImages.emplace_back(std::move(textureData));
        }
        else // Upload raw image data and generate mipmap
        {
          ZoneScopedN("Upload 8-BPP Image");
          FWOG_ASSERT(image.components == 4);
          FWOG_ASSERT(image.pixel_type == GL_UNSIGNED_BYTE);
          FWOG_ASSERT(image.bits == 8);

          // TODO: use R8G8_UNORM for normal maps
          auto textureData = Fwog::CreateTexture2DMip(dims, Fwog::Format::R8G8B8A8_UNORM, uint32_t(1 + floor(log2(glm::max(dims.width, dims.height)))), image.name);

          auto updateInfo = Fwog::TextureUpdateInfo{
            .level = 0,
            .offset = {},
            .extent = {dims.width, dims.height, 1},
            .format = Fwog::UploadFormat::RGBA,
            .type = Fwog::UploadType::UBYTE,
            .pixels = image.data.get(),
          };
          textureData.UpdateImage(updateInfo);
          textureData.GenMipmaps();

          loadedImages.emplace_back(std::move(textureData));
        }
      }

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

  std::vector<Vertex> ConvertVertexBufferFormat(const fastgltf::Asset& model,
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

    FWOG_ASSERT(positions.size() == normals.size() && positions.size() == texcoords.size());

    std::vector<Vertex> vertices;
    vertices.resize(positions.size());

    for (size_t i = 0; i < positions.size(); i++)
    {
      vertices[i] = {positions[i], glm::packSnorm2x16(float32x3_to_oct(normals[i])), texcoords[i]};
    }

    return vertices;
  }

  std::vector<index_t> ConvertIndexBufferFormat(const fastgltf::Asset& model, std::size_t indicesAccessorIndex)
  {
    ZoneScoped;
    auto indices = std::vector<index_t>();
    auto& accessor = model.accessors[indicesAccessorIndex];
    indices.resize(accessor.count);
    fastgltf::iterateAccessorWithIndex<index_t>(model, accessor, [&](index_t index, size_t idx) { indices[idx] = index; });
    return indices;
  }

  std::vector<Material> LoadMaterials(const fastgltf::Asset& model, std::span<Fwog::Texture> images)
  {
    ZoneScoped;
    auto LoadSampler = [](const fastgltf::Sampler& sampler)
    {
      Fwog::SamplerState samplerState{};

      samplerState.addressModeU = ConvertGlAddressMode((GLint)sampler.wrapS);
      samplerState.addressModeV = ConvertGlAddressMode((GLint)sampler.wrapT);
      if (sampler.minFilter.has_value())
      {
        samplerState.minFilter = ConvertGlFilterMode((GLint)sampler.minFilter.value());
        samplerState.mipmapFilter = GetGlMipmapFilter((GLint)sampler.minFilter.value());

        if (samplerState.minFilter != Fwog::Filter::NONE)
        {
          samplerState.anisotropy = Fwog::SampleCount::SAMPLES_16;
        }
      }
      if (sampler.magFilter.has_value())
      {
        samplerState.magFilter = ConvertGlFilterMode((GLint)sampler.magFilter.value());
      }

      return samplerState;
    };

    std::vector<Material> materials;

    for (const auto& loaderMaterial : model.materials)
    {
      Material material;

      if (loaderMaterial.occlusionTexture.has_value())
      {
        material.gpuMaterial.flags |= MaterialFlagBit::HAS_OCCLUSION_TEXTURE;
        auto occlusionTextureIndex = loaderMaterial.occlusionTexture->textureIndex;
        const auto& occlusionTexture = model.textures[occlusionTextureIndex];
        auto& image = images[occlusionTexture.imageIndex.value()];
        material.occlusionTextureSampler = {
          image.CreateFormatView(image.GetCreateInfo().format),
          LoadSampler(model.samplers[occlusionTexture.samplerIndex.value()]),
        };
      }

      if (loaderMaterial.emissiveTexture.has_value())
      {
        material.gpuMaterial.flags |= MaterialFlagBit::HAS_EMISSION_TEXTURE;
        auto emissiveTextureIndex = loaderMaterial.emissiveTexture->textureIndex;
        const auto& emissiveTexture = model.textures[emissiveTextureIndex];
        auto& image = images[emissiveTexture.imageIndex.value()];
        material.emissiveTextureSampler = {
          image.CreateFormatView(FormatToSrgb(image.GetCreateInfo().format)),
          LoadSampler(model.samplers[emissiveTexture.samplerIndex.value()]),
        };
      }

      if (loaderMaterial.normalTexture.has_value())
      {
        material.gpuMaterial.flags |= MaterialFlagBit::HAS_NORMAL_TEXTURE;
        auto normalTextureIndex = loaderMaterial.normalTexture->textureIndex;
        const auto& normalTexture = model.textures[normalTextureIndex];
        auto& image = images[normalTexture.imageIndex.value()];
        material.normalTextureSampler = {
          image.CreateFormatView(image.GetCreateInfo().format),
          LoadSampler(model.samplers[normalTexture.samplerIndex.value()]),
        };
        material.gpuMaterial.normalXyScale = loaderMaterial.normalTexture->scale;
      }
      
      if (loaderMaterial.pbrData.baseColorTexture.has_value())
      {
        material.gpuMaterial.flags |= MaterialFlagBit::HAS_BASE_COLOR_TEXTURE;
        auto baseColorTextureIndex = loaderMaterial.pbrData.baseColorTexture->textureIndex;
        const auto& baseColorTexture = model.textures[baseColorTextureIndex];
        auto& image = images[baseColorTexture.imageIndex.value()];
        material.albedoTextureSampler = {
          image.CreateFormatView(FormatToSrgb(image.GetCreateInfo().format)),
          LoadSampler(model.samplers[baseColorTexture.samplerIndex.value()]),
        };

        if (Fwog::GetDeviceProperties().features.bindlessTextures)
        {
          material.gpuMaterial.baseColorTextureHandle = material.albedoTextureSampler->texture.GetBindlessHandle(Fwog::Sampler(material.albedoTextureSampler->sampler));
        }
        else
        {
          material.gpuMaterial.baseColorTextureHandle = 0;
        }
      }

      if (loaderMaterial.pbrData.metallicRoughnessTexture.has_value())
      {
        material.gpuMaterial.flags |= MaterialFlagBit::HAS_METALLIC_ROUGHNESS_TEXTURE;
        auto metallicRoughnessTextureIndex = loaderMaterial.pbrData.metallicRoughnessTexture->textureIndex;
        const auto& metallicRoughnessTexture = model.textures[metallicRoughnessTextureIndex];
        auto& image = images[metallicRoughnessTexture.imageIndex.value()];
        material.metallicRoughnessTextureSampler = {
          image.CreateFormatView(image.GetCreateInfo().format),
          LoadSampler(model.samplers[metallicRoughnessTexture.samplerIndex.value()]),
        };
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

  struct RawMesh
  {
    std::vector<Vertex> vertices;
    std::vector<index_t> indices;
    Box3D boundingBox;
  };

  struct MeshInstance
  {
    size_t rawMeshIndex;
    uint32_t materialIdx;
    glm::mat4 transform;
  };

  struct LoadModelResult
  {
    std::vector<RawMesh> rawMeshes;
    std::vector<MeshInstance> meshInstances;

    std::vector<Material> materials;
    std::vector<GpuLight> lights;
  };

  std::optional<LoadModelResult> LoadModelFromFileBase(std::filesystem::path path, glm::mat4 rootTransform, bool binary, uint32_t baseMaterialIndex)
  {
    ZoneScoped;

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
      if (binary)
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

    auto& asset = maybeAsset.get();

    // Let's not deal with glTFs containing multiple scenes right now
    FWOG_ASSERT(asset.scenes.size() == 1);

    auto imageFormats = std::vector<ImageUsage>(asset.images.size(), ImageUsage::BASE_COLOR);

    // Determine how each image is used so we can transcode to the proper format.
    // Assumption: each image has exactly one usage, or is used for both metallic-roughness AND occlusion (which is handled in LoadImages()).
    for (const auto& material : asset.materials)
    {
      if (material.pbrData.baseColorTexture && asset.textures[material.pbrData.baseColorTexture->textureIndex].imageIndex)
      {
        imageFormats[*asset.textures[material.pbrData.baseColorTexture->textureIndex].imageIndex] = ImageUsage::BASE_COLOR;
      }
      if (material.normalTexture && asset.textures[material.normalTexture->textureIndex].imageIndex)
      {
        imageFormats[*asset.textures[material.normalTexture->textureIndex].imageIndex] = ImageUsage::NORMAL;
      }
      if (material.pbrData.metallicRoughnessTexture && asset.textures[material.pbrData.metallicRoughnessTexture->textureIndex].imageIndex)
      {
        imageFormats[*asset.textures[material.pbrData.metallicRoughnessTexture->textureIndex].imageIndex] = ImageUsage::METALLIC_ROUGHNESS;
      }
      if (material.occlusionTexture && asset.textures[material.occlusionTexture->textureIndex].imageIndex)
      {
        imageFormats[*asset.textures[material.occlusionTexture->textureIndex].imageIndex] = ImageUsage::OCCLUSION;
      }
      if (material.emissiveTexture && asset.textures[material.emissiveTexture->textureIndex].imageIndex)
      {
        imageFormats[*asset.textures[material.emissiveTexture->textureIndex].imageIndex] = ImageUsage::EMISSION;
      }
    }

    // Load images and boofers
    auto images = LoadImages(asset, imageFormats);

    LoadModelResult scene;

    auto materials = LoadMaterials(asset, images);
    std::ranges::move(materials, std::back_inserter(scene.materials));

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
    std::stack<std::pair<const fastgltf::Node*, glm::mat4>> nodeStack;

    for (auto nodeIndex : asset.scenes[0].nodeIndices)
    {
      nodeStack.emplace(&asset.nodes[nodeIndex], rootTransform);
    }

    while (!nodeStack.empty())
    {
      decltype(nodeStack)::value_type top = nodeStack.top();
      const auto& [node, parentGlobalTransform] = top;
      nodeStack.pop();

      // std::cout << "Node: " << node->name << '\n';

      const glm::mat4 localTransform = NodeToMat4(*node);
      const glm::mat4 globalTransform = parentGlobalTransform * localTransform;

      for (auto childNodeIndex : node->children)
      {
        nodeStack.emplace(&asset.nodes[childNodeIndex], globalTransform);
      }

      if (node->meshIndex.has_value())
      {
        // TODO: get a reference to the mesh instead of loading it from scratch
        for (const fastgltf::Mesh& mesh = asset.meshes[node->meshIndex.value()]; const auto& primitive : mesh.primitives)
        {
          AccessorIndices accessorIndices;
          if (auto it = primitive.findAttribute("POSITION"); it != primitive.attributes.end())
          {
            accessorIndices.positionsIndex = it->second;
          }
          else
          {
            FWOG_UNREACHABLE;
          }

          if (auto it = primitive.findAttribute("NORMAL"); it != primitive.attributes.end())
          {
            accessorIndices.normalsIndex = it->second;
          }
          else
          {
            // TODO: calculate normal
            FWOG_UNREACHABLE;
          }

          if (auto it = primitive.findAttribute("TEXCOORD_0"); it != primitive.attributes.end())
          {
            accessorIndices.texcoordsIndex = it->second;
          }
          else
          {
            // Okay, texcoord can be safely missing
          }

          FWOG_ASSERT(primitive.indicesAccessor.has_value() && "Non-indexed meshes are not supported");
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

          scene.meshInstances.emplace_back(MeshInstance{
            .rawMeshIndex = rawMeshIndex,
            .materialIdx = primitive.materialIndex.has_value() ? baseMaterialIndex + uint32_t(primitive.materialIndex.value()) : 0,
            .transform = globalTransform,
          });
        }
      }

      // Deduplicating lights is not a concern (they are small and quick to decode), so we load them here for convenience.
      if (node->lightIndex.has_value())
      {
        const auto& light = asset.lights[*node->lightIndex];

        GpuLight gpuLight{};

        if (light.type == fastgltf::LightType::Directional)
        {
          gpuLight.type = LightType::DIRECTIONAL;
        }
        else if (light.type == fastgltf::LightType::Spot)
        {
          gpuLight.type = LightType::SPOT;
        }
        else
        {
          gpuLight.type = LightType::POINT;
        }
        
        std::array<float, 16> globalTransformArray{};
        std::copy_n(&globalTransform[0][0], 16, globalTransformArray.data());
        std::array<float, 3> scaleArray{};
        std::array<float, 4> rotationArray{};
        std::array<float, 3> translationArray{};
        fastgltf::decomposeTransformMatrix(globalTransformArray, scaleArray, rotationArray, translationArray);

        glm::quat rotation = {rotationArray[3], rotationArray[0], rotationArray[1], rotationArray[2]};
        glm::vec3 translation = glm::make_vec3(translationArray.data());

        gpuLight.color = glm::make_vec3(light.color.data());
        // We rotate (0, 0, -1) because that is the default, un-rotated direction of spot and directional lights according to the glTF spec
        gpuLight.direction = glm::normalize(rotation) * glm::vec3(0, 0, -1);
        gpuLight.intensity = light.intensity;
        gpuLight.position = translation;
        // If not present, range is infinite
        gpuLight.range = light.range.value_or(std::numeric_limits<float>::infinity());
        gpuLight.innerConeAngle = light.innerConeAngle.value_or(0);
        gpuLight.outerConeAngle = light.outerConeAngle.value_or(0);

        scene.lights.push_back(gpuLight);
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

  bool LoadModelFromFileMeshlet(SceneMeshlet& scene, const std::filesystem::path& fileName, glm::mat4 rootTransform, bool binary)
  {
    ZoneScoped;
    ZoneText(fileName.string().c_str(), fileName.string().size());
    // If the scene has no materials, give it a default material
    if (scene.materials.empty())
    {
      scene.materials.emplace_back(GpuMaterial{
        .metallicFactor = 0,
      });
    }

    const auto baseMaterialIndex = static_cast<uint32_t>(scene.materials.size());
    const auto baseVertexOffset = scene.vertices.size();
    const auto baseIndexOffset = scene.indices.size();
    const auto basePrimitiveOffset = scene.primitives.size();
    const auto baseInstanceId = static_cast<uint32_t>(scene.transforms.size());

    auto loadedScene = LoadModelFromFileBase(fileName, rootTransform, binary, baseMaterialIndex);
    if (!loadedScene)
      return false;

    uint32_t vertexOffset = (uint32_t)baseVertexOffset;
    uint32_t indexOffset = (uint32_t)baseIndexOffset;
    uint32_t primitiveOffset = (uint32_t)basePrimitiveOffset;
    std::vector<glm::mat4> transforms;
    transforms.reserve(loadedScene->meshInstances.size());

    struct MeshletInfo
    {
      const RawMesh* rawMeshPtr{};
      std::span<const Vertex> vertices;
      std::vector<uint32_t> meshletIndices;
      std::vector<uint8_t> meshletPrimitives;
      std::vector<meshopt_Meshlet> rawMeshlets;
      //glm::mat4 transform;
    };

    std::vector<MeshletInfo> meshletInfos(loadedScene->rawMeshes.size());

    std::transform(
      std::execution::par,
      loadedScene->rawMeshes.begin(),
      loadedScene->rawMeshes.end(),
      meshletInfos.begin(),
      [] (const RawMesh& mesh) -> MeshletInfo
      {
        ZoneScopedN("Create meshlets for mesh");

        const auto maxMeshlets = meshopt_buildMeshletsBound(mesh.indices.size(), maxMeshletIndices, maxMeshletPrimitives);

        MeshletInfo meshletInfo;
        auto& [rawMeshPtr, vertices, meshletIndices, meshletPrimitives, rawMeshlets] = meshletInfo;

        rawMeshPtr = &mesh;
        vertices = mesh.vertices;
        meshletIndices.resize(maxMeshlets * maxMeshletIndices);
        meshletPrimitives.resize(maxMeshlets * maxMeshletPrimitives * 3);
        rawMeshlets.resize(maxMeshlets);
        
        const auto meshletCount = [&]
        {
          ZoneScopedN("meshopt_buildMeshlets");
          return meshopt_buildMeshlets(rawMeshlets.data(),
                                       meshletIndices.data(),
                                       meshletPrimitives.data(),
                                       mesh.indices.data(),
                                       mesh.indices.size(),
                                       reinterpret_cast<const float*>(mesh.vertices.data()),
                                       mesh.vertices.size(),
                                       sizeof(Vertex),
                                       maxMeshletIndices,
                                       maxMeshletPrimitives,
                                       meshletConeWeight);
        }();

        auto& lastMeshlet = rawMeshlets[meshletCount - 1];
        meshletIndices.resize(lastMeshlet.vertex_offset + lastMeshlet.vertex_count);
        meshletPrimitives.resize(lastMeshlet.triangle_offset + ((lastMeshlet.triangle_count * 3 + 3) & ~3));
        rawMeshlets.resize(meshletCount);

        return meshletInfo;
      });

    auto perMeshMeshlets = std::vector<std::vector<Meshlet>>(meshletInfos.size());

    // For each mesh, create "meshlet templates" (meshlets without per-instance data) and copy vertices, indices, and primitives to mega buffers
    for (size_t meshIndex = 0; meshIndex < meshletInfos.size(); meshIndex++)
    {
      const auto& [rawMesh, vertices, meshletIndices, meshletPrimitives, rawMeshlets] = meshletInfos[meshIndex];
      perMeshMeshlets[meshIndex].reserve(rawMeshlets.size());

      for (const auto& meshlet : rawMeshlets)
      {
        auto min = glm::vec3(std::numeric_limits<float>::max());
        auto max = glm::vec3(std::numeric_limits<float>::lowest());
        for (uint32_t i = 0; i < meshlet.triangle_count * 3; ++i)
        {
          const auto& vertex = rawMesh->vertices[meshletIndices[meshlet.vertex_offset + meshletPrimitives[meshlet.triangle_offset + i]]];
          min = glm::min(min, vertex.position);
          max = glm::max(max, vertex.position);
        }
        
        perMeshMeshlets[meshIndex].emplace_back(Meshlet{
          .vertexOffset = vertexOffset,
          .indexOffset = indexOffset + meshlet.vertex_offset,
          .primitiveOffset = primitiveOffset + meshlet.triangle_offset,
          .indexCount = meshlet.vertex_count,
          .primitiveCount = meshlet.triangle_count,
          .aabbMin = {min.x, min.y, min.z},
          .aabbMax = {max.x, max.y, max.z},
        });
      }

      //transforms.emplace_back(transform);
      vertexOffset += (uint32_t)vertices.size();
      indexOffset += (uint32_t)meshletIndices.size();
      primitiveOffset += (uint32_t)meshletPrimitives.size();

      std::ranges::copy(vertices, std::back_inserter(scene.vertices));
      std::ranges::copy(meshletIndices, std::back_inserter(scene.indices));
      std::ranges::copy(meshletPrimitives, std::back_inserter(scene.primitives));
    }

    // For each mesh instance, copy all of its associated meshlets (with per-instance data) and transform to the mega meshlet buffer
    for (const auto& meshInstance : loadedScene->meshInstances)
    {
      for (auto meshlet : perMeshMeshlets[meshInstance.rawMeshIndex])
      {
        meshlet.materialId = meshInstance.materialIdx;
        meshlet.instanceId = baseInstanceId + static_cast<uint32_t>(transforms.size());

        scene.meshlets.emplace_back(meshlet);
      }

      transforms.emplace_back(meshInstance.transform);
    }

    scene.transforms.insert(scene.transforms.end(), transforms.begin(), transforms.end());

    std::ranges::move(loadedScene->materials, std::back_inserter(scene.materials));
    std::ranges::move(loadedScene->lights, std::back_inserter(scene.lights));

    return true;
  }
} // namespace Utility

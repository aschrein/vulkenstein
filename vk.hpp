#ifndef VK_HPP
#define VK_HPP
#include "3rdparty/vulkan/vulkan.h"
#include "utils.hpp"

extern "C" void *compile_spirv(uint32_t const *pCode, size_t code_size);
extern "C" void release_spirv(void *ptr);
struct xcb_connection_t;
typedef uint32_t xcb_window_t;

// Data structures to keep track of the objects
// TODO(aschrein): Nuke this from the orbit
namespace vki {
template <typename T> inline T clamp(T v, T min, T max) {
  return v < min ? min : v > max ? max : v;
}

template <typename T> inline T alignup(T elem, uint32_t pow2) {
  return (elem + (1 << pow2) - 1) & (~((1 << pow2) - 1));
}
static uint32_t get_format_bpp(VkFormat format) {
  switch (format) {
  case VkFormat::VK_FORMAT_R8G8B8A8_SINT:
  case VkFormat::VK_FORMAT_R8G8B8A8_SRGB:
  case VkFormat::VK_FORMAT_R8G8B8A8_UINT:
  case VkFormat::VK_FORMAT_R8G8B8A8_SNORM:
  case VkFormat::VK_FORMAT_R8G8B8A8_UNORM:
  //
  case VkFormat::VK_FORMAT_B8G8R8A8_SRGB:
  case VkFormat::VK_FORMAT_B8G8R8A8_SINT:
  case VkFormat::VK_FORMAT_B8G8R8A8_UINT:
  case VkFormat::VK_FORMAT_B8G8R8A8_SNORM:
  case VkFormat::VK_FORMAT_B8G8R8A8_UNORM:
    return 4;
  case VkFormat::VK_FORMAT_D32_SFLOAT_S8_UINT:
    return 8;
  case VkFormat::VK_FORMAT_R32G32B32_SFLOAT:
    return 12;
  case VkFormat::VK_FORMAT_R32G32_SFLOAT:
    return 8;
  case VkFormat::VK_FORMAT_R32_SFLOAT:
    return 4;
  case VkFormat::VK_FORMAT_R32G32B32A32_SFLOAT:
    return 16;
  default:
    ASSERT_ALWAYS(false);
  }
  ASSERT_ALWAYS(false);
}
static uint32_t get_mip_size(uint32_t size, uint32_t mip_level) {
  return clamp<uint32_t>(size >> mip_level, 1, 1 << 16);
}
struct VkDevice_Impl {
  uint64_t magic_0;
  uint64_t magic_1;
  uint32_t refcnt;
  void release() {
    if (--refcnt == 0)
      memset(this, 0, sizeof(*this));
  }
};
struct VkSurfaceKHR_Impl {
  uint64_t magic_0;
  uint64_t magic_1;
  uint32_t refcnt;
  xcb_connection_t *connection;
  xcb_window_t window;
  void release() {
    if (--refcnt == 0)
      memset(this, 0, sizeof(*this));
  }
};
struct VkPhysicalDevice_Impl {
  uint64_t magic_0;
  uint64_t magic_1;
  uint32_t refcnt;
  void release() {
    if (--refcnt == 0)
      memset(this, 0, sizeof(*this));
  }
};

struct VkInstance_Impl {
  uint64_t magic_0;
  uint64_t magic_1;
  uint32_t refcnt;
  void release() {
    if (--refcnt == 0)
      memset(this, 0, sizeof(*this));
  }
};
struct VkDeviceMemory_Impl {
  uint64_t magic_0;
  uint64_t magic_1;
  uint32_t refcnt;
  uint8_t *ptr;
  size_t size;
  void release() {
    if (--refcnt == 0) {
      if (ptr != NULL)
        free(ptr);
      size = 0;
    }
  }
};
struct VkBuffer_Impl {
  uint64_t magic_0;
  uint64_t magic_1;
  uint32_t refcnt;
  VkDeviceMemory_Impl *mem;
  size_t offset;
  size_t size;
  void release() {
    if (--refcnt == 0) {
      mem->release();
      memset(this, 0, sizeof(*this));
    }
  }
  uint8_t *get_ptr() { return mem->ptr + this->offset; }
};
struct VkBufferView_Impl {
  uint64_t magic_0;
  uint64_t magic_1;
  uint32_t refcnt;
  VkBuffer_Impl *buf;
  VkFormat format;
  size_t offset;
  void release() {
    if (--refcnt == 0) {
      buf->release();
      memset(this, 0, sizeof(*this));
    }
  }
};
struct VkSampler_Impl {
  uint64_t magic_0;
  uint64_t magic_1;
  uint32_t refcnt;
  VkFilter magFilter;
  VkFilter minFilter;
  VkSamplerMipmapMode mipmapMode;
  VkSamplerAddressMode addressModeU;
  VkSamplerAddressMode addressModeV;
  VkSamplerAddressMode addressModeW;
  float mipLodBias;
  VkBool32 anisotropyEnable;
  float maxAnisotropy;
  VkBool32 compareEnable;
  VkCompareOp compareOp;
  float minLod;
  float maxLod;
  VkBorderColor borderColor;
  VkBool32 unnormalizedCoordinates;
  void release() {
    if (--refcnt == 0) {
      memset(this, 0, sizeof(*this));
    }
  }
};
struct VkImage_Impl {
  uint64_t magic_0;
  uint64_t magic_1;
  uint32_t refcnt;
  VkDeviceMemory_Impl *mem;
  size_t offset;
  size_t size;
  VkFormat format;
  VkExtent3D extent;
  uint32_t mipLevels;
  uint32_t arrayLayers;
  size_t mip_offsets[0x10];
  size_t array_offsets[0x10];
  VkSampleCountFlagBits samples;
  VkImageLayout initialLayout;
  uint8_t *get_ptr(uint32_t mip_level = 0, uint32_t array_elem = 0) {
    return mem->ptr + this->offset + array_offsets[array_elem] +
           mip_offsets[mip_level];
  }
  void release() {
    if (--refcnt == 0) {
      mem->release();
      memset(this, 0, sizeof(*this));
    }
  }
};
struct VkDescriptorSetLayout_Impl {
  uint64_t magic_0;
  uint64_t magic_1;
  uint32_t refcnt;
  uint32_t bindingCount;
  VkDescriptorSetLayoutBinding *pBindings;
  void release() {
    if (--refcnt == 0) {
      if (pBindings != NULL)
        free(pBindings);
      memset(this, 0, sizeof(*this));
    }
  }
};
struct VkPipelineLayout_Impl {
  uint64_t magic_0;
  uint64_t magic_1;
  uint32_t refcnt;
  uint32_t setLayoutCount;
  VkDescriptorSetLayout_Impl *pSetLayouts[0x10];
  uint32_t pushConstantRangeCount;
  VkPushConstantRange pPushConstantRanges[0x10];
  void release() {
    if (--refcnt == 0) {
      ito(setLayoutCount) pSetLayouts[i]->release();
      memset(this, 0, sizeof(*this));
    }
  }
};
struct VkImageView_Impl {
  uint64_t magic_0;
  uint64_t magic_1;
  uint32_t refcnt;
  VkImage_Impl *img;
  VkImageViewType type;
  VkFormat format;
  VkComponentMapping components;
  VkImageSubresourceRange subresourceRange;
  void release() {
    if (--refcnt == 0) {
      img->release();
      memset(this, 0, sizeof(*this));
    }
  }
};
struct VkShaderModule_Impl {
  uint64_t magic_0;
  uint64_t magic_1;
  uint32_t refcnt;
  void *jitted_code;
  void init(uint32_t const *pCode, size_t code_size) {
    jitted_code = compile_spirv(pCode, code_size);
  }
  void release() {
    if (--refcnt == 0) {
      if (jitted_code != NULL)
        release_spirv(jitted_code);
      memset(this, 0, sizeof(*this));
    }
  }
};

#define MEMZERO(obj) memset(&obj, 0, sizeof(obj))

struct VkSwapChain_Impl {
  uint64_t magic_0;
  uint64_t magic_1;
  uint32_t refcnt;
  uint32_t cur_image = 0;
  VkImage_Impl images[3];
  uint32_t image_count;
  uint32_t current_image;
  uint32_t width, height;
  VkFormat format;
  VkSurfaceKHR_Impl *surface;
  void release() {
    if (--refcnt == 0) {
      surface->release();
      for (auto &image : images) {
        if (image.mem != 0) {
          image.mem->release();
        }
      }
      MEMZERO((*this));
    }
  }
};

struct VkRenderPass_Impl {
  uint64_t magic_0;
  uint64_t magic_1;
  uint32_t refcnt;
  uint32_t attachmentCount;
  VkAttachmentDescription pAttachments[10];
  uint32_t subpassCount;
  struct VkSubpassDescription_Impl {
    VkSubpassDescriptionFlags flags;
    VkPipelineBindPoint pipelineBindPoint;
    uint32_t inputAttachmentCount;
    VkAttachmentReference pInputAttachments[0x10];
    uint32_t colorAttachmentCount;
    VkAttachmentReference pColorAttachments[0x10];
    VkAttachmentReference pResolveAttachments[0x10];
    bool has_depth_stencil_attachment;
    VkAttachmentReference pDepthStencilAttachment;
    uint32_t preserveAttachmentCount;
    uint32_t pPreserveAttachments[0x10];
  };
  VkSubpassDescription_Impl pSubpasses[10];
  uint32_t dependencyCount;
  VkSubpassDependency pDependencies[10];
  void release() {
    if (--refcnt == 0)
      memset(this, 0, sizeof(*this));
  }
};
struct VkPipeline_Impl {
  uint64_t magic_0;
  uint64_t magic_1;
  uint32_t refcnt;
  bool is_graphics;
  // Compute state
  VkShaderModule_Impl *cs;
  // Graphics state
  VkShaderModule_Impl *vs;
  VkShaderModule_Impl *ps;
  struct {
    uint32_t vertexBindingDescriptionCount;
    VkVertexInputBindingDescription pVertexBindingDescriptions[0x10];
    uint32_t vertexAttributeDescriptionCount;
    VkVertexInputAttributeDescription pVertexAttributeDescriptions[0x10];
  } IA_bindings;
  VkPrimitiveTopology IA_topology;
  struct {
    uint32_t viewportCount;
    VkViewport pViewports[0x10];
    uint32_t scissorCount;
    VkRect2D pScissors[0x10];
  } RS_viewports;
  struct {
    VkBool32 depthClampEnable;
    VkBool32 rasterizerDiscardEnable;
    VkPolygonMode polygonMode;
    VkCullModeFlags cullMode;
    VkFrontFace frontFace;
    VkBool32 depthBiasEnable;
    float depthBiasConstantFactor;
    float depthBiasClamp;
    float depthBiasSlopeFactor;
    float lineWidth;
  } RS_state;
  struct {
    VkBool32 depthTestEnable;
    VkBool32 depthWriteEnable;
    VkCompareOp depthCompareOp;
    VkBool32 depthBoundsTestEnable;
    VkBool32 stencilTestEnable;
    VkStencilOpState front;
    VkStencilOpState back;
    float minDepthBounds;
    float maxDepthBounds;
  } DS_state;
  struct {
    VkBool32 logicOpEnable;
    VkLogicOp logicOp;
    uint32_t attachmentCount;
    VkPipelineColorBlendAttachmentState pAttachments[0x10];
    float blendConstants[4];
  } OM_blend_state;
  struct {
    VkSampleCountFlagBits rasterizationSamples;
    VkBool32 sampleShadingEnable;
    float minSampleShading;
    VkSampleMask pSampleMask[0x10];
    VkBool32 alphaToCoverageEnable;
    VkBool32 alphaToOneEnable;
  } MS_state;
  VkPipelineLayout_Impl *layout;
  VkRenderPass_Impl *renderPass;
  uint32_t subpass;
  void release() {
    if (--refcnt == 0) {
      if (cs != NULL)
        cs->release();
      if (vs != NULL)
        vs->release();
      if (ps != NULL)
        ps->release();
      layout->release();
      renderPass->release();
      memset(this, 0, sizeof(*this));
    }
  }
};
struct VkDescriptorPool_Impl {
  uint64_t magic_0;
  uint64_t magic_1;
  uint32_t refcnt;
  uint32_t maxSets;
  uint32_t poolSizeCount;
  VkDescriptorPoolSize *pPoolSizes;
  void release() {
    if (--refcnt == 0) {
      if (pPoolSizes != NULL) {
        free(pPoolSizes);
      }
      memset(this, 0, sizeof(*this));
    }
  }
};
struct VkDescriptorSet_Impl {
  uint64_t magic_0;
  uint64_t magic_1;
  uint32_t refcnt;
  VkDescriptorPool_Impl *pool;
  VkDescriptorSetLayout_Impl *layout;
  struct Slot {
    VkDescriptorType type;
    VkImageView_Impl *image_view;
    VkBufferView_Impl *buf_view;
    VkSampler_Impl *sampler;
    VkBuffer_Impl *buffer;
    VkDeviceSize offset;
    VkDeviceSize range;
  };
  uint32_t slot_count;
  Slot *slots;
  void release() {
    if (--refcnt == 0) {
      ito(layout->bindingCount) {
        if (slots[i].buffer != NULL)
          slots[i].buffer->release();
        if (slots[i].image_view != NULL)
          slots[i].image_view->release();
        if (slots[i].buf_view != NULL)
          slots[i].buf_view->release();
        if (slots[i].sampler != NULL)
          slots[i].sampler->release();
      }
      layout->release();
      pool->release();
      if (slots)
        free(slots);
      memset(this, 0, sizeof(*this));
    }
  }
};
struct VkCommandBuffer_Impl {
  uint64_t magic_0;
  uint64_t magic_1;
  uint32_t refcnt;
  uint8_t *data;
  size_t data_size;
  size_t data_cursor;
  size_t read_cursor;
  template <typename T> void write_cmd(T const &cmd) {
    memcpy(data + data_cursor, &cmd, sizeof(cmd));
    data_cursor += sizeof(cmd);
    ASSERT_ALWAYS(data_cursor < data_size);
  }
  void write_key(uint8_t key) {
    data[data_cursor] = key;
    data_cursor += 1;
    ASSERT_ALWAYS(data_cursor < data_size);
  }
  void write(void const *pData, size_t size) {
    memcpy(data + data_cursor, pData, size);
    data_cursor += size;
    ASSERT_ALWAYS(data_cursor < data_size);
  }
  void init() {
    // 1 << 20 == 1 MB of data
    data_size = 16 * (1 << 20);
    data = (uint8_t *)malloc(data_size);
    data_cursor = 0;
    read_cursor = 0;
  }
  void rewind() { read_cursor = 0; }
  void reset() {
    read_cursor = 0;
    data_cursor = 0;
  }
  void release() {
    if (--refcnt == 0) {
      if (data != NULL) {
        free(data);
      }
      memset(this, 0, sizeof(*this));
    }
  }
  template <typename T> T consume() {
    T out = {};
    memcpy(&out, data + read_cursor, sizeof(T));
    read_cursor += sizeof(T);
    ASSERT_ALWAYS(read_cursor <= data_cursor);
    return out;
  }
  void *read(size_t size) {
    void *ptr = (void *)(data + read_cursor);
    read_cursor += size;
    ASSERT_ALWAYS(read_cursor <= data_cursor);
    return ptr;
  }
  bool has_items() { return read_cursor < data_cursor; }
};

struct VkFramebuffer_Impl {
  uint64_t magic_0;
  uint64_t magic_1;
  uint32_t refcnt;
  VkFramebufferCreateFlags flags;
  VkRenderPass_Impl *renderPass;
  uint32_t attachmentCount;
  VkImageView_Impl *pAttachments[0x10];
  uint32_t width;
  uint32_t height;
  uint32_t layers;
  void release() {
    if (--refcnt == 0) {
      ito(attachmentCount) pAttachments[i]->release();
      renderPass->release();
      memset(this, 0, sizeof(*this));
    }
  }
};
namespace cmd {
struct GPU_State { // doesn't do any ref counting here
  VkPipeline_Impl *graphics_pipeline = NULL;
  VkPipeline_Impl *compute_pipeline = NULL;
  VkRenderPass_Impl *render_pass = NULL;
  VkFramebuffer_Impl *framebuffer = NULL;
  VkDescriptorSet_Impl *descriptor_sets[0x10] = {};
  VkBuffer_Impl *index_buffer = NULL;
  VkDeviceSize index_buffer_offset = 0;
  VkIndexType index_type = VkIndexType::VK_INDEX_TYPE_UINT32;
  VkBuffer_Impl *vertex_buffers[0x10] = {};
  VkDeviceSize vertex_buffer_offsets[0x10] = {};
  VkRect2D render_area = {};
  uint32_t viewport_count = 0;
  VkViewport viewports[0x10] = {};
  uint8_t push_constants[0x100];
  void reset_state() { memset(this, 0, sizeof(*this)); }
  void execute_commands(VkCommandBuffer_Impl *cmd_buf);
};
} // namespace cmd
} // namespace vki
struct Shader_Symbols {
  void (*spv_main)(void *, uint64_t);
  uint32_t (*get_private_size)();
  uint32_t (*get_export_count)();
  uint32_t (*get_input_count)();
  uint32_t (*get_input_stride)();
  uint32_t (*get_output_count)();
  uint32_t (*get_output_stride)();
  uint32_t (*get_subgroup_size)();
  void (*get_export_items)(uint32_t *);
  void (*get_input_slots)(uint32_t *);
  void (*get_output_slots)(uint32_t *);
  struct Varying_Slot {
    uint32_t location;
    uint32_t offset;
    uint32_t format;
  };
  Varying_Slot input_slots[0x10];
  Varying_Slot output_slots[0x10];
  uint32_t input_item_count;
  uint32_t input_stride;
  uint32_t output_item_count;
  uint32_t output_stride;
  uint32_t private_storage_size;
  uint32_t export_count;
  uint32_t export_items[0x10];
  uint32_t subgroup_size;
};
extern "C" Shader_Symbols *get_shader_symbols(void *ptr);
extern "C" void draw_indexed(vki::cmd::GPU_State *state, uint32_t indexCount,
                             uint32_t instanceCount, uint32_t firstIndex,
                             int32_t vertexOffset, uint32_t firstInstance);
extern "C" void clear_attachment(vki::VkImageView_Impl *attachment, VkClearValue val);
#endif // VK_HPP

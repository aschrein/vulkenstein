#ifndef VK_HPP
#define VK_HPP
#include <xcb/xcb.h>
#define VK_USE_PLATFORM_XLIB_KHR
#include "3rdparty/vulkan/vulkan.h"
#include "utils.hpp"
// Data structures to keep track of the objects
// TODO(aschrein): Nuke this from the orbit
namespace vki {
struct VkDevice_Impl {
  uint32_t refcnt;
  void release() {
    if (--refcnt == 0)
      memset(this, 0, sizeof(*this));
  }
};
struct VkSurfaceKHR_Impl {
  uint32_t refcnt;
  xcb_connection_t *connection;
  xcb_window_t window;
  void release() {
    if (--refcnt == 0)
      memset(this, 0, sizeof(*this));
  }
};
struct VkPhysicalDevice_Impl {
  uint32_t refcnt;
  void release() {
    if (--refcnt == 0)
      memset(this, 0, sizeof(*this));
  }
};

struct VkInstance_Impl {
  uint32_t refcnt;
  void release() {
    if (--refcnt == 0)
      memset(this, 0, sizeof(*this));
  }
};
struct VkDeviceMemory_Impl {
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
struct VkImage_Impl {
  uint32_t refcnt;
  VkDeviceMemory_Impl *mem;
  size_t offset;
  size_t size;
  VkFormat format;
  VkExtent3D extent;
  uint32_t mipLevels;
  uint32_t arrayLayers;
  VkSampleCountFlagBits samples;
  VkImageLayout initialLayout;
  void release() {
    if (--refcnt == 0) {
      mem->release();
      memset(this, 0, sizeof(*this));
    }
  }
};
struct VkDescriptorSetLayout_Impl {
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
extern "C" void *compile_spirv(uint32_t const *pCode, size_t code_size);
extern "C" void release_spirv(void *ptr);
struct VkShaderModule_Impl {
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
  uint32_t refcnt;
  uint32_t cur_image = 0;
  VkImage_Impl images[3];
  uint32_t image_count;
  uint32_t current_image;
  uint32_t width, height;
  VkFormat format;
  VkSurfaceKHR_Impl *surface;
  void present() {}
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
struct VkSampler_Impl {
  uint32_t refcnt;
  void release() {
    if (--refcnt == 0)
      memset(this, 0, sizeof(*this));
  }
};
struct VkDescriptorSet_Impl {
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
}
#endif // VK_HPP

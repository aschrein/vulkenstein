#include <xcb/xcb.h>

#define VK_USE_PLATFORM_XLIB_KHR
#include "3rdparty/vulkan/vk_icd.h"
#include "3rdparty/vulkan/vk_layer.h"
#include "3rdparty/vulkan/vk_platform.h"
#include "3rdparty/vulkan/vulkan.h"
#include "3rdparty/vulkan/vulkan_xcb.h"

#include "utils.hpp"
#include <stdint.h>

#include <sys/mman.h>
#include <unistd.h>
#define DLL_EXPORT __attribute__((visibility("default")))

#define ALL_FUNCTIONS                                                          \
  CASE(vkCreateInstance);                                                      \
  CASE(vkCreateXcbSurfaceKHR);                                                 \
  CASE(vkGetPhysicalDeviceXcbPresentationSupportKHR);                          \
  CASE(vkEnumerateInstanceExtensionProperties);                                \
  CASE(vkEnumerateInstanceVersion);                                            \
  CASE(vkEnumeratePhysicalDevices);                                            \
  CASE(vkGetPhysicalDeviceSurfaceSupportKHR);                                  \
  CASE(vkGetPhysicalDeviceSurfaceFormatsKHR);                                  \
  CASE(vkGetPhysicalDeviceSurfaceCapabilitiesKHR);                             \
  CASE(vkGetPhysicalDeviceSurfacePresentModesKHR);                             \
  CASE(vkEnumerateDeviceExtensionProperties);                                  \
  CASE(vkGetPhysicalDeviceFeatures);                                           \
  CASE(vkGetPhysicalDeviceQueueFamilyProperties);                              \
  CASE(vkGetPhysicalDeviceProperties);                                         \
  CASE(vkCreateDevice);                                                        \
  CASE(vkGetDeviceProcAddr);                                                   \
  CASE(vkGetPhysicalDeviceMemoryProperties);                                   \
  CASE(vkGetPhysicalDeviceFormatProperties);                                   \
  CASE(vkGetSwapchainImagesKHR);                                               \
  CASE(vkCreateSwapchainKHR);                                                  \
  CASE(vkCreateImageView);                                                     \
  CASE(vkCreateFence);                                                         \
  CASE(vkAllocateCommandBuffers);                                              \
  CASE(vkCreateImage);                                                         \
  CASE(vkAllocateMemory);                                                      \
  CASE(vkGetImageMemoryRequirements);                                          \
  CASE(vkBindImageMemory);                                                     \
  CASE(vkCreateRenderPass);                                                    \
  CASE(vkCreatePipelineCache);                                                 \
  CASE(vkDestroyPipelineCache);                                                \
  CASE(vkGetPipelineCacheData);                                                \
  CASE(vkCreateCommandPool);                                                   \
  CASE(vkDestroyCommandPool);                                                  \
  CASE(vkGetDeviceQueue);                                                      \
  CASE(vkCreateSemaphore);                                                     \
  CASE(vkDestroySemaphore);                                                    \
  CASE(vkDestroyRenderPass);                                                   \
  CASE(vkCreateFramebuffer);                                                   \
  CASE(vkDestroyFramebuffer);                                                  \
  CASE(vkCreateShaderModule);                                                  \
  CASE(vkDestroyShaderModule);                                                 \
  CASE(vkCreateBuffer);                                                        \
  CASE(vkDestroyBuffer);                                                       \
  CASE(vkCreateBufferView);                                                    \
  CASE(vkDestroyBufferView);                                                   \
  CASE(vkGetBufferMemoryRequirements);                                         \
  CASE(vkBindBufferMemory);                                                    \
  CASE(vkMapMemory);                                                           \
  CASE(vkUnmapMemory);                                                         \
  CASE(vkBeginCommandBuffer);                                                  \
  CASE(vkEndCommandBuffer);                                                    \
  CASE(vkResetCommandBuffer);                                                  \
  CASE(vkCmdCopyBuffer);                                                       \
  CASE(vkQueueSubmit);                                                         \
  CASE(vkQueueWaitIdle);                                                       \
  CASE(vkDeviceWaitIdle);                                                      \
  CASE(vkWaitForFences);                                                       \
  CASE(vkResetFences);                                                         \
  CASE(vkDestroyFence);                                                        \
  CASE(vkGetFenceStatus);                                                      \
  CASE(vkDestroySemaphore);                                                    \
  CASE(vkCreateEvent);                                                         \
  CASE(vkDestroyEvent);                                                        \
  CASE(vkGetEventStatus);                                                      \
  CASE(vkFreeCommandBuffers);                                                  \
  CASE(vkFreeMemory);                                                          \
  CASE(vkCreateDescriptorSetLayout);                                           \
  CASE(vkDestroyDescriptorSetLayout);                                          \
  CASE(vkCreatePipelineLayout);                                                \
  CASE(vkDestroyPipelineLayout);                                               \
  CASE(vkCreateGraphicsPipelines);                                             \
  CASE(vkDestroyPipeline);                                                     \
  CASE(vkCreateDescriptorPool);                                                \
  CASE(vkDestroyDescriptorPool);                                               \
  CASE(vkAllocateDescriptorSets);                                              \
  CASE(vkFreeDescriptorSets);                                                  \
  CASE(vkResetDescriptorPool);                                                 \
  CASE(vkUpdateDescriptorSets);                                                \
  CASE(vkCmdBeginRenderPass);                                                  \
  CASE(vkCmdEndRenderPass);                                                    \
  CASE(vkCmdSetViewport);                                                      \
  CASE(vkCmdSetScissor);                                                       \
  CASE(vkCmdBindDescriptorSets);                                               \
  CASE(vkCmdBindPipeline);                                                     \
  CASE(vkCmdBindVertexBuffers);                                                \
  CASE(vkCmdBindIndexBuffer);                                                  \
  CASE(vkCmdDrawIndexed);                                                      \
  CASE(vkDestroyImageView);                                                    \
  CASE(vkDestroySwapchainKHR);                                                 \
  CASE(vkDestroyImage);                                                        \
  CASE(vkAcquireNextImageKHR);                                                 \
  CASE(vkQueuePresentKHR);                                                     \
  (void)0

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
static constexpr uint32_t MAX_OBJECTS = 1000;
#define MEMZERO(obj) memset(&obj, 0, sizeof(obj))
// Simple object pool
template <typename T, int N = MAX_OBJECTS> struct Pool {
  T pool[N];
  uint32_t next_free_slot;
  Pool() { memset(this, 0, sizeof(*this)); }
  void find_next_free_slot() {
    bool first_attempt = true;
    while (true) {
      while (next_free_slot < N && pool[next_free_slot].refcnt != 0) {
        next_free_slot += 1;
      }
      if (next_free_slot != N)
        break;
      ASSERT_ALWAYS(first_attempt);
      next_free_slot = 0;
      first_attempt = false;
    }
  }
  T *alloc() {
    find_next_free_slot();
    T *out = &pool[next_free_slot];
    ASSERT_ALWAYS(out->refcnt == 0);
    memset(out, 0, sizeof(T));
    out->refcnt = 1;
    return out;
  }
};

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

#define OBJ_POOL(type) Pool<type##_Impl> type##_pool = {};

OBJ_POOL(VkBuffer)
OBJ_POOL(VkSampler)
OBJ_POOL(VkDescriptorSet)
OBJ_POOL(VkDescriptorPool)
OBJ_POOL(VkPipeline)
OBJ_POOL(VkCommandBuffer)
OBJ_POOL(VkBufferView)
OBJ_POOL(VkRenderPass)
OBJ_POOL(VkImageView)
OBJ_POOL(VkImage)
OBJ_POOL(VkDeviceMemory)
OBJ_POOL(VkShaderModule)
OBJ_POOL(VkDescriptorSetLayout)
OBJ_POOL(VkPipelineLayout)
OBJ_POOL(VkDevice)
OBJ_POOL(VkSurfaceKHR)
OBJ_POOL(VkPhysicalDevice)
OBJ_POOL(VkInstance)
OBJ_POOL(VkSwapChain)

#define DECL_IMPL(type)                                                        \
  struct type##_Impl {                                                         \
    uint32_t refcnt;                                                           \
    void release() {                                                           \
      if (--refcnt == 0)                                                       \
        memset(this, 0, sizeof(*this));                                        \
    }                                                                          \
  };

#define OBJ_POOL_DUMMY(type)                                                   \
  DECL_IMPL(type)                                                              \
  Pool<type##_Impl> type##_pool = {};

OBJ_POOL_DUMMY(VkSemaphore)
OBJ_POOL_DUMMY(VkQueue)
OBJ_POOL_DUMMY(VkCommandPool)
OBJ_POOL_DUMMY(VkFence)
OBJ_POOL_DUMMY(VkEvent)
OBJ_POOL_DUMMY(VkPipelineCache)

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
OBJ_POOL(VkFramebuffer)

#define ALLOC_VKOBJ(type) (type)(void *) vki::type##_pool.alloc()
#define ALLOC_VKOBJ_T(type) vki::type##_pool.alloc()
#define RELEASE_VKOBJ(obj, type)                                               \
  do {                                                                         \
    NOTNULL(obj);                                                              \
    ((vki::type##_Impl *)obj)->release();                                      \
  } while (0)

// static VkInstance_Impl g_instance;
// static VkPhysicalDevice_Impl g_phys_device;
// static VkDevice_Impl g_device;
// static VkSwapChain_Impl g_swapchain;
// static VkSurfaceKHR_Impl g_surface;
// static xcb_connection_t *g_connection;
// static xcb_window_t g_window;
void init() {
  //  MEMZERO(g_instance);
  //  MEMZERO(g_phys_device);
  //  MEMZERO(g_device);
  //  MEMZERO(g_swapchain);
  //  MEMZERO(g_surface);
  //  MEMZERO(g_connection);
  //  MEMZERO(g_window);
}
namespace cmd {
enum class Cmd_t : uint8_t {
  CopyBuffer = 1,
  CopyImage,
  RenderPassBegin,
  RenderPassEnd,
  SetViewport,
  SetScissor,
  SetLineWidth,
  SetBlendConstants,
  SetDepthBias,
  SetDepthBounds,
  SetStencilCompareMask,
  SetStencilWriteMask,
  SetStencilReference,
  BindDescriptorSets,
  BindIndexBuffer,
  BindVertexBuffers,
  Draw,
  DrawIndexed,
  DrawIndirect,
  DrawIndexedIndirect,
  Dispatch,
  DispatchIndirect,
  CopyBufferToImage,
  CopyImageToBuffer,
  UpdateBuffer,
  FillBuffer,
  ClearColorImage,
  ClearDepthStencilImage,
  ClearAttachments,
  ResolveImage,
  SetEvent,
  ResetEvent,
  WaitEvents,
  PipelineBarrier,
  BeginQuery,
  EndQuery,
  ResetQueryPool,
  WriteTimestamp,
  CopyQueryPoolResults,
  PushConstants,
  NextSubpass,
  ExecuteCommands,
  BindPipeline
};
struct CopyBuffer {
  VkBuffer_Impl *src;
  VkBuffer_Impl *dst;
  uint32_t regionCount;
  VkBufferCopy pRegions[0x10];
};
struct CopyImage {
  VkImage_Impl *src;
  VkImage_Impl *dst;
  uint32_t regionCount;
  VkImageCopy pRegions[0x10];
};
struct RenderPassBegin {
  VkRenderPass_Impl *renderPass;
  VkFramebuffer_Impl *framebuffer;
  VkRect2D renderArea;
  uint32_t clearValueCount;
  VkClearValue pClearValues[0x10];
};
struct SetViewport {
  uint32_t firstViewport;
  uint32_t viewportCount;
  VkViewport pViewports[0x10];
};
struct BindPipeline {
  VkPipelineBindPoint pipelineBindPoint;
  VkPipeline_Impl *pipeline;
};
struct SetScissor {
  uint32_t firstScissor;
  uint32_t scissorCount;
  VkRect2D pScissors[0x10];
};
struct SetLineWidth {
  float lineWidth;
};
struct SetDepthBias {
  float depthBiasConstantFactor;
  float depthBiasClamp;
  float depthBiasSlopeFactor;
};
struct SetBlendConstants {
  float blendConstants[4];
};
struct SetDepthBounds {
  float minDepthBounds;
  float maxDepthBounds;
};
struct SetStencilCompareMask {
  VkStencilFaceFlags faceMask;
  uint32_t compareMask;
};
struct SetStencilWriteMask {
  VkStencilFaceFlags faceMask;
  uint32_t writeMask;
};
struct SetStencilReference {
  VkStencilFaceFlags faceMask;
  uint32_t reference;
};
struct BindDescriptorSets {
  VkPipelineBindPoint pipelineBindPoint;
  VkPipelineLayout_Impl *layout;
  uint32_t firstSet;
  uint32_t descriptorSetCount;
  VkDescriptorSet_Impl *pDescriptorSets[0x10];
  uint32_t dynamicOffsetCount;
  uint32_t pDynamicOffsets[0x10];
};
struct BindIndexBuffer {
  VkBuffer_Impl *buffer;
  VkDeviceSize offset;
  VkIndexType indexType;
};
struct BindVertexBuffers {
  uint32_t firstBinding;
  uint32_t bindingCount;
  VkBuffer_Impl *pBuffers[0x40];
  VkDeviceSize pOffsets[0x40];
};
struct Draw {
  uint32_t vertexCount;
  uint32_t instanceCount;
  uint32_t firstVertex;
  uint32_t firstInstance;
};
struct DrawIndexed {
  uint32_t indexCount;
  uint32_t instanceCount;
  uint32_t firstIndex;
  int32_t vertexOffset;
  uint32_t firstInstance;
};
struct DrawIndirect {
  VkBuffer_Impl *buffer;
  VkDeviceSize offset;
  uint32_t drawCount;
  uint32_t stride;
};
struct DrawIndexedIndirect {
  VkBuffer_Impl *buffer;
  VkDeviceSize offset;
  uint32_t drawCount;
  uint32_t stride;
};
struct Dispatch {
  uint32_t groupCountX;
  uint32_t groupCountY;
  uint32_t groupCountZ;
};
struct DispatchIndirect {
  VkBuffer_Impl *buffer;
  VkDeviceSize offset;
};
struct CopyBufferToImage {
  VkBuffer_Impl *srcBuffer;
  VkImage_Impl *dstImage;
  uint32_t regionCount;
  VkBufferImageCopy pRegions[0x10];
};
struct CopyImageToBuffer {
  VkBuffer_Impl *dstBuffer;
  VkImage_Impl *srcImage;
  uint32_t regionCount;
  VkBufferImageCopy pRegions[0x10];
};
struct UpdateBuffer {
  VkBuffer_Impl *dstBuffer;
  VkDeviceSize dstOffset;
  VkDeviceSize dataSize;
  // uint8_t pData[dataSize] follows the cmd in the command buffer
};
struct ClearAttachments {
  uint32_t attachmentCount;
  VkClearAttachment pAttachments[0x10];
  uint32_t rectCount;
  VkClearRect pRects[0x10];
};
struct PushConstants {
  VkPipelineLayout_Impl *layout;
  VkShaderStageFlags stageFlags;
  uint32_t offset;
  uint32_t size;
  // uint8_t pData[size] follows the cmd in the command buffer
};
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
  void reset_state() { memset(this, 0, sizeof(*this)); }
  void execute_commands(VkCommandBuffer_Impl *cmd_buf) {
    reset_state();
    while (cmd_buf->has_items()) {
      cmd::Cmd_t op = cmd_buf->consume<Cmd_t>();
      switch (op) {
      case Cmd_t::BindIndexBuffer: {
        cmd::BindIndexBuffer cmd = cmd_buf->consume<cmd::BindIndexBuffer>();
        index_buffer = cmd.buffer;
        index_type = cmd.indexType;
        index_buffer_offset = cmd.offset;
        break;
      }
      case Cmd_t::BindVertexBuffers: {
        cmd::BindVertexBuffers cmd = cmd_buf->consume<cmd::BindVertexBuffers>();
        ito(cmd.bindingCount) {
          vertex_buffers[i + cmd.firstBinding] = cmd.pBuffers[i];
          vertex_buffer_offsets[i + cmd.firstBinding] = cmd.pOffsets[i];
        }
        break;
      }
      case Cmd_t::BindDescriptorSets: {
        cmd::BindDescriptorSets cmd =
            cmd_buf->consume<cmd::BindDescriptorSets>();
        ASSERT_ALWAYS(cmd.dynamicOffsetCount == 0);
        ito(cmd.descriptorSetCount) {
          descriptor_sets[i + cmd.firstSet] = cmd.pDescriptorSets[i];
        }
        break;
      }
      case Cmd_t::BindPipeline: {
        cmd::BindPipeline cmd = cmd_buf->consume<cmd::BindPipeline>();
        if (cmd.pipelineBindPoint ==
            VkPipelineBindPoint::VK_PIPELINE_BIND_POINT_GRAPHICS) {
          graphics_pipeline = cmd.pipeline;
        } else {
          compute_pipeline = cmd.pipeline;
        }
        break;
      }
      case Cmd_t::RenderPassBegin: {
        cmd::RenderPassBegin cmd = cmd_buf->consume<cmd::RenderPassBegin>();
        ASSERT_ALWAYS(cmd.renderPass != NULL);
        render_pass = cmd.renderPass;
        render_area = cmd.renderArea;
        framebuffer = cmd.framebuffer;
        ito(cmd.clearValueCount) {
          // TODO
          (void)cmd.pClearValues[i];
        }
        break;
      }
      case Cmd_t::RenderPassEnd: {
        render_pass = NULL;
        render_area = {};
        framebuffer = NULL;
        break;
      }
      case Cmd_t::SetViewport: {
        cmd::SetViewport cmd = cmd_buf->consume<cmd::SetViewport>();
        ASSERT_ALWAYS(cmd.viewportCount == 1);
        viewports[0] = cmd.pViewports[0];
        viewport_count = 1;
        break;
      }
      case Cmd_t::CopyBuffer: {
        cmd::CopyBuffer cmd = cmd_buf->consume<cmd::CopyBuffer>();
        ito(cmd.regionCount) {
          memcpy(cmd.dst->get_ptr() + cmd.pRegions[i].dstOffset,
                 cmd.src->get_ptr() + cmd.pRegions[i].srcOffset,
                 cmd.pRegions[i].size);
        }
        break;
      }
      case Cmd_t::SetScissor: {
        cmd::SetScissor cmd = cmd_buf->consume<cmd::SetScissor>();
        // TODO
        break;
      }
      case Cmd_t::DrawIndexed: {
        cmd::DrawIndexed cmd = cmd_buf->consume<cmd::DrawIndexed>();
        break;
      }
      default:
        UNIMPLEMENTED;
      }
    }
  }
} g_gpu_state;
} // namespace cmd
#define WRITE_CMD(cmdbuf, cmd, type)                                           \
  {                                                                            \
    cmdbuf->write_key((uint8_t)vki::cmd::Cmd_t::type);                         \
    cmdbuf->write_cmd(cmd);                                                    \
  }

}; // namespace vki
// Allocate a trap for unimplemented function
// x86_64:linux only
void *allocate_trap(char const *fun_name) {
  static uint32_t allocated_memory_size = 0;
  static uint32_t executable_memory_cursor = 0;
  static uint8_t *executable_memory = NULL;
  if (executable_memory == NULL) {
    uint32_t page_size = (uint32_t)sysconf(_SC_PAGE_SIZE);
    // Allocate executable pages
    allocated_memory_size = page_size * 10;
    executable_memory = (uint8_t *)mmap(NULL, allocated_memory_size,
                                        PROT_READ | PROT_WRITE | PROT_EXEC,
                                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ASSERT_ALWAYS(executable_memory != MAP_FAILED);
  }

  size_t len = MIN(strlen(fun_name), 127);
  uint8_t *const mem = executable_memory + (size_t)executable_memory_cursor;
  uint8_t *ptr = mem;
  auto push_byte = [&](uint8_t b) {
    ptr[0] = b;
    ptr++;
    executable_memory_cursor++;
  };
  uint8_t lb_0 = ((uint8_t *)&len)[0] + 2 + 5;
  // clang-format off
  void *trap_start = ptr;
  //   cc                      break
  // push_byte(0xcc);
  //   48 8d 05 00 00 00 00    lea    rax,[rip]
  push_byte(0x48); push_byte(0x8d); push_byte(0x05); push_byte(0x00); push_byte(0x00); push_byte(0x00); push_byte(0x00);
  //   eb yy                   jmp    yy
  push_byte(0xeb); push_byte(lb_0);
  // push the string to instruction
  push_byte((uint8_t)'t');
  push_byte((uint8_t)'r');
  push_byte((uint8_t)'a');
  push_byte((uint8_t)'p');
  push_byte((uint8_t)' ');
  ito (len)
    push_byte((uint8_t)fun_name[i]);
  push_byte((uint8_t)'\n');
  push_byte(0x0);
  //   48 83 c0 0d             add    rax,0xd
  push_byte(0x48); push_byte(0x83); push_byte(0xc0); push_byte(0x02);
  //   48 89 c6                mov    rsi,rax    ; rsi <- char *
  push_byte(0x48); push_byte(0x89); push_byte(0xc6);
  //   48 c7 c0 01 00 00 00    mov    rax,0x1    ; write
  push_byte(0x48); push_byte(0xc7); push_byte(0xc0); push_byte(0x01); push_byte(0x00); push_byte(0x00); push_byte(0x00);
  //   48 c7 c7 01 00 00 00    mov    rdi,0x1    ; stdout
  push_byte(0x48); push_byte(0xc7); push_byte(0xc7); push_byte(0x01); push_byte(0x00); push_byte(0x00); push_byte(0x00);
  //   48 c7 c2 66 00 00 00    mov    rdx,0x66   ; rdx <- strlen
  push_byte(0x48); push_byte(0xc7); push_byte(0xc2); push_byte(lb_0); push_byte(0x00); push_byte(0x00); push_byte(0x00);
  //   0f 05                   syscall
  push_byte(0x0f); push_byte(0x05);
  //   cc                      break
  // push_byte(0xcc);
  static uint8_t exit_1[] = { 0xBB, 0x00, 0x00, 0x00, 0x00, 0xB8, 0x01, 0x00, 0x00, 0x00, 0xCD, 0x80 };
  for (auto b : exit_1)
  push_byte(b);
  // clang-format on
  ASSERT_ALWAYS(executable_memory_cursor < allocated_memory_size);
  //  fprintf(stderr, "trap: %s\n", fun_name);
  return trap_start;
}

uint32_t get_format_bpp(VkFormat format) {
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
    return 4;
  default:
    ASSERT_ALWAYS(false);
  }
  ASSERT_ALWAYS(false);
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceSupportKHR(
    VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex,
    VkSurfaceKHR surface, VkBool32 *pSupported) {
  if (pSupported != NULL)
    *pSupported = VK_TRUE;
  return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceFormatsKHR(
    VkPhysicalDevice physicalDevice, VkSurfaceKHR surface,
    uint32_t *pSurfaceFormatCount, VkSurfaceFormatKHR *pSurfaceFormats) {
  if (pSurfaceFormatCount != NULL && pSurfaceFormats == NULL) {
    *pSurfaceFormatCount = 1;
    return VK_SUCCESS;
  }
  pSurfaceFormats[0].colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
  pSurfaceFormats[0].format = VK_FORMAT_R8G8B8A8_SRGB;
  return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
    VkPhysicalDevice physicalDevice, VkSurfaceKHR surface,
    VkSurfaceCapabilitiesKHR *pSurfaceCapabilities) {
  struct {
    uint32_t minImageCount = 1;
    uint32_t maxImageCount = 2;
    VkExtent2D currentExtent = {512, 512};
    VkExtent2D minImageExtent = {1, 1};
    VkExtent2D maxImageExtent = {1 << 16, 1 << 16};
    uint32_t maxImageArrayLayers = 1;
    VkSurfaceTransformFlagsKHR supportedTransforms =
        VkSurfaceTransformFlagBitsKHR::VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    VkSurfaceTransformFlagBitsKHR currentTransform =
        VkSurfaceTransformFlagBitsKHR::VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    VkCompositeAlphaFlagsKHR supportedCompositeAlpha = 0;
    VkImageUsageFlags supportedUsageFlags = 0;
  } caps;
  NOTNULL(pSurfaceCapabilities);
  ASSERT_ALWAYS(sizeof(caps) == sizeof(*pSurfaceCapabilities));
  memcpy(pSurfaceCapabilities, &caps, sizeof(caps));
  return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfacePresentModesKHR(
    VkPhysicalDevice physicalDevice, VkSurfaceKHR surface,
    uint32_t *pPresentModeCount, VkPresentModeKHR *pPresentModes) {
  if (pPresentModeCount != NULL && pPresentModes == NULL) {
    *pPresentModeCount = 2;
    return VK_SUCCESS;
  }
  *pPresentModeCount = 2;
  pPresentModes[0] = VkPresentModeKHR::VK_PRESENT_MODE_FIFO_KHR;

  if (*pPresentModes < 2)
    return VK_INCOMPLETE;
  pPresentModes[1] = VkPresentModeKHR::VK_PRESENT_MODE_MAILBOX_KHR;
  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceProperties(
    VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties *pProperties) {
  NOTNULL(pProperties);
  struct {
    uint32_t maxImageDimension1D = 1 << 16;
    uint32_t maxImageDimension2D = 1 << 16;
    uint32_t maxImageDimension3D = 1 << 16;
    uint32_t maxImageDimensionCube = 1 << 16;
    uint32_t maxImageArrayLayers = 1 << 16;
    uint32_t maxTexelBufferElements = 1 << 30;
    uint32_t maxUniformBufferRange = 1 << 30;
    uint32_t maxStorageBufferRange = 1 << 30;
    uint32_t maxPushConstantsSize = 1 << 7;
    uint32_t maxMemoryAllocationCount = 1 << 20;
    uint32_t maxSamplerAllocationCount = 1 << 20;
    VkDeviceSize bufferImageGranularity = 1 << 20;
    VkDeviceSize sparseAddressSpaceSize = 1 << 20;
    uint32_t maxBoundDescriptorSets = 1 << 10;
    uint32_t maxPerStageDescriptorSamplers = 1 << 20;
    uint32_t maxPerStageDescriptorUniformBuffers = 1 << 20;
    uint32_t maxPerStageDescriptorStorageBuffers = 1 << 20;
    uint32_t maxPerStageDescriptorSampledImages = 1 << 20;
    uint32_t maxPerStageDescriptorStorageImages = 1 << 20;
    uint32_t maxPerStageDescriptorInputAttachments = 1 << 20;
    uint32_t maxPerStageResources = 1 << 20;
    uint32_t maxDescriptorSetSamplers = 1 << 20;
    uint32_t maxDescriptorSetUniformBuffers = 1 << 20;
    uint32_t maxDescriptorSetUniformBuffersDynamic = 1 << 20;
    uint32_t maxDescriptorSetStorageBuffers = 1 << 20;
    uint32_t maxDescriptorSetStorageBuffersDynamic = 1 << 20;
    uint32_t maxDescriptorSetSampledImages = 1 << 20;
    uint32_t maxDescriptorSetStorageImages = 1 << 20;
    uint32_t maxDescriptorSetInputAttachments = 1 << 20;
    uint32_t maxVertexInputAttributes = 1 << 20;
    uint32_t maxVertexInputBindings = 1 << 20;
    uint32_t maxVertexInputAttributeOffset = 1 << 20;
    uint32_t maxVertexInputBindingStride = 1 << 20;
    uint32_t maxVertexOutputComponents = 1 << 20;
    uint32_t maxTessellationGenerationLevel = 1 << 20;
    uint32_t maxTessellationPatchSize = 1 << 20;
    uint32_t maxTessellationControlPerVertexInputComponents = 1 << 20;
    uint32_t maxTessellationControlPerVertexOutputComponents = 1 << 20;
    uint32_t maxTessellationControlPerPatchOutputComponents = 1 << 20;
    uint32_t maxTessellationControlTotalOutputComponents = 1 << 20;
    uint32_t maxTessellationEvaluationInputComponents = 1 << 20;
    uint32_t maxTessellationEvaluationOutputComponents = 1 << 20;
    uint32_t maxGeometryShaderInvocations = 1 << 20;
    uint32_t maxGeometryInputComponents = 1 << 20;
    uint32_t maxGeometryOutputComponents = 1 << 20;
    uint32_t maxGeometryOutputVertices = 1 << 20;
    uint32_t maxGeometryTotalOutputComponents = 1 << 20;
    uint32_t maxFragmentInputComponents = 1 << 20;
    uint32_t maxFragmentOutputAttachments = 1 << 20;
    uint32_t maxFragmentDualSrcAttachments = 1 << 20;
    uint32_t maxFragmentCombinedOutputResources = 1 << 20;
    uint32_t maxComputeSharedMemorySize = 1 << 20;
    uint32_t maxComputeWorkGroupCount_0 = 1 << 10;
    uint32_t maxComputeWorkGroupCount_1 = 1 << 10;
    uint32_t maxComputeWorkGroupCount_2 = 1 << 10;
    uint32_t maxComputeWorkGroupInvocations = 1 << 20;
    uint32_t maxComputeWorkGroupSize_0 = 1 << 10;
    uint32_t maxComputeWorkGroupSize_1 = 1 << 10;
    uint32_t maxComputeWorkGroupSize_2 = 1 << 10;
    uint32_t subPixelPrecisionBits = 16;
    uint32_t subTexelPrecisionBits = 16;
    uint32_t mipmapPrecisionBits = 16;
    uint32_t maxDrawIndexedIndexValue = 1 << 20;
    uint32_t maxDrawIndirectCount = 1 << 20;
    float maxSamplerLodBias = 1000.0f;
    float maxSamplerAnisotropy = 1000.0f;
    uint32_t maxViewports = 1 << 10;
    uint32_t maxViewportDimensions_0 = 1 << 16;
    uint32_t maxViewportDimensions_1 = 1 << 16;
    float viewportBoundsRange_0 = 0.0f;
    float viewportBoundsRange_1 = 1.0f;
    uint32_t viewportSubPixelBits = 16;
    size_t minMemoryMapAlignment = 0x10;
    VkDeviceSize minTexelBufferOffsetAlignment = 0x10;
    VkDeviceSize minUniformBufferOffsetAlignment = 0x10;
    VkDeviceSize minStorageBufferOffsetAlignment = 0x10;
    int32_t minTexelOffset = 0;
    uint32_t maxTexelOffset = 1 << 20;
    int32_t minTexelGatherOffset = 0;
    uint32_t maxTexelGatherOffset = 1 << 20;
    float minInterpolationOffset = 0.0f;
    float maxInterpolationOffset = 1.0f;
    uint32_t subPixelInterpolationOffsetBits = 16;
    uint32_t maxFramebufferWidth = 1 << 16;
    uint32_t maxFramebufferHeight = 1 << 16;
    uint32_t maxFramebufferLayers = 1 << 16;
    VkSampleCountFlags framebufferColorSampleCounts = 1 << 20;
    VkSampleCountFlags framebufferDepthSampleCounts = 1 << 20;
    VkSampleCountFlags framebufferStencilSampleCounts = 1 << 20;
    VkSampleCountFlags framebufferNoAttachmentsSampleCounts = 1 << 20;
    uint32_t maxColorAttachments = 1 << 10;
    VkSampleCountFlags sampledImageColorSampleCounts = 1 << 20;
    VkSampleCountFlags sampledImageIntegerSampleCounts = 1 << 20;
    VkSampleCountFlags sampledImageDepthSampleCounts = 1 << 20;
    VkSampleCountFlags sampledImageStencilSampleCounts = 1 << 20;
    VkSampleCountFlags storageImageSampleCounts = 1 << 20;
    uint32_t maxSampleMaskWords = 1 << 20;
    VkBool32 timestampComputeAndGraphics = VK_TRUE;
    float timestampPeriod = 1 << 20;
    uint32_t maxClipDistances = 1 << 20;
    uint32_t maxCullDistances = 1 << 20;
    uint32_t maxCombinedClipAndCullDistances = 1 << 20;
    uint32_t discreteQueuePriorities = 1 << 20;
    float pointSizeRange_0 = 0.0f;
    float pointSizeRange_1 = 10000.0f;
    float lineWidthRange_0 = 0.0f;
    float lineWidthRange_1 = 10000.0f;
    float pointSizeGranularity = 1 << 20;
    float lineWidthGranularity = 1 << 20;
    VkBool32 strictLines = VK_TRUE;
    VkBool32 standardSampleLocations = VK_TRUE;
    VkDeviceSize optimalBufferCopyOffsetAlignment = 0x10;
    VkDeviceSize optimalBufferCopyRowPitchAlignment = 0x10;
    VkDeviceSize nonCoherentAtomSize = 64;
  } limits;
  ASSERT_ALWAYS(sizeof(limits) == sizeof(pProperties->limits));
  memset(pProperties, 0, sizeof(*pProperties));
  memcpy(&pProperties->limits, &limits, sizeof(VkPhysicalDeviceLimits));
  pProperties->deviceID = 0;
  pProperties->vendorID = 0;
  pProperties->apiVersion = VK_API_VERSION_1_2;
  char const dev_name[] = "NOICE_DEVOICE\0";
  memcpy(pProperties->deviceName, dev_name, sizeof(dev_name));
  pProperties->deviceType =
      VkPhysicalDeviceType::VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU;
  pProperties->driverVersion = 1;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateXcbSurfaceKHR(
    VkInstance instance, const VkXcbSurfaceCreateInfoKHR *pCreateInfo,
    const VkAllocationCallbacks *pAllocator, VkSurfaceKHR *pSurface) {
  NOTNULL(pCreateInfo);
  vki::VkSurfaceKHR_Impl *impl = ALLOC_VKOBJ_T(VkSurfaceKHR);
  impl->connection = pCreateInfo->connection;
  impl->window = pCreateInfo->window;
  *pSurface = (VkSurfaceKHR)impl;
  return VK_SUCCESS;
}

VKAPI_ATTR VkBool32 VKAPI_CALL vkGetPhysicalDeviceXcbPresentationSupportKHR(
    VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex,
    xcb_connection_t *connection, xcb_visualid_t visual_id) {
  return true;
}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceExtensionProperties(
    const char *pLayerName, uint32_t *pPropertyCount,
    VkExtensionProperties *pProperties) {

  static VkExtensionProperties props[] = {
      // clang-format off
      {VK_KHR_DEVICE_GROUP_CREATION_EXTENSION_NAME,             VK_KHR_DEVICE_GROUP_CREATION_SPEC_VERSION},
      {VK_KHR_EXTERNAL_FENCE_CAPABILITIES_EXTENSION_NAME,       VK_KHR_EXTERNAL_FENCE_CAPABILITIES_SPEC_VERSION},
      {VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME,      VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_SPEC_VERSION},
      {VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME,   VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_SPEC_VERSION},
      {VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,  VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_SPEC_VERSION},
      {VK_KHR_XCB_SURFACE_EXTENSION_NAME,                       VK_KHR_XCB_SURFACE_SPEC_VERSION},
      {VK_KHR_XLIB_SURFACE_EXTENSION_NAME,                      VK_KHR_XLIB_SURFACE_SPEC_VERSION},
      {VK_KHR_SURFACE_EXTENSION_NAME,                           VK_KHR_SURFACE_SPEC_VERSION},
      // clang-format on
  };
  NOTNULL(pPropertyCount);
  *pPropertyCount = sizeof(props) / sizeof(props[0]);
  if (pProperties == NULL)
    return VK_SUCCESS;
  ito(sizeof(props) / sizeof(props[0])) pProperties[i] = props[i];
  return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceExtensionProperties(
    VkPhysicalDevice physicalDevice, const char *pLayerName,
    uint32_t *pPropertyCount, VkExtensionProperties *pProperties) {
  static const VkExtensionProperties props[] = {
      // clang-format off
      {VK_KHR_DRIVER_PROPERTIES_EXTENSION_NAME,                 VK_KHR_DRIVER_PROPERTIES_SPEC_VERSION},
      {VK_EXT_LINE_RASTERIZATION_EXTENSION_NAME,                VK_EXT_LINE_RASTERIZATION_SPEC_VERSION},
      {VK_KHR_SWAPCHAIN_EXTENSION_NAME,                         VK_KHR_SWAPCHAIN_SPEC_VERSION},
      // clang-format on
  };
  NOTNULL(pPropertyCount);
  *pPropertyCount = sizeof(props) / sizeof(props[0]);
  if (pProperties == NULL)
    return VK_SUCCESS;
  ito(sizeof(props) / sizeof(props[0])) pProperties[i] = props[i];
  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceQueueFamilyProperties(
    VkPhysicalDevice physicalDevice, uint32_t *pQueueFamilyPropertyCount,
    VkQueueFamilyProperties *pQueueFamilyProperties) {
  (void)physicalDevice;
  if (pQueueFamilyPropertyCount != NULL && pQueueFamilyProperties == NULL) {
    *pQueueFamilyPropertyCount = 1;
    return;
  }
  if (*pQueueFamilyPropertyCount < 1)
    return;
  *pQueueFamilyPropertyCount = 1;
  pQueueFamilyProperties[0].queueCount = 2;
  pQueueFamilyProperties[0].queueFlags =
      VkQueueFlagBits::VK_QUEUE_COMPUTE_BIT |
      VkQueueFlagBits::VK_QUEUE_GRAPHICS_BIT |
      VkQueueFlagBits::VK_QUEUE_TRANSFER_BIT;
  return;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkEnumeratePhysicalDevices(VkInstance instance, uint32_t *pPhysicalDeviceCount,
                           VkPhysicalDevice *pPhysicalDevices) {
  if (pPhysicalDeviceCount != NULL && pPhysicalDevices == NULL) {
    *pPhysicalDeviceCount = 1;
    return VK_SUCCESS;
  }
  if (*pPhysicalDeviceCount < 1)
    return VK_INCOMPLETE;
  *pPhysicalDeviceCount = 1;
  *pPhysicalDevices = ALLOC_VKOBJ(VkPhysicalDevice);
  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vkDestroySurfaceKHR(VkInstance instance, VkSurfaceKHR surface,
                    const VkAllocationCallbacks *pAllocator) {
  RELEASE_VKOBJ(surface, VkSurfaceKHR);
}

// BEGIN DEVICE FUNCTIONS
VKAPI_ATTR VkResult VKAPI_CALL vkCreateInstance(
    const VkInstanceCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator, VkInstance *pInstance) {
  NOTNULL(pInstance);
  *pInstance = ALLOC_VKOBJ(VkInstance);
  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyInstance(
    VkInstance instance, const VkAllocationCallbacks *pAllocator) {
  RELEASE_VKOBJ(instance, VkInstance);
  return;
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFeatures(
    VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures *pFeatures) {
  struct {
    VkBool32 robustBufferAccess = VK_TRUE;
    VkBool32 fullDrawIndexUint32 = VK_TRUE;
    VkBool32 imageCubeArray = VK_TRUE;
    VkBool32 independentBlend = VK_TRUE;
    VkBool32 geometryShader = VK_TRUE;
    VkBool32 tessellationShader = VK_TRUE;
    VkBool32 sampleRateShading = VK_TRUE;
    VkBool32 dualSrcBlend = VK_TRUE;
    VkBool32 logicOp = VK_TRUE;
    VkBool32 multiDrawIndirect = VK_TRUE;
    VkBool32 drawIndirectFirstInstance = VK_TRUE;
    VkBool32 depthClamp = VK_TRUE;
    VkBool32 depthBiasClamp = VK_TRUE;
    VkBool32 fillModeNonSolid = VK_TRUE;
    VkBool32 depthBounds = VK_TRUE;
    VkBool32 wideLines = VK_TRUE;
    VkBool32 largePoints = VK_TRUE;
    VkBool32 alphaToOne = VK_TRUE;
    VkBool32 multiViewport = VK_TRUE;
    VkBool32 samplerAnisotropy = VK_TRUE;
    VkBool32 textureCompressionETC2 = VK_TRUE;
    VkBool32 textureCompressionASTC_LDR = VK_TRUE;
    VkBool32 textureCompressionBC = VK_TRUE;
    VkBool32 occlusionQueryPrecise = VK_TRUE;
    VkBool32 pipelineStatisticsQuery = VK_TRUE;
    VkBool32 vertexPipelineStoresAndAtomics = VK_TRUE;
    VkBool32 fragmentStoresAndAtomics = VK_TRUE;
    VkBool32 shaderTessellationAndGeometryPointSize = VK_TRUE;
    VkBool32 shaderImageGatherExtended = VK_TRUE;
    VkBool32 shaderStorageImageExtendedFormats = VK_TRUE;
    VkBool32 shaderStorageImageMultisample = VK_TRUE;
    VkBool32 shaderStorageImageReadWithoutFormat = VK_TRUE;
    VkBool32 shaderStorageImageWriteWithoutFormat = VK_TRUE;
    VkBool32 shaderUniformBufferArrayDynamicIndexing = VK_TRUE;
    VkBool32 shaderSampledImageArrayDynamicIndexing = VK_TRUE;
    VkBool32 shaderStorageBufferArrayDynamicIndexing = VK_TRUE;
    VkBool32 shaderStorageImageArrayDynamicIndexing = VK_TRUE;
    VkBool32 shaderClipDistance = VK_TRUE;
    VkBool32 shaderCullDistance = VK_TRUE;
    VkBool32 shaderFloat64 = VK_TRUE;
    VkBool32 shaderInt64 = VK_TRUE;
    VkBool32 shaderInt16 = VK_TRUE;
    VkBool32 shaderResourceResidency = VK_TRUE;
    VkBool32 shaderResourceMinLod = VK_TRUE;
    VkBool32 sparseBinding = VK_TRUE;
    VkBool32 sparseResidencyBuffer = VK_TRUE;
    VkBool32 sparseResidencyImage2D = VK_TRUE;
    VkBool32 sparseResidencyImage3D = VK_TRUE;
    VkBool32 sparseResidency2Samples = VK_TRUE;
    VkBool32 sparseResidency4Samples = VK_TRUE;
    VkBool32 sparseResidency8Samples = VK_TRUE;
    VkBool32 sparseResidency16Samples = VK_TRUE;
    VkBool32 sparseResidencyAliased = VK_TRUE;
    VkBool32 variableMultisampleRate = VK_TRUE;
    VkBool32 inheritedQueries = VK_TRUE;
  } features;
  NOTNULL(pFeatures);
  ASSERT_ALWAYS(sizeof(*pFeatures) == sizeof(features));
  memcpy(pFeatures, &features, sizeof(features));
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFormatProperties(
    VkPhysicalDevice physicalDevice, VkFormat format,
    VkFormatProperties *pFormatProperties) {
  NOTNULL(pFormatProperties);
  pFormatProperties->bufferFeatures =
      VkFormatFeatureFlagBits::VK_FORMAT_FEATURE_FLAG_BITS_MAX_ENUM;
  pFormatProperties->linearTilingFeatures =
      VkFormatFeatureFlagBits::VK_FORMAT_FEATURE_FLAG_BITS_MAX_ENUM;
  pFormatProperties->optimalTilingFeatures =
      VkFormatFeatureFlagBits::VK_FORMAT_FEATURE_FLAG_BITS_MAX_ENUM;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceImageFormatProperties(
    VkPhysicalDevice physicalDevice, VkFormat format, VkImageType type,
    VkImageTiling tiling, VkImageUsageFlags usage, VkImageCreateFlags flags,
    VkImageFormatProperties *pImageFormatProperties) {
  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceMemoryProperties(
    VkPhysicalDevice physicalDevice,
    VkPhysicalDeviceMemoryProperties *pMemoryProperties) {

  NOTNULL(pMemoryProperties);
  pMemoryProperties->memoryHeapCount = 1;
  pMemoryProperties->memoryTypeCount = 1;
  pMemoryProperties->memoryHeaps[0].size = (size_t)1 << 32;
  pMemoryProperties->memoryHeaps[0].flags =
      VkMemoryHeapFlagBits::VK_MEMORY_HEAP_DEVICE_LOCAL_BIT;
  pMemoryProperties->memoryTypes[0].heapIndex = 0;
  pMemoryProperties->memoryTypes[0].propertyFlags =
      // clang-format off
      VkMemoryPropertyFlagBits::VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT         |
      VkMemoryPropertyFlagBits::VK_MEMORY_PROPERTY_HOST_CACHED_BIT          |
      VkMemoryPropertyFlagBits::VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT         |
      VkMemoryPropertyFlagBits::VK_MEMORY_PROPERTY_HOST_COHERENT_BIT        |
      VkMemoryPropertyFlagBits::VK_MEMORY_PROPERTY_DEVICE_COHERENT_BIT_AMD  |
      // clang-format on
      0;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDevice(
    VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator, VkDevice *pDevice) {
  NOTNULL(pDevice);
  *pDevice = ALLOC_VKOBJ(VkDevice);
  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vkDestroyDevice(VkDevice device, const VkAllocationCallbacks *pAllocator) {
  RELEASE_VKOBJ(device, VkDevice);
}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceLayerProperties(
    uint32_t *pPropertyCount, VkLayerProperties *pProperties) {
  return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceLayerProperties(
    VkPhysicalDevice physicalDevice, uint32_t *pPropertyCount,
    VkLayerProperties *pProperties) {
  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkGetDeviceQueue(VkDevice device,
                                            uint32_t queueFamilyIndex,
                                            uint32_t queueIndex,
                                            VkQueue *pQueue) {
  NOTNULL(pQueue);
  *pQueue = ALLOC_VKOBJ(VkQueue);
}

VKAPI_ATTR VkResult VKAPI_CALL vkQueueSubmit(VkQueue queue,
                                             uint32_t submitCount,
                                             const VkSubmitInfo *pSubmits,
                                             VkFence fence) {
  ito(submitCount) {
    jto(pSubmits[i].commandBufferCount) {
      vki::VkCommandBuffer_Impl *impl =
          (vki::VkCommandBuffer_Impl *)pSubmits[i].pCommandBuffers[j];
      vki::cmd::g_gpu_state.execute_commands(impl);
    }
  }
  return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkQueueWaitIdle(VkQueue queue) {
  return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkDeviceWaitIdle(VkDevice device) {
  return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkAllocateMemory(
    VkDevice device, const VkMemoryAllocateInfo *pAllocateInfo,
    const VkAllocationCallbacks *pAllocator, VkDeviceMemory *pMemory) {
  vki::VkDeviceMemory_Impl *mem = ALLOC_VKOBJ_T(VkDeviceMemory);
  mem->size = pAllocateInfo->allocationSize;
  mem->ptr = (uint8_t *)malloc(mem->size);
  *pMemory = (VkDeviceMemory)mem;
  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vkFreeMemory(VkDevice device, VkDeviceMemory memory,
             const VkAllocationCallbacks *pAllocator) {
  vki::VkDeviceMemory_Impl *mem = (vki::VkDeviceMemory_Impl *)memory;
  mem->release();
}

VKAPI_ATTR VkResult VKAPI_CALL
vkMapMemory(VkDevice device, VkDeviceMemory memory, VkDeviceSize offset,
            VkDeviceSize size, VkMemoryMapFlags flags, void **ppData) {
  vki::VkDeviceMemory_Impl *mem = (vki::VkDeviceMemory_Impl *)memory;
  *ppData = (void *)(mem->ptr + offset);
  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkUnmapMemory(VkDevice device,
                                         VkDeviceMemory memory) {
  return;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkFlushMappedMemoryRanges(VkDevice device, uint32_t memoryRangeCount,
                          const VkMappedMemoryRange *pMemoryRanges) {
  return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkInvalidateMappedMemoryRanges(VkDevice device, uint32_t memoryRangeCount,
                               const VkMappedMemoryRange *pMemoryRanges) {
  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vkGetDeviceMemoryCommitment(VkDevice device, VkDeviceMemory memory,
                            VkDeviceSize *pCommittedMemoryInBytes) {
  return;
}

VKAPI_ATTR VkResult VKAPI_CALL vkBindBufferMemory(VkDevice device,
                                                  VkBuffer buffer,
                                                  VkDeviceMemory memory,
                                                  VkDeviceSize memoryOffset) {
  vki::VkBuffer_Impl *buf_impl = (vki::VkBuffer_Impl *)buffer;
  vki::VkDeviceMemory_Impl *mem_impl = (vki::VkDeviceMemory_Impl *)memory;
  NOTNULL(buf_impl);
  NOTNULL(mem_impl);
  buf_impl->mem = mem_impl;
  mem_impl->refcnt++;
  buf_impl->offset = memoryOffset;
  return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkBindImageMemory(VkDevice device, VkImage image,
                                                 VkDeviceMemory memory,
                                                 VkDeviceSize memoryOffset) {
  vki::VkDeviceMemory_Impl *mem = (vki::VkDeviceMemory_Impl *)memory;
  vki::VkImage_Impl *impl = (vki::VkImage_Impl *)image;
  impl->mem = mem;
  mem->refcnt++;
  impl->offset = memoryOffset;
  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vkGetBufferMemoryRequirements(VkDevice device, VkBuffer buffer,
                              VkMemoryRequirements *pMemoryRequirements) {
  vki::VkBuffer_Impl *impl = (vki::VkBuffer_Impl *)buffer;
  pMemoryRequirements->size = impl->size;
  pMemoryRequirements->alignment = 0x10;
  pMemoryRequirements->memoryTypeBits =
      VkMemoryPropertyFlagBits::VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
}

VKAPI_ATTR void VKAPI_CALL vkGetImageMemoryRequirements(
    VkDevice device, VkImage image, VkMemoryRequirements *pMemoryRequirements) {
  vki::VkImage_Impl *impl = (vki::VkImage_Impl *)image;
  pMemoryRequirements->size = impl->size;
  pMemoryRequirements->alignment = 0x10;
  pMemoryRequirements->memoryTypeBits = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT;
  return;
}

VKAPI_ATTR void VKAPI_CALL vkGetImageSparseMemoryRequirements(
    VkDevice device, VkImage image, uint32_t *pSparseMemoryRequirementCount,
    VkSparseImageMemoryRequirements *pSparseMemoryRequirements) {
  return;
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceSparseImageFormatProperties(
    VkPhysicalDevice physicalDevice, VkFormat format, VkImageType type,
    VkSampleCountFlagBits samples, VkImageUsageFlags usage,
    VkImageTiling tiling, uint32_t *pPropertyCount,
    VkSparseImageFormatProperties *pProperties) {
  return;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkQueueBindSparse(VkQueue queue, uint32_t bindInfoCount,
                  const VkBindSparseInfo *pBindInfo, VkFence fence) {
  return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkCreateFence(VkDevice device, const VkFenceCreateInfo *pCreateInfo,
              const VkAllocationCallbacks *pAllocator, VkFence *pFence) {
  *pFence = ALLOC_VKOBJ(VkFence);
  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyFence(
    VkDevice device, VkFence fence, const VkAllocationCallbacks *pAllocator) {
  RELEASE_VKOBJ(fence, VkFence);
  return;
}

VKAPI_ATTR VkResult VKAPI_CALL vkResetFences(VkDevice device,
                                             uint32_t fenceCount,
                                             const VkFence *pFences) {
  return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetFenceStatus(VkDevice device,
                                                VkFence fence) {
  return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkWaitForFences(VkDevice device,
                                               uint32_t fenceCount,
                                               const VkFence *pFences,
                                               VkBool32 waitAll,
                                               uint64_t timeout) {
  return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateSemaphore(
    VkDevice device, const VkSemaphoreCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator, VkSemaphore *pSemaphore) {
  *pSemaphore = ALLOC_VKOBJ(VkSemaphore);
  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vkDestroySemaphore(VkDevice device, VkSemaphore semaphore,
                   const VkAllocationCallbacks *pAllocator) {
  RELEASE_VKOBJ(semaphore, VkSemaphore);
  return;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkCreateEvent(VkDevice device, const VkEventCreateInfo *pCreateInfo,
              const VkAllocationCallbacks *pAllocator, VkEvent *pEvent) {
  *pEvent = ALLOC_VKOBJ(VkEvent);
  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyEvent(
    VkDevice device, VkEvent event, const VkAllocationCallbacks *pAllocator) {
  RELEASE_VKOBJ(event, VkEvent);
  return;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetEventStatus(VkDevice device,
                                                VkEvent event) {
  return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkSetEvent(VkDevice device, VkEvent event) {
  return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkResetEvent(VkDevice device, VkEvent event) {
  return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateQueryPool(
    VkDevice device, const VkQueryPoolCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator, VkQueryPool *pQueryPool) {
  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vkDestroyQueryPool(VkDevice device, VkQueryPool queryPool,
                   const VkAllocationCallbacks *pAllocator) {
  return;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetQueryPoolResults(
    VkDevice device, VkQueryPool queryPool, uint32_t firstQuery,
    uint32_t queryCount, size_t dataSize, void *pData, VkDeviceSize stride,
    VkQueryResultFlags flags) {
  return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkCreateBuffer(VkDevice device, const VkBufferCreateInfo *pCreateInfo,
               const VkAllocationCallbacks *pAllocator, VkBuffer *pBuffer) {
  vki::VkBuffer_Impl *buf = ALLOC_VKOBJ_T(VkBuffer);
  buf->mem = NULL;
  buf->size = pCreateInfo->size;
  buf->offset = 0;
  *pBuffer = (VkBuffer)(void *)buf;
  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyBuffer(
    VkDevice device, VkBuffer buffer, const VkAllocationCallbacks *pAllocator) {
  RELEASE_VKOBJ(buffer, VkBuffer);
  return;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateBufferView(
    VkDevice device, const VkBufferViewCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator, VkBufferView *pView) {
  vki::VkBufferView_Impl *impl = ALLOC_VKOBJ_T(VkBufferView);
  impl->buf = (vki::VkBuffer_Impl *)pCreateInfo->buffer;
  impl->buf->refcnt++;
  impl->format = pCreateInfo->format;
  impl->offset = pCreateInfo->offset;
  *pView = (VkBufferView)impl;
  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vkDestroyBufferView(VkDevice device, VkBufferView bufferView,
                    const VkAllocationCallbacks *pAllocator) {
  RELEASE_VKOBJ(bufferView, VkBufferView);
}

VKAPI_ATTR VkResult VKAPI_CALL
vkCreateImage(VkDevice device, const VkImageCreateInfo *pCreateInfo,
              const VkAllocationCallbacks *pAllocator, VkImage *pImage) {
  vki::VkImage_Impl *img = ALLOC_VKOBJ_T(VkImage);
  img->mem = NULL;
  img->size = 0;
  img->offset = 0;
  img->format = pCreateInfo->format;
  img->extent = pCreateInfo->extent;
  img->mipLevels = pCreateInfo->mipLevels;
  img->arrayLayers = pCreateInfo->arrayLayers;
  img->samples = pCreateInfo->samples;
  img->initialLayout = pCreateInfo->initialLayout;
  img->size = img->extent.width * img->extent.height * img->extent.depth *
              get_format_bpp(img->format);
  *pImage = (VkImage)(void *)img;
  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyImage(
    VkDevice device, VkImage image, const VkAllocationCallbacks *pAllocator) {
  RELEASE_VKOBJ(image, VkImage);
}

VKAPI_ATTR void VKAPI_CALL vkGetImageSubresourceLayout(
    VkDevice device, VkImage image, const VkImageSubresource *pSubresource,
    VkSubresourceLayout *pLayout) {
  return;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkCreateImageView(VkDevice device, const VkImageViewCreateInfo *pCreateInfo,
                  const VkAllocationCallbacks *pAllocator, VkImageView *pView) {
  vki::VkImage_Impl *img = (vki::VkImage_Impl *)pCreateInfo->image;
  vki::VkImageView_Impl *img_view = ALLOC_VKOBJ_T(VkImageView);
  img_view->img = img;
  img->refcnt++;
  img_view->type = pCreateInfo->viewType;
  img_view->format = pCreateInfo->format;
  img_view->components = pCreateInfo->components;
  *pView = (VkImageView)(void *)img_view;
  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vkDestroyImageView(VkDevice device, VkImageView imageView,
                   const VkAllocationCallbacks *pAllocator) {
  ((vki::VkImageView_Impl *)imageView)->release();
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateShaderModule(
    VkDevice device, const VkShaderModuleCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator, VkShaderModule *pShaderModule) {
  vki::VkShaderModule_Impl *impl = ALLOC_VKOBJ_T(VkShaderModule);
  impl->init(pCreateInfo->pCode, pCreateInfo->codeSize);
  *pShaderModule = (VkShaderModule)(void *)impl;
  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vkDestroyShaderModule(VkDevice device, VkShaderModule shaderModule,
                      const VkAllocationCallbacks *pAllocator) {
  RELEASE_VKOBJ(shaderModule, VkShaderModule);
  return;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreatePipelineCache(
    VkDevice device, const VkPipelineCacheCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator, VkPipelineCache *pPipelineCache) {
  *pPipelineCache = ALLOC_VKOBJ(VkPipelineCache);
  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vkDestroyPipelineCache(VkDevice device, VkPipelineCache pipelineCache,
                       const VkAllocationCallbacks *pAllocator) {
  ((vki::VkPipelineCache_Impl *)pipelineCache)->release();
  return;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkGetPipelineCacheData(VkDevice device, VkPipelineCache pipelineCache,
                       size_t *pDataSize, void *pData) {
  *pDataSize = 4;
  if (pData != NULL)
    ((uint32_t *)pData)[0] = 0xdeadbeef;
  return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkMergePipelineCaches(
    VkDevice device, VkPipelineCache dstCache, uint32_t srcCacheCount,
    const VkPipelineCache *pSrcCaches) {
  return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateGraphicsPipelines(
    VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount,
    const VkGraphicsPipelineCreateInfo *pCreateInfos,
    const VkAllocationCallbacks *pAllocator, VkPipeline *pPipelines) {

  ito(createInfoCount) {
    vki::VkPipeline_Impl *impl = ALLOC_VKOBJ_T(VkPipeline);
    VkGraphicsPipelineCreateInfo info = pCreateInfos[i];
    impl->is_graphics = true;
    jto(info.stageCount) {
      switch (info.pStages[j].stage) {
      case VkShaderStageFlagBits::VK_SHADER_STAGE_VERTEX_BIT: {
        impl->vs = (vki::VkShaderModule_Impl *)info.pStages[j].module;
        impl->vs->refcnt++;
        ASSERT_ALWAYS(strcmp(info.pStages[j].pName, "main") == 0);
        break;
      }
      case VkShaderStageFlagBits::VK_SHADER_STAGE_FRAGMENT_BIT: {
        impl->ps = (vki::VkShaderModule_Impl *)info.pStages[j].module;
        impl->ps->refcnt++;
        ASSERT_ALWAYS(strcmp(info.pStages[j].pName, "main") == 0);
        break;
      }
      default:
        ASSERT_ALWAYS(false);
      }
    }
    impl->IA_bindings.vertexBindingDescriptionCount =
        info.pVertexInputState->vertexBindingDescriptionCount;
    impl->IA_bindings.vertexAttributeDescriptionCount =
        info.pVertexInputState->vertexAttributeDescriptionCount;
    jto(impl->IA_bindings.vertexBindingDescriptionCount)
        impl->IA_bindings.pVertexBindingDescriptions[j] =
        info.pVertexInputState->pVertexBindingDescriptions[j];
    jto(impl->IA_bindings.vertexAttributeDescriptionCount)
        impl->IA_bindings.pVertexAttributeDescriptions[j] =
        info.pVertexInputState->pVertexAttributeDescriptions[j];
    impl->IA_topology = info.pInputAssemblyState->topology;
    impl->RS_viewports.viewportCount = info.pViewportState->viewportCount;
    impl->RS_viewports.scissorCount = info.pViewportState->scissorCount;
    if (info.pViewportState->pViewports != NULL) {
      jto(impl->RS_viewports.viewportCount) impl->RS_viewports.pViewports[j] =
          info.pViewportState->pViewports[j];
    }
    if (info.pViewportState->pScissors != NULL) {
      jto(impl->RS_viewports.scissorCount) impl->RS_viewports.pScissors[j] =
          info.pViewportState->pScissors[j];
    }
    impl->RS_state.depthClampEnable =
        info.pRasterizationState->depthClampEnable;
    impl->RS_state.rasterizerDiscardEnable =
        info.pRasterizationState->rasterizerDiscardEnable;
    impl->RS_state.polygonMode = info.pRasterizationState->polygonMode;
    impl->RS_state.cullMode = info.pRasterizationState->cullMode;
    impl->RS_state.frontFace = info.pRasterizationState->frontFace;
    impl->RS_state.depthBiasEnable = info.pRasterizationState->depthBiasEnable;
    impl->RS_state.depthBiasConstantFactor =
        info.pRasterizationState->depthBiasConstantFactor;
    impl->RS_state.depthBiasClamp = info.pRasterizationState->depthBiasClamp;
    impl->RS_state.depthBiasSlopeFactor =
        info.pRasterizationState->depthBiasSlopeFactor;
    impl->RS_state.lineWidth = info.pRasterizationState->lineWidth;

    impl->DS_state.depthTestEnable = info.pDepthStencilState->depthTestEnable;
    impl->DS_state.depthWriteEnable = info.pDepthStencilState->depthWriteEnable;
    impl->DS_state.depthCompareOp = info.pDepthStencilState->depthCompareOp;
    impl->DS_state.depthBoundsTestEnable =
        info.pDepthStencilState->depthBoundsTestEnable;
    impl->DS_state.stencilTestEnable =
        info.pDepthStencilState->stencilTestEnable;
    impl->DS_state.front = info.pDepthStencilState->front;
    impl->DS_state.back = info.pDepthStencilState->back;
    impl->DS_state.minDepthBounds = info.pDepthStencilState->minDepthBounds;
    impl->DS_state.maxDepthBounds = info.pDepthStencilState->maxDepthBounds;

    impl->OM_blend_state.logicOp = info.pColorBlendState->logicOp;
    impl->OM_blend_state.logicOpEnable = info.pColorBlendState->logicOpEnable;
    impl->OM_blend_state.attachmentCount =
        info.pColorBlendState->attachmentCount;
    jto(impl->OM_blend_state.attachmentCount)
        impl->OM_blend_state.pAttachments[j] =
        info.pColorBlendState->pAttachments[j];
    jto(4) impl->OM_blend_state.blendConstants[j] =
        info.pColorBlendState->blendConstants[j];
    impl->layout = (vki::VkPipelineLayout_Impl *)info.layout;
    impl->layout->refcnt++;
    impl->renderPass = (vki::VkRenderPass_Impl *)info.renderPass;
    impl->renderPass->refcnt++;
    impl->subpass = info.subpass;
    ASSERT_ALWAYS(impl->subpass == 0);
    ASSERT_ALWAYS(info.pMultisampleState->rasterizationSamples ==
                  VkSampleCountFlagBits::VK_SAMPLE_COUNT_1_BIT);
    pPipelines[i] = (VkPipeline)impl;
  }
  return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateComputePipelines(
    VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount,
    const VkComputePipelineCreateInfo *pCreateInfos,
    const VkAllocationCallbacks *pAllocator, VkPipeline *pPipelines) {

  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vkDestroyPipeline(VkDevice device, VkPipeline pipeline,
                  const VkAllocationCallbacks *pAllocator) {
  RELEASE_VKOBJ(pipeline, VkPipeline);
  return;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreatePipelineLayout(
    VkDevice device, const VkPipelineLayoutCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator,
    VkPipelineLayout *pPipelineLayout) {
  vki::VkPipelineLayout_Impl *impl = ALLOC_VKOBJ_T(VkPipelineLayout);
  impl->setLayoutCount = pCreateInfo->setLayoutCount;
  impl->pushConstantRangeCount = pCreateInfo->pushConstantRangeCount;
  ASSERT_ALWAYS(impl->setLayoutCount < 0x10);
  ASSERT_ALWAYS(impl->pushConstantRangeCount < 0x10);
  ito(impl->setLayoutCount) {
    impl->pSetLayouts[i] =
        (vki::VkDescriptorSetLayout_Impl *)pCreateInfo->pSetLayouts[i];
    impl->pSetLayouts[i]->refcnt++;
  }
  ito(impl->pushConstantRangeCount) {
    impl->pPushConstantRanges[i] = pCreateInfo->pPushConstantRanges[i];
  }
  *pPipelineLayout = (VkPipelineLayout)impl;
  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vkDestroyPipelineLayout(VkDevice device, VkPipelineLayout pipelineLayout,
                        const VkAllocationCallbacks *pAllocator) {
  RELEASE_VKOBJ(pipelineLayout, VkPipelineLayout);
  return;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkCreateSampler(VkDevice device, const VkSamplerCreateInfo *pCreateInfo,
                const VkAllocationCallbacks *pAllocator, VkSampler *pSampler) {
  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vkDestroySampler(VkDevice device, VkSampler sampler,
                 const VkAllocationCallbacks *pAllocator) {
  return;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDescriptorSetLayout(
    VkDevice device, const VkDescriptorSetLayoutCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator,
    VkDescriptorSetLayout *pSetLayout) {
  vki::VkDescriptorSetLayout_Impl *impl = ALLOC_VKOBJ_T(VkDescriptorSetLayout);
  impl->bindingCount = pCreateInfo->bindingCount;
  impl->pBindings = (VkDescriptorSetLayoutBinding *)malloc(
      sizeof(VkDescriptorSetLayoutBinding) * impl->bindingCount);
  memcpy(impl->pBindings, pCreateInfo->pBindings,
         sizeof(VkDescriptorSetLayoutBinding) * impl->bindingCount);
  *pSetLayout = (VkDescriptorSetLayout)impl;
  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyDescriptorSetLayout(
    VkDevice device, VkDescriptorSetLayout descriptorSetLayout,
    const VkAllocationCallbacks *pAllocator) {
  RELEASE_VKOBJ(descriptorSetLayout, VkDescriptorSetLayout);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDescriptorPool(
    VkDevice device, const VkDescriptorPoolCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator,
    VkDescriptorPool *pDescriptorPool) {
  vki::VkDescriptorPool_Impl *impl = ALLOC_VKOBJ_T(VkDescriptorPool);
  impl->maxSets = pCreateInfo->maxSets;
  impl->poolSizeCount = pCreateInfo->poolSizeCount;
  impl->pPoolSizes = (VkDescriptorPoolSize *)malloc(
      sizeof(VkDescriptorPoolSize) * impl->poolSizeCount);
  ito(impl->poolSizeCount) impl->pPoolSizes[i] = pCreateInfo->pPoolSizes[i];
  *pDescriptorPool = (VkDescriptorPool)impl;
  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vkDestroyDescriptorPool(VkDevice device, VkDescriptorPool descriptorPool,
                        const VkAllocationCallbacks *pAllocator) {
  RELEASE_VKOBJ(descriptorPool, VkDescriptorPool);
}

VKAPI_ATTR VkResult VKAPI_CALL
vkResetDescriptorPool(VkDevice device, VkDescriptorPool descriptorPool,
                      VkDescriptorPoolResetFlags flags) {
  return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkAllocateDescriptorSets(
    VkDevice device, const VkDescriptorSetAllocateInfo *pAllocateInfo,
    VkDescriptorSet *pDescriptorSets) {
  ito(pAllocateInfo->descriptorSetCount) {
    vki::VkDescriptorSet_Impl *impl = ALLOC_VKOBJ_T(VkDescriptorSet);
    impl->pool = (vki::VkDescriptorPool_Impl *)pAllocateInfo->descriptorPool;
    impl->pool->refcnt++;
    impl->layout =
        (vki::VkDescriptorSetLayout_Impl *)pAllocateInfo->pSetLayouts[i];
    impl->layout->refcnt++;
    impl->slots = (vki::VkDescriptorSet_Impl::Slot *)malloc(
        sizeof(vki::VkDescriptorSet_Impl::Slot) * impl->layout->bindingCount);
    memset(impl->slots, 0,
           sizeof(vki::VkDescriptorSet_Impl::Slot) *
               impl->layout->bindingCount);
    pDescriptorSets[i] = (VkDescriptorSet)impl;
  }
  return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkFreeDescriptorSets(
    VkDevice device, VkDescriptorPool descriptorPool,
    uint32_t descriptorSetCount, const VkDescriptorSet *pDescriptorSets) {
  ito(descriptorSetCount) {
    RELEASE_VKOBJ((VkDescriptorSet)pDescriptorSets[i], VkDescriptorSet);
  }
  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkUpdateDescriptorSets(
    VkDevice device, uint32_t descriptorWriteCount,
    const VkWriteDescriptorSet *pDescriptorWrites, uint32_t descriptorCopyCount,
    const VkCopyDescriptorSet *pDescriptorCopies) {
  ito(descriptorWriteCount) {
    VkWriteDescriptorSet write = pDescriptorWrites[i];
    vki::VkDescriptorSet_Impl *impl = (vki::VkDescriptorSet_Impl *)write.dstSet;
    ASSERT_ALWAYS(write.descriptorCount == 1);
    ASSERT_ALWAYS(write.dstArrayElement == 0);
    impl->slots[write.dstBinding].type = write.descriptorType;
    if (write.pImageInfo != NULL) {
      if (impl->slots[write.dstBinding].sampler != NULL)
        impl->slots[write.dstBinding].sampler->release();
      impl->slots[write.dstBinding].sampler =
          (vki::VkSampler_Impl *)write.pImageInfo->sampler;
      impl->slots[write.dstBinding].sampler->refcnt++;
      if (impl->slots[write.dstBinding].image_view != NULL)
        impl->slots[write.dstBinding].image_view->release();
      impl->slots[write.dstBinding].image_view =
          (vki::VkImageView_Impl *)write.pImageInfo->imageView;
      impl->slots[write.dstBinding].image_view->refcnt++;
    }
    if (write.pBufferInfo != NULL) {
      if (impl->slots[write.dstBinding].buffer != NULL)
        impl->slots[write.dstBinding].buffer->release();
      impl->slots[write.dstBinding].buffer =
          (vki::VkBuffer_Impl *)write.pBufferInfo->buffer;
      impl->slots[write.dstBinding].buffer->refcnt++;
      impl->slots[write.dstBinding].range = write.pBufferInfo->range;
      impl->slots[write.dstBinding].offset = write.pBufferInfo->offset;
    }
    if (write.pTexelBufferView != NULL) {
      if (impl->slots[write.dstBinding].buf_view != NULL)
        impl->slots[write.dstBinding].buf_view->release();
      impl->slots[write.dstBinding].buf_view =
          (vki::VkBufferView_Impl *)write.pTexelBufferView;
      impl->slots[write.dstBinding].buf_view->refcnt++;
    }
  }
  ASSERT_ALWAYS(pDescriptorCopies == NULL);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateFramebuffer(
    VkDevice device, const VkFramebufferCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator, VkFramebuffer *pFramebuffer) {
  vki::VkFramebuffer_Impl *impl = ALLOC_VKOBJ_T(VkFramebuffer);
  impl->flags = pCreateInfo->flags;
  impl->width = pCreateInfo->width;
  impl->height = pCreateInfo->height;
  impl->layers = pCreateInfo->layers;
  impl->attachmentCount = pCreateInfo->attachmentCount;
  ASSERT_ALWAYS(impl->attachmentCount < 0x10);
  ito(impl->attachmentCount) {
    impl->pAttachments[i] =
        (vki::VkImageView_Impl *)pCreateInfo->pAttachments[i];
    impl->pAttachments[i]->refcnt++;
  }
  impl->renderPass = (vki::VkRenderPass_Impl *)pCreateInfo->renderPass;
  impl->renderPass->refcnt++;
  *pFramebuffer = (VkFramebuffer)impl;
  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vkDestroyFramebuffer(VkDevice device, VkFramebuffer framebuffer,
                     const VkAllocationCallbacks *pAllocator) {
  RELEASE_VKOBJ(framebuffer, VkFramebuffer);
  return;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateRenderPass(
    VkDevice device, const VkRenderPassCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator, VkRenderPass *pRenderPass) {
  vki::VkRenderPass_Impl *impl = ALLOC_VKOBJ_T(VkRenderPass);
  impl->attachmentCount = pCreateInfo->attachmentCount;
  ASSERT_ALWAYS(impl->attachmentCount < 10);
  memcpy(impl->pAttachments, pCreateInfo->pAttachments,
         sizeof(impl->pAttachments[0]) * impl->attachmentCount);
  impl->subpassCount = pCreateInfo->subpassCount;
  ASSERT_ALWAYS(impl->subpassCount < 10);
  ito(impl->subpassCount) {
    vki::VkRenderPass_Impl::VkSubpassDescription_Impl *simpl =
        &impl->pSubpasses[i];
    simpl->flags = pCreateInfo->pSubpasses[i].flags;
    simpl->colorAttachmentCount =
        pCreateInfo->pSubpasses[i].colorAttachmentCount;
    simpl->inputAttachmentCount =
        pCreateInfo->pSubpasses[i].inputAttachmentCount;
    simpl->pipelineBindPoint = pCreateInfo->pSubpasses[i].pipelineBindPoint;
    simpl->preserveAttachmentCount =
        pCreateInfo->pSubpasses[i].preserveAttachmentCount;
    jto(simpl->preserveAttachmentCount) simpl->pPreserveAttachments[j] =
        pCreateInfo->pSubpasses[i].pPreserveAttachments[j];
    jto(simpl->colorAttachmentCount) simpl->pColorAttachments[j] =
        pCreateInfo->pSubpasses[i].pColorAttachments[j];
    jto(simpl->inputAttachmentCount) simpl->pInputAttachments[j] =
        pCreateInfo->pSubpasses[i].pInputAttachments[j];
    ASSERT_ALWAYS(pCreateInfo->pSubpasses[i].pResolveAttachments == NULL);
    if (pCreateInfo->pSubpasses[i].pDepthStencilAttachment != NULL) {
      simpl->has_depth_stencil_attachment = true;
      simpl->pDepthStencilAttachment =
          pCreateInfo->pSubpasses[i].pDepthStencilAttachment[0];
    }
  }
  impl->dependencyCount = pCreateInfo->dependencyCount;
  ASSERT_ALWAYS(impl->dependencyCount < 10);
  memcpy(impl->pDependencies, pCreateInfo->pDependencies,
         sizeof(impl->pDependencies[0]) * impl->dependencyCount);
  *pRenderPass = (VkRenderPass)(void *)impl;
  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vkDestroyRenderPass(VkDevice device, VkRenderPass renderPass,
                    const VkAllocationCallbacks *pAllocator) {
  RELEASE_VKOBJ(renderPass, VkRenderPass);
  return;
}

VKAPI_ATTR void VKAPI_CALL vkGetRenderAreaGranularity(
    VkDevice device, VkRenderPass renderPass, VkExtent2D *pGranularity) {
  return;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateCommandPool(
    VkDevice device, const VkCommandPoolCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator, VkCommandPool *pCommandPool) {
  *pCommandPool = ALLOC_VKOBJ(VkCommandPool);
  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vkDestroyCommandPool(VkDevice device, VkCommandPool commandPool,
                     const VkAllocationCallbacks *pAllocator) {
  RELEASE_VKOBJ(commandPool, VkCommandPool);
  return;
}

VKAPI_ATTR VkResult VKAPI_CALL vkResetCommandPool(
    VkDevice device, VkCommandPool commandPool, VkCommandPoolResetFlags flags) {
  return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkAllocateCommandBuffers(
    VkDevice device, const VkCommandBufferAllocateInfo *pAllocateInfo,
    VkCommandBuffer *pCommandBuffers) {
  ito(pAllocateInfo->commandBufferCount) {
    vki::VkCommandBuffer_Impl *impl = ALLOC_VKOBJ_T(VkCommandBuffer);
    impl->init();
    pCommandBuffers[i] = (VkCommandBuffer)impl;
  }
  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkFreeCommandBuffers(
    VkDevice device, VkCommandPool commandPool, uint32_t commandBufferCount,
    const VkCommandBuffer *pCommandBuffers) {
  ito(commandBufferCount) {
    vki::VkCommandBuffer_Impl *impl =
        (vki::VkCommandBuffer_Impl *)pCommandBuffers[i];
    impl->release();
  }
  return;
}

VKAPI_ATTR VkResult VKAPI_CALL vkBeginCommandBuffer(
    VkCommandBuffer commandBuffer, const VkCommandBufferBeginInfo *pBeginInfo) {
  vki::VkCommandBuffer_Impl *impl = (vki::VkCommandBuffer_Impl *)commandBuffer;
  (void)pBeginInfo->flags;
  impl->reset();
  return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkEndCommandBuffer(VkCommandBuffer commandBuffer) {
  return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkResetCommandBuffer(
    VkCommandBuffer commandBuffer, VkCommandBufferResetFlags flags) {
  vki::VkCommandBuffer_Impl *impl = (vki::VkCommandBuffer_Impl *)commandBuffer;
  impl->reset();
  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vkCmdBindPipeline(VkCommandBuffer commandBuffer,
                  VkPipelineBindPoint pipelineBindPoint, VkPipeline pipeline) {

  vki::VkCommandBuffer_Impl *impl = (vki::VkCommandBuffer_Impl *)commandBuffer;
  vki::cmd::BindPipeline cmd;
  cmd.pipeline = (vki::VkPipeline_Impl *)pipeline;
  cmd.pipelineBindPoint = pipelineBindPoint;
  WRITE_CMD(impl, cmd, BindPipeline);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetViewport(VkCommandBuffer commandBuffer,
                                            uint32_t firstViewport,
                                            uint32_t viewportCount,
                                            const VkViewport *pViewports) {
  vki::VkCommandBuffer_Impl *impl = (vki::VkCommandBuffer_Impl *)commandBuffer;
  vki::cmd::SetViewport cmd;
  cmd.firstViewport = firstViewport;
  cmd.viewportCount = viewportCount;
  ASSERT_ALWAYS(viewportCount < 0x10);
  ito(viewportCount) cmd.pViewports[i] = pViewports[i];
  WRITE_CMD(impl, cmd, SetViewport);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetScissor(VkCommandBuffer commandBuffer,
                                           uint32_t firstScissor,
                                           uint32_t scissorCount,
                                           const VkRect2D *pScissors) {
  vki::VkCommandBuffer_Impl *impl = (vki::VkCommandBuffer_Impl *)commandBuffer;
  vki::cmd::SetScissor cmd;
  cmd.firstScissor = firstScissor;
  cmd.scissorCount = scissorCount;
  ASSERT_ALWAYS(scissorCount < 0x10);
  ito(scissorCount) cmd.pScissors[i] = pScissors[i];
  WRITE_CMD(impl, cmd, SetScissor);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetLineWidth(VkCommandBuffer commandBuffer,
                                             float lineWidth) {
  vki::VkCommandBuffer_Impl *impl = (vki::VkCommandBuffer_Impl *)commandBuffer;
  vki::cmd::SetLineWidth cmd;
  cmd.lineWidth = lineWidth;
  WRITE_CMD(impl, cmd, SetLineWidth);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthBias(VkCommandBuffer commandBuffer,
                                             float depthBiasConstantFactor,
                                             float depthBiasClamp,
                                             float depthBiasSlopeFactor) {
  vki::VkCommandBuffer_Impl *impl = (vki::VkCommandBuffer_Impl *)commandBuffer;
  vki::cmd::SetDepthBias cmd;
  cmd.depthBiasConstantFactor = depthBiasConstantFactor;
  cmd.depthBiasClamp = depthBiasClamp;
  cmd.depthBiasSlopeFactor = depthBiasSlopeFactor;
  WRITE_CMD(impl, cmd, SetDepthBias);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetBlendConstants(
    VkCommandBuffer commandBuffer, const float blendConstants[4]) {
  vki::VkCommandBuffer_Impl *impl = (vki::VkCommandBuffer_Impl *)commandBuffer;
  vki::cmd::SetBlendConstants cmd;
  ito(4) cmd.blendConstants[i] = blendConstants[i];
  WRITE_CMD(impl, cmd, SetBlendConstants);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthBounds(VkCommandBuffer commandBuffer,
                                               float minDepthBounds,
                                               float maxDepthBounds) {
  vki::VkCommandBuffer_Impl *impl = (vki::VkCommandBuffer_Impl *)commandBuffer;
  vki::cmd::SetDepthBounds cmd;
  cmd.minDepthBounds = minDepthBounds;
  cmd.maxDepthBounds = maxDepthBounds;
  WRITE_CMD(impl, cmd, SetDepthBounds);
}

VKAPI_ATTR void VKAPI_CALL
vkCmdSetStencilCompareMask(VkCommandBuffer commandBuffer,
                           VkStencilFaceFlags faceMask, uint32_t compareMask) {
  vki::VkCommandBuffer_Impl *impl = (vki::VkCommandBuffer_Impl *)commandBuffer;
  vki::cmd::SetStencilCompareMask cmd;
  cmd.faceMask = faceMask;
  cmd.compareMask = compareMask;
  WRITE_CMD(impl, cmd, SetStencilCompareMask);
}

VKAPI_ATTR void VKAPI_CALL
vkCmdSetStencilWriteMask(VkCommandBuffer commandBuffer,
                         VkStencilFaceFlags faceMask, uint32_t writeMask) {
  vki::VkCommandBuffer_Impl *impl = (vki::VkCommandBuffer_Impl *)commandBuffer;
  vki::cmd::SetStencilWriteMask cmd;
  cmd.faceMask = faceMask;
  cmd.writeMask = writeMask;
  WRITE_CMD(impl, cmd, SetStencilWriteMask);
}

VKAPI_ATTR void VKAPI_CALL
vkCmdSetStencilReference(VkCommandBuffer commandBuffer,
                         VkStencilFaceFlags faceMask, uint32_t reference) {
  vki::VkCommandBuffer_Impl *impl = (vki::VkCommandBuffer_Impl *)commandBuffer;
  vki::cmd::SetStencilReference cmd;
  cmd.faceMask = faceMask;
  cmd.reference = reference;
  WRITE_CMD(impl, cmd, SetStencilReference);
}

VKAPI_ATTR void VKAPI_CALL vkCmdBindDescriptorSets(
    VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint,
    VkPipelineLayout layout, uint32_t firstSet, uint32_t descriptorSetCount,
    const VkDescriptorSet *pDescriptorSets, uint32_t dynamicOffsetCount,
    const uint32_t *pDynamicOffsets) {
  vki::VkCommandBuffer_Impl *impl = (vki::VkCommandBuffer_Impl *)commandBuffer;
  vki::cmd::BindDescriptorSets cmd;
  cmd.pipelineBindPoint = pipelineBindPoint;
  cmd.layout = (vki::VkPipelineLayout_Impl *)layout;
  cmd.firstSet = firstSet;
  cmd.descriptorSetCount = descriptorSetCount;
  ASSERT_ALWAYS(descriptorSetCount < 0x10);
  ito(descriptorSetCount) cmd.pDescriptorSets[i] =
      (vki::VkDescriptorSet_Impl *)pDescriptorSets[i];
  cmd.dynamicOffsetCount = dynamicOffsetCount;
  ASSERT_ALWAYS(dynamicOffsetCount < 0x10);
  ito(dynamicOffsetCount) cmd.pDynamicOffsets[i] = pDynamicOffsets[i];
  WRITE_CMD(impl, cmd, BindDescriptorSets);
}

VKAPI_ATTR void VKAPI_CALL vkCmdBindIndexBuffer(VkCommandBuffer commandBuffer,
                                                VkBuffer buffer,
                                                VkDeviceSize offset,
                                                VkIndexType indexType) {
  vki::VkCommandBuffer_Impl *impl = (vki::VkCommandBuffer_Impl *)commandBuffer;
  vki::cmd::BindIndexBuffer cmd;
  cmd.buffer = (vki::VkBuffer_Impl *)buffer;
  cmd.offset = offset;
  cmd.indexType = indexType;
  WRITE_CMD(impl, cmd, BindIndexBuffer);
}

VKAPI_ATTR void VKAPI_CALL vkCmdBindVertexBuffers(
    VkCommandBuffer commandBuffer, uint32_t firstBinding, uint32_t bindingCount,
    const VkBuffer *pBuffers, const VkDeviceSize *pOffsets) {
  vki::VkCommandBuffer_Impl *impl = (vki::VkCommandBuffer_Impl *)commandBuffer;
  vki::cmd::BindVertexBuffers cmd;
  cmd.firstBinding = firstBinding;
  cmd.bindingCount = bindingCount;
  ASSERT_ALWAYS(bindingCount < 64);
  ito(bindingCount) {
    cmd.pBuffers[i] = (vki::VkBuffer_Impl *)pBuffers[i];
    cmd.pOffsets[i] = pOffsets[i];
  }
  WRITE_CMD(impl, cmd, BindVertexBuffers);
}

VKAPI_ATTR void VKAPI_CALL vkCmdDraw(VkCommandBuffer commandBuffer,
                                     uint32_t vertexCount,
                                     uint32_t instanceCount,
                                     uint32_t firstVertex,
                                     uint32_t firstInstance) {
  vki::VkCommandBuffer_Impl *impl = (vki::VkCommandBuffer_Impl *)commandBuffer;
  vki::cmd::Draw cmd;
  cmd.vertexCount = vertexCount;
  cmd.instanceCount = instanceCount;
  cmd.firstVertex = firstVertex;
  cmd.firstInstance = firstInstance;
  WRITE_CMD(impl, cmd, Draw);
}

VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndexed(
    VkCommandBuffer commandBuffer, uint32_t indexCount, uint32_t instanceCount,
    uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) {
  vki::VkCommandBuffer_Impl *impl = (vki::VkCommandBuffer_Impl *)commandBuffer;
  vki::cmd::DrawIndexed cmd;
  cmd.indexCount = indexCount;
  cmd.instanceCount = instanceCount;
  cmd.firstIndex = firstIndex;
  cmd.vertexOffset = vertexOffset;
  cmd.firstInstance = firstInstance;
  WRITE_CMD(impl, cmd, DrawIndexed);
}

VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndirect(VkCommandBuffer commandBuffer,
                                             VkBuffer buffer,
                                             VkDeviceSize offset,
                                             uint32_t drawCount,
                                             uint32_t stride) {
  vki::VkCommandBuffer_Impl *impl = (vki::VkCommandBuffer_Impl *)commandBuffer;
  vki::cmd::DrawIndirect cmd;
  cmd.buffer = (vki::VkBuffer_Impl *)buffer;
  cmd.offset = offset;
  cmd.drawCount = drawCount;
  cmd.stride = stride;
  WRITE_CMD(impl, cmd, DrawIndirect);
}

VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndexedIndirect(
    VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset,
    uint32_t drawCount, uint32_t stride) {
  vki::VkCommandBuffer_Impl *impl = (vki::VkCommandBuffer_Impl *)commandBuffer;
  vki::cmd::DrawIndexedIndirect cmd;
  cmd.buffer = (vki::VkBuffer_Impl *)buffer;
  cmd.offset = offset;
  cmd.drawCount = drawCount;
  cmd.stride = stride;
  WRITE_CMD(impl, cmd, DrawIndexedIndirect);
}

VKAPI_ATTR void VKAPI_CALL vkCmdDispatch(VkCommandBuffer commandBuffer,
                                         uint32_t groupCountX,
                                         uint32_t groupCountY,
                                         uint32_t groupCountZ) {
  vki::VkCommandBuffer_Impl *impl = (vki::VkCommandBuffer_Impl *)commandBuffer;
  vki::cmd::Dispatch cmd;
  cmd.groupCountX = groupCountX;
  cmd.groupCountY = groupCountY;
  cmd.groupCountZ = groupCountZ;
  WRITE_CMD(impl, cmd, Dispatch);
}

VKAPI_ATTR void VKAPI_CALL vkCmdDispatchIndirect(VkCommandBuffer commandBuffer,
                                                 VkBuffer buffer,
                                                 VkDeviceSize offset) {
  vki::VkCommandBuffer_Impl *impl = (vki::VkCommandBuffer_Impl *)commandBuffer;
  vki::cmd::DispatchIndirect cmd;
  cmd.buffer = (vki::VkBuffer_Impl *)buffer;
  cmd.offset = offset;
  WRITE_CMD(impl, cmd, DispatchIndirect);
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyBuffer(VkCommandBuffer commandBuffer,
                                           VkBuffer srcBuffer,
                                           VkBuffer dstBuffer,
                                           uint32_t regionCount,
                                           const VkBufferCopy *pRegions) {
  vki::VkCommandBuffer_Impl *impl = (vki::VkCommandBuffer_Impl *)commandBuffer;
  vki::cmd::CopyBuffer cmd;
  cmd.src = (vki::VkBuffer_Impl *)srcBuffer;
  cmd.dst = (vki::VkBuffer_Impl *)dstBuffer;
  cmd.regionCount = regionCount;
  ASSERT_ALWAYS(cmd.regionCount < 0x10);
  memcpy(cmd.pRegions, pRegions, cmd.regionCount * sizeof(VkBufferCopy));
  WRITE_CMD(impl, cmd, CopyBuffer);
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyImage(VkCommandBuffer commandBuffer,
                                          VkImage srcImage,
                                          VkImageLayout srcImageLayout,
                                          VkImage dstImage,
                                          VkImageLayout dstImageLayout,
                                          uint32_t regionCount,
                                          const VkImageCopy *pRegions) {
  vki::VkCommandBuffer_Impl *impl = (vki::VkCommandBuffer_Impl *)commandBuffer;
  vki::cmd::CopyImage cmd;
  cmd.src = (vki::VkImage_Impl *)srcImage;
  cmd.dst = (vki::VkImage_Impl *)dstImage;
  cmd.regionCount = regionCount;
  ASSERT_ALWAYS(regionCount < 0x10);
  ito(regionCount) cmd.pRegions[i] = pRegions[i];
  WRITE_CMD(impl, cmd, CopyImage);
}

VKAPI_ATTR void VKAPI_CALL
vkCmdBlitImage(VkCommandBuffer commandBuffer, VkImage srcImage,
               VkImageLayout srcImageLayout, VkImage dstImage,
               VkImageLayout dstImageLayout, uint32_t regionCount,
               const VkImageBlit *pRegions, VkFilter filter) {
  return;
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyBufferToImage(
    VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkImage dstImage,
    VkImageLayout dstImageLayout, uint32_t regionCount,
    const VkBufferImageCopy *pRegions) {
  vki::VkCommandBuffer_Impl *impl = (vki::VkCommandBuffer_Impl *)commandBuffer;
  vki::cmd::CopyBufferToImage cmd;
  cmd.srcBuffer = (vki::VkBuffer_Impl *)srcBuffer;
  cmd.dstImage = (vki::VkImage_Impl *)dstImage;
  cmd.regionCount = regionCount;
  ASSERT_ALWAYS(regionCount < 0x10);
  ito(regionCount) cmd.pRegions[i] = pRegions[i];
  WRITE_CMD(impl, cmd, CopyBufferToImage);
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyImageToBuffer(
    VkCommandBuffer commandBuffer, VkImage srcImage,
    VkImageLayout srcImageLayout, VkBuffer dstBuffer, uint32_t regionCount,
    const VkBufferImageCopy *pRegions) {
  vki::VkCommandBuffer_Impl *impl = (vki::VkCommandBuffer_Impl *)commandBuffer;
  vki::cmd::CopyImageToBuffer cmd;
  cmd.dstBuffer = (vki::VkBuffer_Impl *)dstBuffer;
  cmd.srcImage = (vki::VkImage_Impl *)srcImage;
  cmd.regionCount = regionCount;
  ASSERT_ALWAYS(regionCount < 0x10);
  ito(regionCount) cmd.pRegions[i] = pRegions[i];
  WRITE_CMD(impl, cmd, CopyImageToBuffer);
}

VKAPI_ATTR void VKAPI_CALL vkCmdUpdateBuffer(VkCommandBuffer commandBuffer,
                                             VkBuffer dstBuffer,
                                             VkDeviceSize dstOffset,
                                             VkDeviceSize dataSize,
                                             const void *pData) {
  vki::VkCommandBuffer_Impl *impl = (vki::VkCommandBuffer_Impl *)commandBuffer;
  vki::cmd::UpdateBuffer cmd;
  cmd.dstBuffer = (vki::VkBuffer_Impl *)dstBuffer;
  cmd.dataSize = dataSize;
  cmd.dstOffset = dstOffset;
  WRITE_CMD(impl, cmd, UpdateBuffer);
  impl->write(pData, dataSize);
}

VKAPI_ATTR void VKAPI_CALL vkCmdFillBuffer(VkCommandBuffer commandBuffer,
                                           VkBuffer dstBuffer,
                                           VkDeviceSize dstOffset,
                                           VkDeviceSize size, uint32_t data) {
  return;
}

VKAPI_ATTR void VKAPI_CALL vkCmdClearColorImage(
    VkCommandBuffer commandBuffer, VkImage image, VkImageLayout imageLayout,
    const VkClearColorValue *pColor, uint32_t rangeCount,
    const VkImageSubresourceRange *pRanges) {
  return;
}

VKAPI_ATTR void VKAPI_CALL vkCmdClearDepthStencilImage(
    VkCommandBuffer commandBuffer, VkImage image, VkImageLayout imageLayout,
    const VkClearDepthStencilValue *pDepthStencil, uint32_t rangeCount,
    const VkImageSubresourceRange *pRanges) {
  return;
}

VKAPI_ATTR void VKAPI_CALL
vkCmdClearAttachments(VkCommandBuffer commandBuffer, uint32_t attachmentCount,
                      const VkClearAttachment *pAttachments, uint32_t rectCount,
                      const VkClearRect *pRects) {
  vki::VkCommandBuffer_Impl *impl = (vki::VkCommandBuffer_Impl *)commandBuffer;
  vki::cmd::ClearAttachments cmd;
  cmd.attachmentCount = attachmentCount;
  cmd.rectCount = rectCount;
  ito(attachmentCount) cmd.pAttachments[i] = pAttachments[i];
  ito(rectCount) cmd.pRects[i] = pRects[i];
  WRITE_CMD(impl, cmd, ClearAttachments);
}

VKAPI_ATTR void VKAPI_CALL vkCmdResolveImage(VkCommandBuffer commandBuffer,
                                             VkImage srcImage,
                                             VkImageLayout srcImageLayout,
                                             VkImage dstImage,
                                             VkImageLayout dstImageLayout,
                                             uint32_t regionCount,
                                             const VkImageResolve *pRegions) {
  return;
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetEvent(VkCommandBuffer commandBuffer,
                                         VkEvent event,
                                         VkPipelineStageFlags stageMask) {
  return;
}

VKAPI_ATTR void VKAPI_CALL vkCmdResetEvent(VkCommandBuffer commandBuffer,
                                           VkEvent event,
                                           VkPipelineStageFlags stageMask) {
  return;
}

VKAPI_ATTR void VKAPI_CALL vkCmdWaitEvents(
    VkCommandBuffer commandBuffer, uint32_t eventCount, const VkEvent *pEvents,
    VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask,
    uint32_t memoryBarrierCount, const VkMemoryBarrier *pMemoryBarriers,
    uint32_t bufferMemoryBarrierCount,
    const VkBufferMemoryBarrier *pBufferMemoryBarriers,
    uint32_t imageMemoryBarrierCount,
    const VkImageMemoryBarrier *pImageMemoryBarriers) {
  return;
}

VKAPI_ATTR void VKAPI_CALL vkCmdPipelineBarrier(
    VkCommandBuffer commandBuffer, VkPipelineStageFlags srcStageMask,
    VkPipelineStageFlags dstStageMask, VkDependencyFlags dependencyFlags,
    uint32_t memoryBarrierCount, const VkMemoryBarrier *pMemoryBarriers,
    uint32_t bufferMemoryBarrierCount,
    const VkBufferMemoryBarrier *pBufferMemoryBarriers,
    uint32_t imageMemoryBarrierCount,
    const VkImageMemoryBarrier *pImageMemoryBarriers) {
  return;
}

VKAPI_ATTR void VKAPI_CALL vkCmdBeginQuery(VkCommandBuffer commandBuffer,
                                           VkQueryPool queryPool,
                                           uint32_t query,
                                           VkQueryControlFlags flags) {
  return;
}

VKAPI_ATTR void VKAPI_CALL vkCmdEndQuery(VkCommandBuffer commandBuffer,
                                         VkQueryPool queryPool,
                                         uint32_t query) {
  return;
}

VKAPI_ATTR void VKAPI_CALL vkCmdResetQueryPool(VkCommandBuffer commandBuffer,
                                               VkQueryPool queryPool,
                                               uint32_t firstQuery,
                                               uint32_t queryCount) {
  return;
}

VKAPI_ATTR void VKAPI_CALL vkCmdWriteTimestamp(
    VkCommandBuffer commandBuffer, VkPipelineStageFlagBits pipelineStage,
    VkQueryPool queryPool, uint32_t query) {
  return;
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyQueryPoolResults(
    VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t firstQuery,
    uint32_t queryCount, VkBuffer dstBuffer, VkDeviceSize dstOffset,
    VkDeviceSize stride, VkQueryResultFlags flags) {
  return;
}

VKAPI_ATTR void VKAPI_CALL vkCmdPushConstants(VkCommandBuffer commandBuffer,
                                              VkPipelineLayout layout,
                                              VkShaderStageFlags stageFlags,
                                              uint32_t offset, uint32_t size,
                                              const void *pValues) {
  vki::VkCommandBuffer_Impl *impl = (vki::VkCommandBuffer_Impl *)commandBuffer;
  vki::cmd::PushConstants cmd;
  cmd.layout = (vki::VkPipelineLayout_Impl *)layout;
  cmd.stageFlags = stageFlags;
  cmd.offset = offset;
  cmd.size = size;
  WRITE_CMD(impl, cmd, PushConstants);
  impl->write(pValues, size);
}

VKAPI_ATTR void VKAPI_CALL vkCmdBeginRenderPass(
    VkCommandBuffer commandBuffer,
    const VkRenderPassBeginInfo *pRenderPassBegin, VkSubpassContents contents) {
  vki::VkCommandBuffer_Impl *impl = (vki::VkCommandBuffer_Impl *)commandBuffer;
  vki::cmd::RenderPassBegin cmd;
  cmd.renderArea = pRenderPassBegin->renderArea;
  cmd.renderPass = (vki::VkRenderPass_Impl *)pRenderPassBegin->renderPass;
  cmd.framebuffer = (vki::VkFramebuffer_Impl *)pRenderPassBegin->framebuffer;
  cmd.clearValueCount = pRenderPassBegin->clearValueCount;
  ito(cmd.clearValueCount) cmd.pClearValues[i] =
      pRenderPassBegin->pClearValues[i];
  ASSERT_ALWAYS(contents == VkSubpassContents::VK_SUBPASS_CONTENTS_INLINE);
  WRITE_CMD(impl, cmd, RenderPassBegin);
}

VKAPI_ATTR void VKAPI_CALL vkCmdNextSubpass(VkCommandBuffer commandBuffer,
                                            VkSubpassContents contents) {
  return;
}

VKAPI_ATTR void VKAPI_CALL vkCmdEndRenderPass(VkCommandBuffer commandBuffer) {
  vki::VkCommandBuffer_Impl *impl = (vki::VkCommandBuffer_Impl *)commandBuffer;
  impl->write_key((uint8_t)vki::cmd::Cmd_t::RenderPassEnd);
}

VKAPI_ATTR void VKAPI_CALL
vkCmdExecuteCommands(VkCommandBuffer commandBuffer, uint32_t commandBufferCount,
                     const VkCommandBuffer *pCommandBuffers) {
  return;
}
// END DEVICE FUNCTIONS

// BEGIN OF SC FUNCTIONS
VKAPI_ATTR VkResult VKAPI_CALL vkCreateSwapchainKHR(
    VkDevice device, const VkSwapchainCreateInfoKHR *pCreateInfo,
    const VkAllocationCallbacks *pAllocator, VkSwapchainKHR *pSwapchain) {
  NOTNULL(pSwapchain);
  vki::VkSurfaceKHR_Impl *surface =
      (vki::VkSurfaceKHR_Impl *)pCreateInfo->surface;
  vki::VkSwapChain_Impl *impl = ALLOC_VKOBJ_T(VkSwapChain);
  xcb_get_geometry_cookie_t cookie;
  xcb_get_geometry_reply_t *reply;

  cookie = xcb_get_geometry(surface->connection, surface->window);
  ASSERT_ALWAYS(reply =
                    xcb_get_geometry_reply(surface->connection, cookie, NULL));
  ASSERT_ALWAYS(pCreateInfo->minImageCount == 2);
  impl->surface = surface;
  impl->surface->refcnt++;
  impl->width = pCreateInfo->imageExtent.width;
  impl->height = pCreateInfo->imageExtent.height;
  impl->format = pCreateInfo->imageFormat;
  impl->image_count = pCreateInfo->minImageCount;
  free(reply);
  *pSwapchain = (VkSwapchainKHR)impl;
  uint32_t bpp = 0;
  switch (impl->format) {
  case VkFormat::VK_FORMAT_R8G8B8A8_SRGB: {
    bpp = 4;
    break;
  }
  default:
    ASSERT_ALWAYS(false);
  };
  ASSERT_ALWAYS(bpp != 0);
  ito(impl->image_count) {
    vki::VkDeviceMemory_Impl *mem = ALLOC_VKOBJ_T(VkDeviceMemory);
    mem->size = bpp * impl->width * impl->height;
    mem->ptr = (uint8_t *)malloc(mem->size);
    impl->images[i].refcnt = 1;
    impl->images[i].format = impl->format;
    impl->images[i].mem = mem;
    impl->images[i].size = mem->size;
    impl->images[i].offset = (size_t)0;
  }
  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vkDestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain,
                      const VkAllocationCallbacks *pAllocator) {
  RELEASE_VKOBJ(swapchain, VkSwapChain);
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetSwapchainImagesKHR(
    VkDevice device, VkSwapchainKHR swapchain, uint32_t *pSwapchainImageCount,
    VkImage *pSwapchainImages) {
  if (pSwapchainImageCount != NULL && pSwapchainImages == NULL) {
    *pSwapchainImageCount = 2;
    return VK_SUCCESS;
  }
  vki::VkSwapChain_Impl *impl = (vki::VkSwapChain_Impl *)swapchain;
  pSwapchainImages[0] = (VkImage)(void *)&impl->images[0];
  pSwapchainImages[1] = (VkImage)(void *)&impl->images[1];
  return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkAcquireNextImageKHR(
    VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout,
    VkSemaphore semaphore, VkFence fence, uint32_t *pImageIndex) {
  vki::VkSwapChain_Impl *impl = (vki::VkSwapChain_Impl *)swapchain;
  impl->cur_image = (impl->cur_image + 1) % impl->image_count;
  *pImageIndex = impl->cur_image;
  return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR *pPresentInfo) {
  ito(pPresentInfo->swapchainCount) {
    vki::VkSwapChain_Impl *impl =
        (vki::VkSwapChain_Impl *)pPresentInfo->pSwapchains[i];
    impl->present();
  }
  return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetDeviceGroupPresentCapabilitiesKHR(
    VkDevice device,
    VkDeviceGroupPresentCapabilitiesKHR *pDeviceGroupPresentCapabilities) {
  return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetDeviceGroupSurfacePresentModesKHR(
    VkDevice device, VkSurfaceKHR surface,
    VkDeviceGroupPresentModeFlagsKHR *pModes) {
  return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDevicePresentRectanglesKHR(
    VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t *pRectCount,
    VkRect2D *pRects) {
  return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkAcquireNextImage2KHR(
    VkDevice device, const VkAcquireNextImageInfoKHR *pAcquireInfo,
    uint32_t *pImageIndex) {
  return VK_SUCCESS;
}
// END OF SC FUNCTIONS

#define CASE(fun)                                                              \
  if (strcmp(pName, #fun) == 0)                                                \
    return (PFN_vkVoidFunction)&fun;

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vkGetDeviceProcAddr(VkDevice device, const char *pName) {
  ALL_FUNCTIONS;
  return (PFN_vkVoidFunction)allocate_trap(pName);
}

VKAPI_ATTR VkResult VKAPI_CALL
vkEnumerateInstanceVersion(uint32_t *pApiVersion) {
  if (pApiVersion != NULL)
    *pApiVersion = VK_API_VERSION_1_2;
  return VK_SUCCESS;
}

// Documentation:
// https://vulkan.lunarg.com/doc/view/1.0.13.0/windows/LoaderAndLayerInterface.html
extern "C" {
#undef VKAPI_ATTR
#define VKAPI_ATTR DLL_EXPORT

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vk_icdGetInstanceProcAddr(VkInstance instance, const char *pName) {
  ALL_FUNCTIONS;
  return (PFN_vkVoidFunction)allocate_trap(pName);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vk_icdGetPhysicalDeviceProcAddr(VkInstance instance, const char *pName) {
  ALL_FUNCTIONS;
  return (PFN_vkVoidFunction)allocate_trap(pName);
}
#undef CASE

VKAPI_ATTR VkResult VKAPI_CALL
vk_icdNegotiateLoaderICDInterfaceVersion(uint32_t *pSupportedVersion) {
  NOTNULL(pSupportedVersion);
  return VK_SUCCESS;
}
}

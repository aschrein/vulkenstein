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
  (void)0

// Data structures to keep track of the objects
// TODO(aschrein): Nuke this from the orbit
namespace vki {
struct VkDeviceMemory_Impl {
  uint32_t id;
  uint8_t *ptr;
  size_t size;
  void release() {
    id = 0;
    if (ptr != NULL)
      free(ptr);
    size = 0;
  }
};
struct VkBuffer_Impl {
  uint32_t id;
  VkDeviceMemory_Impl *mem;
  size_t offset;
  size_t size;
};
struct VkImage_Impl {
  uint32_t id;
  VkDeviceMemory_Impl *mem;
  size_t offset;
  size_t size;
  VkFormat format;
  VkExtent3D extent;
  uint32_t mipLevels;
  uint32_t arrayLayers;
  VkSampleCountFlagBits samples;
  VkImageLayout initialLayout;
};
struct VkImageView_Impl {
  uint32_t id;
  VkImage_Impl *img;
  VkImageViewType type;
  VkFormat format;
  VkComponentMapping components;
  VkImageSubresourceRange subresourceRange;
  void release() { memset(this, 0, sizeof(*this)); }
};
extern "C" void *compile_spirv(uint32_t const *pCode, size_t code_size);
extern "C" void release_spirv(void *ptr);
struct VkShaderModule_Impl {
  uint32_t id;
  void *jitted_code;
  void init(uint32_t const *pCode, size_t code_size) {
    jitted_code = compile_spirv(pCode, code_size);
  }
  void release() {
    if (jitted_code != NULL)
      release_spirv(jitted_code);
    memset(this, 0, sizeof(*this));
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
      while (next_free_slot < N && pool[next_free_slot].id != 0) {
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
    out->id = next_free_slot + 1;
    return out;
  }
};

struct VkSwapChain_Impl {
  uint32_t id;
  VkImage_Impl images[3];
  uint32_t image_count;
  uint32_t current_image;
  uint32_t width, height;
  VkFormat format;
  void release() {
    for (auto &image : images) {
      if (image.mem != 0) {
        image.mem->release();
      }
    }
    MEMZERO((*this));
  }
};

struct VkRenderPass_Impl {
  uint32_t id;
  uint32_t attachmentCount;
  VkAttachmentDescription pAttachments[10];
  uint32_t subpassCount;
  VkSubpassDescription pSubpasses[10];
  uint32_t dependencyCount;
  VkSubpassDependency pDependencies[10];
  void release() { memset(this, 0, sizeof(*this)); }
};

#define OBJ_POOL(type) Pool<type##_Impl> type##_pool = {};

OBJ_POOL(VkBuffer)
OBJ_POOL(VkRenderPass)
OBJ_POOL(VkImageView)
OBJ_POOL(VkImage)
OBJ_POOL(VkDeviceMemory)
OBJ_POOL(VkShaderModule)

#define DECL_IMPL(type)                                                        \
  struct type##_Impl {                                                         \
    uint32_t id;                                                               \
    void release() { memset(this, 0, sizeof(*this)); }                         \
  };

#define OBJ_POOL_DUMMY(type)                                                   \
  DECL_IMPL(type)                                                              \
  Pool<type##_Impl> type##_pool = {};

OBJ_POOL_DUMMY(VkSemaphore)
OBJ_POOL_DUMMY(VkQueue)
OBJ_POOL_DUMMY(VkCommandPool)
OBJ_POOL_DUMMY(VkFence)
OBJ_POOL_DUMMY(VkCommandBuffer)
OBJ_POOL_DUMMY(VkPipelineCache)
DECL_IMPL(VkInstance)
DECL_IMPL(VkPhysicalDevice)
DECL_IMPL(VkDevice)
DECL_IMPL(VkSurfaceKHR)

struct VkFramebuffer_Impl {
  uint32_t id;
  VkFramebufferCreateFlags flags;
  VkRenderPass_Impl *renderPass;
  uint32_t attachmentCount;
  VkImageView_Impl *pAttachments[0x10];
  uint32_t width;
  uint32_t height;
  uint32_t layers;
  void release() { memset(this, 0, sizeof(*this)); }
};
OBJ_POOL(VkFramebuffer)

#define ALLOC_VKOBJ(type) (type)(void *) vki::type##_pool.alloc()
#define ALLOC_VKOBJ_T(type) vki::type##_pool.alloc()
#define GET_VKOBJ(type, id) (&vki::type##_pool.pool[id])
#define RELEASE_VKOBJ(obj, type)                                               \
  NOTNULL(obj);                                                                \
  ((vki::type##_Impl *)obj)->release()

static VkInstance_Impl g_instance;
static VkPhysicalDevice_Impl g_phys_device;
static VkDevice_Impl g_device;
static VkSwapChain_Impl g_swapchain;
static VkSurfaceKHR_Impl g_surface;
static xcb_connection_t *g_connection;
static xcb_window_t g_window;
void init() {
  MEMZERO(g_instance);
  MEMZERO(g_phys_device);
  MEMZERO(g_device);
  MEMZERO(g_swapchain);
  MEMZERO(g_surface);
  MEMZERO(g_connection);
  MEMZERO(g_window);
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
  vki::g_connection = pCreateInfo->connection;
  vki::g_window = pCreateInfo->window;
  pSurface = (VkSurfaceKHR *)(void *)&vki::g_surface;
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
  *pPhysicalDevices = (VkPhysicalDevice)&vki::g_phys_device;
  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vkDestroySurfaceKHR(VkInstance instance, VkSurfaceKHR surface,
                    const VkAllocationCallbacks *pAllocator) {
  ASSERT_ALWAYS(false);
}

// BEGIN DEVICE FUNCTIONS
VKAPI_ATTR VkResult VKAPI_CALL vkCreateInstance(
    const VkInstanceCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator, VkInstance *pInstance) {
  NOTNULL(pInstance);
  *pInstance = (VkInstance)(void *)&vki::g_instance;
  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyInstance(
    VkInstance instance, const VkAllocationCallbacks *pAllocator) {
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
  *pDevice = (VkDevice)&vki::g_device;
  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vkDestroyDevice(VkDevice device, const VkAllocationCallbacks *pAllocator) {
  return;
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
  return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkBindImageMemory(VkDevice device, VkImage image,
                                                 VkDeviceMemory memory,
                                                 VkDeviceSize memoryOffset) {
  vki::VkDeviceMemory_Impl *mem = (vki::VkDeviceMemory_Impl *)memory;
  vki::VkImage_Impl *impl = (vki::VkImage_Impl *)image;
  impl->mem = mem;
  impl->offset = memoryOffset;
  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vkGetBufferMemoryRequirements(VkDevice device, VkBuffer buffer,
                              VkMemoryRequirements *pMemoryRequirements) {
  return;
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
  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyEvent(
    VkDevice device, VkEvent event, const VkAllocationCallbacks *pAllocator) {
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
  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyBuffer(
    VkDevice device, VkBuffer buffer, const VkAllocationCallbacks *pAllocator) {
  return;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateBufferView(
    VkDevice device, const VkBufferViewCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator, VkBufferView *pView) {
  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vkDestroyBufferView(VkDevice device, VkBufferView bufferView,
                    const VkAllocationCallbacks *pAllocator) {
  return;
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
  return;
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
  return;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreatePipelineLayout(
    VkDevice device, const VkPipelineLayoutCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator,
    VkPipelineLayout *pPipelineLayout) {
  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vkDestroyPipelineLayout(VkDevice device, VkPipelineLayout pipelineLayout,
                        const VkAllocationCallbacks *pAllocator) {
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
  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyDescriptorSetLayout(
    VkDevice device, VkDescriptorSetLayout descriptorSetLayout,
    const VkAllocationCallbacks *pAllocator) {
  return;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDescriptorPool(
    VkDevice device, const VkDescriptorPoolCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator,
    VkDescriptorPool *pDescriptorPool) {
  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vkDestroyDescriptorPool(VkDevice device, VkDescriptorPool descriptorPool,
                        const VkAllocationCallbacks *pAllocator) {
  return;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkResetDescriptorPool(VkDevice device, VkDescriptorPool descriptorPool,
                      VkDescriptorPoolResetFlags flags) {
  return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkAllocateDescriptorSets(
    VkDevice device, const VkDescriptorSetAllocateInfo *pAllocateInfo,
    VkDescriptorSet *pDescriptorSets) {
  return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkFreeDescriptorSets(
    VkDevice device, VkDescriptorPool descriptorPool,
    uint32_t descriptorSetCount, const VkDescriptorSet *pDescriptorSets) {
  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkUpdateDescriptorSets(
    VkDevice device, uint32_t descriptorWriteCount,
    const VkWriteDescriptorSet *pDescriptorWrites, uint32_t descriptorCopyCount,
    const VkCopyDescriptorSet *pDescriptorCopies) {
  return;
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
  ito(impl->attachmentCount) impl->pAttachments[i] =
      (vki::VkImageView_Impl *)pCreateInfo->pAttachments[i];
  impl->renderPass = (vki::VkRenderPass_Impl *)pCreateInfo->renderPass;
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
  memcpy(impl->pSubpasses, pCreateInfo->pSubpasses,
         sizeof(impl->pSubpasses[0]) * impl->subpassCount);
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
  *pCommandBuffers = (VkCommandBuffer)(void *)ALLOC_VKOBJ_T(VkCommandBuffer);
  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkFreeCommandBuffers(
    VkDevice device, VkCommandPool commandPool, uint32_t commandBufferCount,
    const VkCommandBuffer *pCommandBuffers) {
  return;
}

VKAPI_ATTR VkResult VKAPI_CALL vkBeginCommandBuffer(
    VkCommandBuffer commandBuffer, const VkCommandBufferBeginInfo *pBeginInfo) {
  return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkEndCommandBuffer(VkCommandBuffer commandBuffer) {
  return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkResetCommandBuffer(
    VkCommandBuffer commandBuffer, VkCommandBufferResetFlags flags) {
  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vkCmdBindPipeline(VkCommandBuffer commandBuffer,
                  VkPipelineBindPoint pipelineBindPoint, VkPipeline pipeline) {
  return;
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetViewport(VkCommandBuffer commandBuffer,
                                            uint32_t firstViewport,
                                            uint32_t viewportCount,
                                            const VkViewport *pViewports) {
  return;
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetScissor(VkCommandBuffer commandBuffer,
                                           uint32_t firstScissor,
                                           uint32_t scissorCount,
                                           const VkRect2D *pScissors) {
  return;
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetLineWidth(VkCommandBuffer commandBuffer,
                                             float lineWidth) {
  return;
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthBias(VkCommandBuffer commandBuffer,
                                             float depthBiasConstantFactor,
                                             float depthBiasClamp,
                                             float depthBiasSlopeFactor) {
  return;
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetBlendConstants(
    VkCommandBuffer commandBuffer, const float blendConstants[4]) {
  return;
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthBounds(VkCommandBuffer commandBuffer,
                                               float minDepthBounds,
                                               float maxDepthBounds) {
  return;
}

VKAPI_ATTR void VKAPI_CALL
vkCmdSetStencilCompareMask(VkCommandBuffer commandBuffer,
                           VkStencilFaceFlags faceMask, uint32_t compareMask) {
  return;
}

VKAPI_ATTR void VKAPI_CALL
vkCmdSetStencilWriteMask(VkCommandBuffer commandBuffer,
                         VkStencilFaceFlags faceMask, uint32_t writeMask) {
  return;
}

VKAPI_ATTR void VKAPI_CALL
vkCmdSetStencilReference(VkCommandBuffer commandBuffer,
                         VkStencilFaceFlags faceMask, uint32_t reference) {
  return;
}

VKAPI_ATTR void VKAPI_CALL vkCmdBindDescriptorSets(
    VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint,
    VkPipelineLayout layout, uint32_t firstSet, uint32_t descriptorSetCount,
    const VkDescriptorSet *pDescriptorSets, uint32_t dynamicOffsetCount,
    const uint32_t *pDynamicOffsets) {
  return;
}

VKAPI_ATTR void VKAPI_CALL vkCmdBindIndexBuffer(VkCommandBuffer commandBuffer,
                                                VkBuffer buffer,
                                                VkDeviceSize offset,
                                                VkIndexType indexType) {
  return;
}

VKAPI_ATTR void VKAPI_CALL vkCmdBindVertexBuffers(
    VkCommandBuffer commandBuffer, uint32_t firstBinding, uint32_t bindingCount,
    const VkBuffer *pBuffers, const VkDeviceSize *pOffsets) {
  return;
}

VKAPI_ATTR void VKAPI_CALL vkCmdDraw(VkCommandBuffer commandBuffer,
                                     uint32_t vertexCount,
                                     uint32_t instanceCount,
                                     uint32_t firstVertex,
                                     uint32_t firstInstance) {
  return;
}

VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndexed(
    VkCommandBuffer commandBuffer, uint32_t indexCount, uint32_t instanceCount,
    uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) {
  return;
}

VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndirect(VkCommandBuffer commandBuffer,
                                             VkBuffer buffer,
                                             VkDeviceSize offset,
                                             uint32_t drawCount,
                                             uint32_t stride) {
  return;
}

VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndexedIndirect(
    VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset,
    uint32_t drawCount, uint32_t stride) {
  return;
}

VKAPI_ATTR void VKAPI_CALL vkCmdDispatch(VkCommandBuffer commandBuffer,
                                         uint32_t groupCountX,
                                         uint32_t groupCountY,
                                         uint32_t groupCountZ) {
  return;
}

VKAPI_ATTR void VKAPI_CALL vkCmdDispatchIndirect(VkCommandBuffer commandBuffer,
                                                 VkBuffer buffer,
                                                 VkDeviceSize offset) {
  return;
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyBuffer(VkCommandBuffer commandBuffer,
                                           VkBuffer srcBuffer,
                                           VkBuffer dstBuffer,
                                           uint32_t regionCount,
                                           const VkBufferCopy *pRegions) {
  return;
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyImage(VkCommandBuffer commandBuffer,
                                          VkImage srcImage,
                                          VkImageLayout srcImageLayout,
                                          VkImage dstImage,
                                          VkImageLayout dstImageLayout,
                                          uint32_t regionCount,
                                          const VkImageCopy *pRegions) {
  return;
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
  return;
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyImageToBuffer(
    VkCommandBuffer commandBuffer, VkImage srcImage,
    VkImageLayout srcImageLayout, VkBuffer dstBuffer, uint32_t regionCount,
    const VkBufferImageCopy *pRegions) {
  return;
}

VKAPI_ATTR void VKAPI_CALL vkCmdUpdateBuffer(VkCommandBuffer commandBuffer,
                                             VkBuffer dstBuffer,
                                             VkDeviceSize dstOffset,
                                             VkDeviceSize dataSize,
                                             const void *pData) {
  return;
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
  return;
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
  return;
}

VKAPI_ATTR void VKAPI_CALL vkCmdBeginRenderPass(
    VkCommandBuffer commandBuffer,
    const VkRenderPassBeginInfo *pRenderPassBegin, VkSubpassContents contents) {
  return;
}

VKAPI_ATTR void VKAPI_CALL vkCmdNextSubpass(VkCommandBuffer commandBuffer,
                                            VkSubpassContents contents) {
  return;
}

VKAPI_ATTR void VKAPI_CALL vkCmdEndRenderPass(VkCommandBuffer commandBuffer) {
  return;
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
  //  ASSERT_ALWAYS(vki::g_swapchain.id == 0 && "Already created one swap
  //  chain!");
  vki::g_swapchain.release();
  vki::g_swapchain.id = 1;
  xcb_get_geometry_cookie_t cookie;
  xcb_get_geometry_reply_t *reply;

  cookie = xcb_get_geometry(vki::g_connection, vki::g_window);
  ASSERT_ALWAYS(reply =
                    xcb_get_geometry_reply(vki::g_connection, cookie, NULL));
  ASSERT_ALWAYS(pCreateInfo->minImageCount == 2);
  vki::g_swapchain.width = pCreateInfo->imageExtent.width;
  vki::g_swapchain.height = pCreateInfo->imageExtent.height;
  vki::g_swapchain.format = pCreateInfo->imageFormat;
  vki::g_swapchain.image_count = pCreateInfo->minImageCount;
  free(reply);
  *pSwapchain = (VkSwapchainKHR)(void *)&vki::g_swapchain;
  uint32_t bpp = 0;
  switch (vki::g_swapchain.format) {
  case VkFormat::VK_FORMAT_R8G8B8A8_SRGB: {
    bpp = 4;
    break;
  }
  default:
    ASSERT_ALWAYS(false);
  };
  ASSERT_ALWAYS(bpp != 0);
  ito(vki::g_swapchain.image_count) {
    vki::VkDeviceMemory_Impl *mem = ALLOC_VKOBJ_T(VkDeviceMemory);
    mem->size = bpp * vki::g_swapchain.width * vki::g_swapchain.height;
    mem->ptr = (uint8_t *)malloc(mem->size);
    vki::g_swapchain.images[i].id = i + 1;
    vki::g_swapchain.images[i].format = vki::g_swapchain.format;
    vki::g_swapchain.images[i].mem = mem;
    vki::g_swapchain.images[i].size = mem->size;
    vki::g_swapchain.images[i].offset = (size_t)0;
  }
  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vkDestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain,
                      const VkAllocationCallbacks *pAllocator) {}

VKAPI_ATTR VkResult VKAPI_CALL vkGetSwapchainImagesKHR(
    VkDevice device, VkSwapchainKHR swapchain, uint32_t *pSwapchainImageCount,
    VkImage *pSwapchainImages) {
  if (pSwapchainImageCount != NULL && pSwapchainImages == NULL) {
    *pSwapchainImageCount = 2;
    return VK_SUCCESS;
  }
  ASSERT_ALWAYS(vki::g_swapchain.id != 0);
  pSwapchainImages[0] = (VkImage)(void *)&vki::g_swapchain.images[0];
  pSwapchainImages[1] = (VkImage)(void *)&vki::g_swapchain.images[1];
  return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkAcquireNextImageKHR(
    VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout,
    VkSemaphore semaphore, VkFence fence, uint32_t *pImageIndex) {
  return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR *pPresentInfo) {
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

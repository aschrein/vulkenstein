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
////////////////////////////////////////////////////////////
// Those are just dummy implementation most of the time   //
////////////////////////////////////////////////////////////

namespace vki {
struct VkInstance {
  uint32_t id;
};
struct VkSurfaceKHR {
  uint32_t id;
};
struct VkPhysicalDevice {
  uint32_t id;
};

VkInstance g_instance;
VkPhysicalDevice g_phys_device;
VkSurfaceKHR g_surface;
xcb_connection_t *g_connection;
xcb_window_t g_window;

}; // namespace vki
extern "C" {
// Allocate a trap for unimplemented function
void *allocate_trap(char const *fun_name) {
  static uint32_t allocated_memory_size = 0;
  static uint32_t executable_memory_cursor = 0;
  static uint8_t *executable_memory = NULL;
  if (executable_memory == NULL) {
    uint32_t page_size = (uint32_t)sysconf(_SC_PAGE_SIZE);
    // Allocate 4 pages
    allocated_memory_size = page_size * 10;
    executable_memory = (uint8_t *)mmap(NULL, allocated_memory_size,
                                        PROT_READ | PROT_WRITE | PROT_EXEC,
                                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ASSERT_ALWAYS(executable_memory != MAP_FAILED);
  }
  size_t len = strlen(fun_name);
  uint8_t *mem = executable_memory + (size_t)executable_memory_cursor;
  // debug break as the first x86 instruction
  mem[0] = 0xcc;
  executable_memory_cursor += len + 1;
  ASSERT_ALWAYS(executable_memory_cursor < allocated_memory_size);
  // so in memory editor we'll see the missed function name
  memcpy(mem + 1, fun_name, len);
  fprintf(stderr, "trap: %s\n", fun_name);
  return mem;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateInstance(
    const VkInstanceCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator, VkInstance *pInstance) {
  NOTNULL(pInstance);
  *pInstance = (VkInstance)(void *)&vki::g_instance;
  return VK_SUCCESS;
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
  ASSERT_ALWAYS(false);
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfacePresentModesKHR(
    VkPhysicalDevice physicalDevice, VkSurfaceKHR surface,
    uint32_t *pPresentModeCount, VkPresentModeKHR *pPresentModes) {
  ASSERT_ALWAYS(false);
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

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceExtensionProperties(
    const char *pLayerName, uint32_t *pPropertyCount,
    VkExtensionProperties *pProperties) {

  static VkExtensionProperties props[] = {
      {VK_KHR_DEVICE_GROUP_CREATION_EXTENSION_NAME,
       VK_KHR_DEVICE_GROUP_CREATION_SPEC_VERSION},
      {VK_KHR_EXTERNAL_FENCE_CAPABILITIES_EXTENSION_NAME,
       VK_KHR_EXTERNAL_FENCE_CAPABILITIES_SPEC_VERSION},
      {VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME,
       VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_SPEC_VERSION},
      {VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME,
       VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_SPEC_VERSION},
      {VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
       VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_SPEC_VERSION},
      {VK_KHR_XCB_SURFACE_EXTENSION_NAME, VK_KHR_XCB_SURFACE_SPEC_VERSION},
      {VK_KHR_XLIB_SURFACE_EXTENSION_NAME, VK_KHR_XLIB_SURFACE_SPEC_VERSION},
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
      {VK_KHR_DRIVER_PROPERTIES_EXTENSION_NAME,
       VK_KHR_DRIVER_PROPERTIES_SPEC_VERSION},
      {VK_EXT_LINE_RASTERIZATION_EXTENSION_NAME,
       VK_EXT_LINE_RASTERIZATION_SPEC_VERSION},
  };
  NOTNULL(pPropertyCount);
  *pPropertyCount = sizeof(props) / sizeof(props[0]);
  if (pProperties == NULL)
    return VK_SUCCESS;
  ito(sizeof(props) / sizeof(props[0])) pProperties[i] = props[i];
  return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkEnumerateInstanceVersion(uint32_t *pApiVersion) {
  if (pApiVersion != NULL)
    *pApiVersion = VK_API_VERSION_1_2;
  return VK_SUCCESS;
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

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vkGetDeviceProcAddr(VkDevice device, const char *pName) {
#define CASE(fun)                                                              \
  if (strcmp(pName, #fun) == 0)                                                \
    return (PFN_vkVoidFunction)&fun;

#undef CASE
  return (PFN_vkVoidFunction)allocate_trap(pName);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vkGetInstanceProcAddr(VkInstance instance, const char *pName) {
#define CASE(fun)                                                              \
  if (strcmp(pName, #fun) == 0)                                                \
    return (PFN_vkVoidFunction)&fun;
  CASE(vkCreateInstance);
  CASE(vkCreateXcbSurfaceKHR);
  CASE(vkEnumerateInstanceExtensionProperties);
  CASE(vkEnumerateInstanceVersion);
  CASE(vkEnumeratePhysicalDevices);
  CASE(vkGetPhysicalDeviceSurfaceSupportKHR);
  CASE(vkGetPhysicalDeviceSurfaceFormatsKHR);
  CASE(vkGetPhysicalDeviceSurfaceCapabilitiesKHR);
  CASE(vkGetPhysicalDeviceSurfacePresentModesKHR);
  CASE(vkEnumerateDeviceExtensionProperties);
#undef CASE
  return (PFN_vkVoidFunction)allocate_trap(pName);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vkGetPhysicalDeviceProcAddr(VkInstance instance, const char *pName) {
#define CASE(fun)                                                              \
  if (strcmp(pName, #fun) == 0)                                                \
    return (PFN_vkVoidFunction)&fun;
  CASE(vkCreateInstance);
  CASE(vkCreateXcbSurfaceKHR);
  CASE(vkEnumerateInstanceExtensionProperties);
  CASE(vkEnumerateInstanceVersion);
  CASE(vkEnumeratePhysicalDevices);
  CASE(vkGetPhysicalDeviceSurfaceSupportKHR);
  CASE(vkGetPhysicalDeviceSurfaceFormatsKHR);
  CASE(vkGetPhysicalDeviceSurfaceCapabilitiesKHR);
  CASE(vkGetPhysicalDeviceSurfacePresentModesKHR);
  CASE(vkEnumerateDeviceExtensionProperties);
#undef CASE
  return (PFN_vkVoidFunction)allocate_trap(pName);
}

VKAPI_ATTR VkResult VKAPI_CALL vkNegotiateLoaderLayerInterfaceVersion(
    VkNegotiateLayerInterface *pVersionStruct) {
  NOTNULL(pVersionStruct);
  pVersionStruct->sType = LAYER_NEGOTIATE_INTERFACE_STRUCT;
  pVersionStruct->pNext = NULL;
  pVersionStruct->pfnGetDeviceProcAddr = vkGetDeviceProcAddr;
  pVersionStruct->pfnGetInstanceProcAddr = vkGetInstanceProcAddr;
  pVersionStruct->pfnGetPhysicalDeviceProcAddr = vkGetPhysicalDeviceProcAddr;
  pVersionStruct->loaderLayerInterfaceVersion =
      CURRENT_LOADER_ICD_INTERFACE_VERSION;
  return VK_SUCCESS;
}
}

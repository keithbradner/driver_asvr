#include "stdafx.h"

#include <cstdarg>
#include <unistd.h>

#include "linux/CVulkanContext.h"

static void LogLine(const char *f_format, ...)
{
    char l_line[256];
    va_list l_args;
    va_start(l_args, f_format);
    vsnprintf(l_line, sizeof(l_line), f_format, l_args);
    va_end(l_args);
    vr::IVRDriverLog *l_log = vr::VRDriverLog();
    if(l_log) l_log->Log(l_line);
}

CVulkanContext::~CVulkanContext()
{
    Shutdown();
}

bool CVulkanContext::Init()
{
    if(m_device != VK_NULL_HANDLE) return true;
    if(m_initFailed) return false;
    m_initFailed = true; // cleared on success

    if(!m_volkReady)
    {
        if(volkInitialize() != VK_SUCCESS)
        {
            LogLine("CVulkanContext: libvulkan.so.1 not available, frames disabled");
            return false;
        }
        m_volkReady = true;
    }

    VkApplicationInfo l_appInfo = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
    l_appInfo.pApplicationName = "driver_asvr";
    l_appInfo.apiVersion = VK_API_VERSION_1_1; // external memory/semaphore + get_physical_device_properties2 in core

    VkInstanceCreateInfo l_instanceInfo = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    l_instanceInfo.pApplicationInfo = &l_appInfo;
    VkResult l_result = vkCreateInstance(&l_instanceInfo, nullptr, &m_instance);
    if(l_result != VK_SUCCESS)
    {
        LogLine("CVulkanContext: vkCreateInstance failed, VkResult=%d", l_result);
        return false;
    }
    volkLoadInstance(m_instance);

    if(!PickPhysicalDevice()) return false;

    uint32_t l_familyCount = 0U;
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &l_familyCount, nullptr);
    std::vector<VkQueueFamilyProperties> l_families(l_familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &l_familyCount, l_families.data());
    bool l_familyFound = false;
    for(uint32_t i = 0U; i < l_familyCount; i++)
    {
        if(l_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
        {
            m_queueFamily = i;
            l_familyFound = true;
            break;
        }
    }
    if(!l_familyFound)
    {
        LogLine("CVulkanContext: no graphics queue family");
        return false;
    }

    uint32_t l_extensionCount = 0U;
    vkEnumerateDeviceExtensionProperties(m_physicalDevice, nullptr, &l_extensionCount, nullptr);
    std::vector<VkExtensionProperties> l_available(l_extensionCount);
    vkEnumerateDeviceExtensionProperties(m_physicalDevice, nullptr, &l_extensionCount, l_available.data());
    auto l_hasExtension = [&l_available](const char *f_name)
    {
        for(const auto &l_extension : l_available)
        {
            if(strcmp(l_extension.extensionName, f_name) == 0) return true;
        }
        return false;
    };

    std::vector<const char*> l_extensions;
    const char *l_required[] = { VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME, VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME };
    for(const char *l_name : l_required)
    {
        if(!l_hasExtension(l_name))
        {
            LogLine("CVulkanContext: required device extension %s missing", l_name);
            return false;
        }
        l_extensions.push_back(l_name);
    }
    m_hasQueueFamilyForeign = l_hasExtension(VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME);
    if(m_hasQueueFamilyForeign) l_extensions.push_back(VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME);

    const float l_priority = 1.f;
    VkDeviceQueueCreateInfo l_queueInfo = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
    l_queueInfo.queueFamilyIndex = m_queueFamily;
    l_queueInfo.queueCount = 1U;
    l_queueInfo.pQueuePriorities = &l_priority;

    VkDeviceCreateInfo l_deviceInfo = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    l_deviceInfo.queueCreateInfoCount = 1U;
    l_deviceInfo.pQueueCreateInfos = &l_queueInfo;
    l_deviceInfo.enabledExtensionCount = static_cast<uint32_t>(l_extensions.size());
    l_deviceInfo.ppEnabledExtensionNames = l_extensions.data();
    l_result = vkCreateDevice(m_physicalDevice, &l_deviceInfo, nullptr, &m_device);
    if(l_result != VK_SUCCESS)
    {
        LogLine("CVulkanContext: vkCreateDevice failed, VkResult=%d", l_result);
        m_device = VK_NULL_HANDLE;
        return false;
    }
    volkLoadDevice(m_device);
    vkGetDeviceQueue(m_device, m_queueFamily, 0U, &m_queue);

    VkCommandPoolCreateInfo l_poolInfo = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    l_poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    l_poolInfo.queueFamilyIndex = m_queueFamily;
    if(vkCreateCommandPool(m_device, &l_poolInfo, nullptr, &m_commandPool) != VK_SUCCESS)
    {
        Shutdown();
        return false;
    }
    VkCommandBufferAllocateInfo l_allocateInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    l_allocateInfo.commandPool = m_commandPool;
    l_allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    l_allocateInfo.commandBufferCount = 2U;
    if(vkAllocateCommandBuffers(m_device, &l_allocateInfo, m_commandBuffers) != VK_SUCCESS)
    {
        Shutdown();
        return false;
    }
    for(int i = 0; i < 2; i++)
    {
        VkFenceCreateInfo l_fenceInfo = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        l_fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        if(vkCreateFence(m_device, &l_fenceInfo, nullptr, &m_fences[i]) != VK_SUCCESS)
        {
            Shutdown();
            return false;
        }
    }

    m_initFailed = false;
    return true;
}

bool CVulkanContext::PickPhysicalDevice()
{
    uint32_t l_deviceCount = 0U;
    vkEnumeratePhysicalDevices(m_instance, &l_deviceCount, nullptr);
    if(l_deviceCount == 0U)
    {
        LogLine("CVulkanContext: no Vulkan devices");
        return false;
    }
    std::vector<VkPhysicalDevice> l_devices(l_deviceCount);
    vkEnumeratePhysicalDevices(m_instance, &l_deviceCount, l_devices.data());

    // Prefer the device SteamVR itself renders on: RADV first (the Deck),
    // then any discrete GPU, then whatever is first
    int l_bestScore = -1;
    for(VkPhysicalDevice l_candidate : l_devices)
    {
        VkPhysicalDeviceDriverProperties l_driverProperties = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES };
        VkPhysicalDeviceProperties2 l_properties = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
        l_properties.pNext = &l_driverProperties;
        vkGetPhysicalDeviceProperties2(l_candidate, &l_properties);

        int l_score = 0;
        if(l_driverProperties.driverID == VK_DRIVER_ID_MESA_RADV) l_score += 2;
        if(l_properties.properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) l_score += 1;
        if(l_properties.properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU) l_score -= 2;
        if(l_score > l_bestScore)
        {
            l_bestScore = l_score;
            m_physicalDevice = l_candidate;
        }
    }

    VkPhysicalDeviceProperties l_chosen;
    vkGetPhysicalDeviceProperties(m_physicalDevice, &l_chosen);
    LogLine("CVulkanContext: using GPU '%s'", l_chosen.deviceName);
    return true;
}

void CVulkanContext::Shutdown()
{
    if(m_device != VK_NULL_HANDLE)
    {
        vkDeviceWaitIdle(m_device);
        for(int i = 0; i < 2; i++)
        {
            if(m_fences[i] != VK_NULL_HANDLE) vkDestroyFence(m_device, m_fences[i], nullptr);
            m_fences[i] = VK_NULL_HANDLE;
        }
        if(m_commandPool != VK_NULL_HANDLE) vkDestroyCommandPool(m_device, m_commandPool, nullptr);
        m_commandPool = VK_NULL_HANDLE;
        vkDestroyDevice(m_device, nullptr);
        m_device = VK_NULL_HANDLE;
    }
    m_queue = VK_NULL_HANDLE;
    if(m_instance != VK_NULL_HANDLE)
    {
        vkDestroyInstance(m_instance, nullptr);
        m_instance = VK_NULL_HANDLE;
    }
    m_physicalDevice = VK_NULL_HANDLE;
}

bool CVulkanContext::FindMemoryType(uint32_t f_typeBits, VkMemoryPropertyFlags f_wanted, uint32_t &f_index) const
{
    VkPhysicalDeviceMemoryProperties l_properties;
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &l_properties);
    for(uint32_t i = 0U; i < l_properties.memoryTypeCount; i++)
    {
        if((f_typeBits & (1U << i)) && ((l_properties.memoryTypes[i].propertyFlags & f_wanted) == f_wanted))
        {
            f_index = i;
            return true;
        }
    }
    return false;
}

bool CVulkanContext::ImportSharedImage(int f_fd, uint32_t f_width, uint32_t f_height, VkFormat f_format,
    VkImage &f_image, VkDeviceMemory &f_memory)
{
    // The import consumes the fd, so the fallback attempt needs its own copy
    int l_fallbackFd = dup(f_fd);
    if(ImportWithHandleType(f_fd, f_width, f_height, f_format, VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT, f_image, f_memory))
    {
        if(l_fallbackFd >= 0) close(l_fallbackFd);
        return true;
    }
    if(l_fallbackFd < 0) return false;
    LogLine("CVulkanContext: opaque-fd import failed, retrying as dmabuf");
    return ImportWithHandleType(l_fallbackFd, f_width, f_height, f_format, VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT, f_image, f_memory);
}

bool CVulkanContext::ImportWithHandleType(int f_fd, uint32_t f_width, uint32_t f_height, VkFormat f_format,
    VkExternalMemoryHandleTypeFlagBits f_handleType, VkImage &f_image, VkDeviceMemory &f_memory)
{
    f_image = VK_NULL_HANDLE;
    f_memory = VK_NULL_HANDLE;

    VkExternalMemoryImageCreateInfo l_externalInfo = { VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO };
    l_externalInfo.handleTypes = static_cast<VkExternalMemoryHandleTypeFlags>(f_handleType);

    // Mirror what a renderable shared image plausibly has; must include our TRANSFER usage
    VkImageCreateInfo l_imageInfo = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    l_imageInfo.pNext = &l_externalInfo;
    l_imageInfo.imageType = VK_IMAGE_TYPE_2D;
    l_imageInfo.format = f_format;
    l_imageInfo.extent = { f_width, f_height, 1U };
    l_imageInfo.mipLevels = 1U;
    l_imageInfo.arrayLayers = 1U;
    l_imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    l_imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    l_imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    l_imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    l_imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkResult l_result = vkCreateImage(m_device, &l_imageInfo, nullptr, &f_image);
    if(l_result != VK_SUCCESS)
    {
        LogLine("CVulkanContext: vkCreateImage(import, handleType=%d) failed, VkResult=%d", f_handleType, l_result);
        close(f_fd);
        return false;
    }

    VkMemoryRequirements l_requirements;
    vkGetImageMemoryRequirements(m_device, f_image, &l_requirements);
    uint32_t l_typeBits = l_requirements.memoryTypeBits;
    if(f_handleType == VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT)
    {
        VkMemoryFdPropertiesKHR l_fdProperties = { VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR };
        if(vkGetMemoryFdPropertiesKHR(m_device, f_handleType, f_fd, &l_fdProperties) == VK_SUCCESS) l_typeBits &= l_fdProperties.memoryTypeBits;
    }
    uint32_t l_memoryType = 0U;
    if(!FindMemoryType(l_typeBits, 0U, l_memoryType))
    {
        LogLine("CVulkanContext: no memory type for imported image");
        vkDestroyImage(m_device, f_image, nullptr);
        f_image = VK_NULL_HANDLE;
        close(f_fd);
        return false;
    }

    VkMemoryDedicatedAllocateInfo l_dedicatedInfo = { VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO };
    l_dedicatedInfo.image = f_image;
    VkImportMemoryFdInfoKHR l_importInfo = { VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR };
    l_importInfo.pNext = &l_dedicatedInfo;
    l_importInfo.handleType = f_handleType;
    l_importInfo.fd = f_fd; // ownership passes to Vulkan on success
    VkMemoryAllocateInfo l_allocateInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    l_allocateInfo.pNext = &l_importInfo;
    l_allocateInfo.allocationSize = l_requirements.size;
    l_allocateInfo.memoryTypeIndex = l_memoryType;
    l_result = vkAllocateMemory(m_device, &l_allocateInfo, nullptr, &f_memory);
    if(l_result != VK_SUCCESS)
    {
        LogLine("CVulkanContext: vkAllocateMemory(import, handleType=%d) failed, VkResult=%d", f_handleType, l_result);
        vkDestroyImage(m_device, f_image, nullptr);
        f_image = VK_NULL_HANDLE;
        close(f_fd);
        return false;
    }
    l_result = vkBindImageMemory(m_device, f_image, f_memory, 0U);
    if(l_result != VK_SUCCESS)
    {
        LogLine("CVulkanContext: vkBindImageMemory(import) failed, VkResult=%d", l_result);
        DestroyImage(f_image, f_memory);
        return false;
    }
    return true;
}

bool CVulkanContext::CreateOutputImage(uint32_t f_width, uint32_t f_height, VkFormat f_format,
    VkImage &f_image, VkDeviceMemory &f_memory)
{
    f_image = VK_NULL_HANDLE;
    f_memory = VK_NULL_HANDLE;

    VkImageCreateInfo l_imageInfo = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    l_imageInfo.imageType = VK_IMAGE_TYPE_2D;
    l_imageInfo.format = f_format;
    l_imageInfo.extent = { f_width, f_height, 1U };
    l_imageInfo.mipLevels = 1U;
    l_imageInfo.arrayLayers = 1U;
    l_imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    l_imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    l_imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    l_imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    l_imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if(vkCreateImage(m_device, &l_imageInfo, nullptr, &f_image) != VK_SUCCESS) return false;

    VkMemoryRequirements l_requirements;
    vkGetImageMemoryRequirements(m_device, f_image, &l_requirements);
    uint32_t l_memoryType = 0U;
    if(!FindMemoryType(l_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, l_memoryType) &&
        !FindMemoryType(l_requirements.memoryTypeBits, 0U, l_memoryType))
    {
        vkDestroyImage(m_device, f_image, nullptr);
        f_image = VK_NULL_HANDLE;
        return false;
    }
    VkMemoryAllocateInfo l_allocateInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    l_allocateInfo.allocationSize = l_requirements.size;
    l_allocateInfo.memoryTypeIndex = l_memoryType;
    if((vkAllocateMemory(m_device, &l_allocateInfo, nullptr, &f_memory) != VK_SUCCESS) ||
        (vkBindImageMemory(m_device, f_image, f_memory, 0U) != VK_SUCCESS))
    {
        DestroyImage(f_image, f_memory);
        return false;
    }
    return true;
}

bool CVulkanContext::CreateStagingBuffer(VkDeviceSize f_size, VkBuffer &f_buffer, VkDeviceMemory &f_memory, void *&f_mapped)
{
    f_buffer = VK_NULL_HANDLE;
    f_memory = VK_NULL_HANDLE;
    f_mapped = nullptr;

    VkBufferCreateInfo l_bufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    l_bufferInfo.size = f_size;
    l_bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    l_bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if(vkCreateBuffer(m_device, &l_bufferInfo, nullptr, &f_buffer) != VK_SUCCESS) return false;

    VkMemoryRequirements l_requirements;
    vkGetBufferMemoryRequirements(m_device, f_buffer, &l_requirements);
    // Coherent keeps the CPU read simple; cached makes the memcpy out fast
    uint32_t l_memoryType = 0U;
    if(!FindMemoryType(l_requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT, l_memoryType) &&
        !FindMemoryType(l_requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, l_memoryType))
    {
        vkDestroyBuffer(m_device, f_buffer, nullptr);
        f_buffer = VK_NULL_HANDLE;
        return false;
    }
    VkMemoryAllocateInfo l_allocateInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    l_allocateInfo.allocationSize = l_requirements.size;
    l_allocateInfo.memoryTypeIndex = l_memoryType;
    if((vkAllocateMemory(m_device, &l_allocateInfo, nullptr, &f_memory) != VK_SUCCESS) ||
        (vkBindBufferMemory(m_device, f_buffer, f_memory, 0U) != VK_SUCCESS) ||
        (vkMapMemory(m_device, f_memory, 0U, VK_WHOLE_SIZE, 0U, &f_mapped) != VK_SUCCESS))
    {
        DestroyBuffer(f_buffer, f_memory);
        return false;
    }
    return true;
}

void CVulkanContext::DestroyImage(VkImage &f_image, VkDeviceMemory &f_memory)
{
    if(f_image != VK_NULL_HANDLE) vkDestroyImage(m_device, f_image, nullptr);
    if(f_memory != VK_NULL_HANDLE) vkFreeMemory(m_device, f_memory, nullptr);
    f_image = VK_NULL_HANDLE;
    f_memory = VK_NULL_HANDLE;
}

void CVulkanContext::DestroyBuffer(VkBuffer &f_buffer, VkDeviceMemory &f_memory)
{
    if(f_buffer != VK_NULL_HANDLE) vkDestroyBuffer(m_device, f_buffer, nullptr);
    if(f_memory != VK_NULL_HANDLE) vkFreeMemory(m_device, f_memory, nullptr);
    f_buffer = VK_NULL_HANDLE;
    f_memory = VK_NULL_HANDLE;
}

#pragma once

#include "volk.h"

// Owns the driver's private Vulkan device inside vrserver and the machinery to
// import SteamVR's IPC-shared images (fd -> VkImage). Loaded through volk via
// dlopen so a missing libvulkan degrades to headless instead of failing to load
// the driver module.
class CVulkanContext final
{
public:
    ~CVulkanContext();

    bool Init(); // safe to call repeatedly; true once initialized
    void Shutdown();
    bool IsReady() const { return m_device != VK_NULL_HANDLE; }

    VkDevice GetDevice() const { return m_device; }
    VkQueue GetQueue() const { return m_queue; }
    uint32_t GetQueueFamily() const { return m_queueFamily; }
    bool HasForeignQueueFamily() const { return m_hasQueueFamilyForeign; }

    // Imports a shared image's fd (from IVRIPCResourceManagerClient::ReceiveSharedFd).
    // Ownership of f_fd passes to this call on success AND failure.
    // Tries an opaque-fd import first, then a dmabuf import.
    bool ImportSharedImage(int f_fd, uint32_t f_width, uint32_t f_height, VkFormat f_format,
        VkImage &f_image, VkDeviceMemory &f_memory);

    bool CreateOutputImage(uint32_t f_width, uint32_t f_height, VkFormat f_format,
        VkImage &f_image, VkDeviceMemory &f_memory);
    // Host-visible, persistently mapped
    bool CreateStagingBuffer(VkDeviceSize f_size, VkBuffer &f_buffer, VkDeviceMemory &f_memory, void *&f_mapped);

    void DestroyImage(VkImage &f_image, VkDeviceMemory &f_memory);
    void DestroyBuffer(VkBuffer &f_buffer, VkDeviceMemory &f_memory);

    // Two pre-allocated command buffers with fences, used in ping-pong by the
    // per-frame copy pipeline
    VkCommandBuffer GetCommandBuffer(uint32_t f_parity) const { return m_commandBuffers[f_parity & 1U]; }
    VkFence GetFence(uint32_t f_parity) const { return m_fences[f_parity & 1U]; }

private:
    bool PickPhysicalDevice();
    bool FindMemoryType(uint32_t f_typeBits, VkMemoryPropertyFlags f_wanted, uint32_t &f_index) const;
    bool ImportWithHandleType(int f_fd, uint32_t f_width, uint32_t f_height, VkFormat f_format,
        VkExternalMemoryHandleTypeFlagBits f_handleType, VkImage &f_image, VkDeviceMemory &f_memory);

    bool m_volkReady = false;
    VkInstance m_instance = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    VkQueue m_queue = VK_NULL_HANDLE;
    uint32_t m_queueFamily = 0U;
    VkCommandPool m_commandPool = VK_NULL_HANDLE;
    VkCommandBuffer m_commandBuffers[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
    VkFence m_fences[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
    bool m_hasQueueFamilyForeign = false;
    bool m_initFailed = false; // one attempt only; vrserver must not retry-spam
};

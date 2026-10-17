#include "stdafx.h"

#include <cstdarg>
#include <poll.h>
#include <time.h>
#include <unistd.h>

#include "linux/CDirectModeOutput.h"

// The published pixel format; every source format is blit-converted into this
static constexpr VkFormat g_outputFormat = VK_FORMAT_R8G8B8A8_SRGB;

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

CDirectModeOutput::CDirectModeOutput(uint32_t f_width, uint32_t f_height, float f_fps)
{
    m_width = f_width;
    m_height = f_height;
    m_fps = (f_fps > 1.f) ? f_fps : 60.f;
}

CDirectModeOutput::~CDirectModeOutput()
{
    Shutdown();
}

void CDirectModeOutput::Shutdown()
{
    std::lock_guard<std::mutex> l_lock(m_stateLock);
    if(m_shuttingDown) return;
    m_shuttingDown = true;

    for(auto &l_set : m_sets) ReleaseSet(l_set.get());
    m_sets.clear();

    if(m_vk.IsReady())
    {
        vkDeviceWaitIdle(m_vk.GetDevice());
        m_vk.DestroyImage(m_outputImage, m_outputMemory);
        m_vk.DestroyBuffer(m_staging, m_stagingMemory);
        m_stagingMapped = nullptr;
    }
    m_vk.Shutdown();
    m_publisher.Destroy();
    m_resourcesReady = false;
}

double CDirectModeOutput::GetSeconds()
{
    timespec l_time;
    clock_gettime(CLOCK_MONOTONIC, &l_time);
    return static_cast<double>(l_time.tv_sec) + (static_cast<double>(l_time.tv_nsec) * 1e-9);
}

VkFormat CDirectModeOutput::TranslateSwapFormat(uint32_t f_raw)
{
    switch(f_raw)
    {
        case VK_FORMAT_R8G8B8A8_UNORM:
        case VK_FORMAT_R8G8B8A8_SRGB:
        case VK_FORMAT_B8G8R8A8_UNORM:
        case VK_FORMAT_B8G8R8A8_SRGB:
        case VK_FORMAT_R16G16B16A16_SFLOAT:
            return static_cast<VkFormat>(f_raw);
        // The header documents nFormat "in DXGI_FORMAT"; belt and braces in
        // case the Linux compositor really sends those
        case 10: return VK_FORMAT_R16G16B16A16_SFLOAT; // DXGI_FORMAT_R16G16B16A16_FLOAT
        case 28: return VK_FORMAT_R8G8B8A8_UNORM;      // DXGI_FORMAT_R8G8B8A8_UNORM
        case 29: return VK_FORMAT_R8G8B8A8_SRGB;       // DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
        case 87: return VK_FORMAT_B8G8R8A8_UNORM;      // DXGI_FORMAT_B8G8R8A8_UNORM
        case 91: return VK_FORMAT_B8G8R8A8_SRGB;       // DXGI_FORMAT_B8G8R8A8_UNORM_SRGB
        default:
            LogLine("CDirectModeOutput: unknown swap texture format %u, assuming R8G8B8A8_SRGB", f_raw);
            return VK_FORMAT_R8G8B8A8_SRGB;
    }
}

bool CDirectModeOutput::EnsureResources()
{
    if(m_resourcesReady) return true;

    // The resource manager is initialized asynchronously by vrserver; a null
    // here is retried on the next call rather than latched
    if(!vr::VRIPCResourceManager())
    {
        if(!m_resourceFailureLogged)
        {
            LogLine("CDirectModeOutput: IVRIPCResourceManagerClient not (yet) available");
            m_resourceFailureLogged = true;
        }
        return false;
    }

    if(!m_vk.Init()) return false;
    if(m_outputImage == VK_NULL_HANDLE)
    {
        if(!m_vk.CreateOutputImage(m_width, m_height, g_outputFormat, m_outputImage, m_outputMemory))
        {
            LogLine("CDirectModeOutput: output image creation failed");
            return false;
        }
    }
    if(m_staging == VK_NULL_HANDLE)
    {
        if(!m_vk.CreateStagingBuffer(static_cast<VkDeviceSize>(m_width) * m_height * 4U, m_staging, m_stagingMemory, m_stagingMapped))
        {
            LogLine("CDirectModeOutput: staging buffer creation failed");
            return false;
        }
    }
    if(!m_publisher.Create(m_width, m_height, static_cast<uint32_t>(g_outputFormat)))
    {
        LogLine("CDirectModeOutput: shm frame block creation failed");
        return false;
    }

    LogLine("CDirectModeOutput: frame pipeline ready (%ux%u @ %.1f fps, pid=%d)", m_width, m_height, m_fps, getpid());
    m_resourcesReady = true;
    return true;
}

void CDirectModeOutput::CreateSwapTextureSet(uint32_t unPid, const SwapTextureSetDesc_t *pSwapTextureSetDesc, SwapTextureSet_t *pOutSwapTextureSet)
{
    std::lock_guard<std::mutex> l_lock(m_stateLock);

    memset(pOutSwapTextureSet, 0, sizeof(SwapTextureSet_t));
    LogLine("CDirectModeOutput: CreateSwapTextureSet pid=%u %ux%u format=%u samples=%u",
        unPid, pSwapTextureSetDesc->nWidth, pSwapTextureSetDesc->nHeight, pSwapTextureSetDesc->nFormat, pSwapTextureSetDesc->nSampleCount);
    if(m_shuttingDown || !EnsureResources()) return; // headless: tracking keeps working without frames

    auto l_set = std::make_unique<SwapSet>();
    l_set->m_pid = unPid;
    l_set->m_width = pSwapTextureSetDesc->nWidth;
    l_set->m_height = pSwapTextureSetDesc->nHeight;
    l_set->m_format = TranslateSwapFormat(pSwapTextureSetDesc->nFormat);
    if(pSwapTextureSetDesc->nSampleCount > 1U) LogLine("CDirectModeOutput: multisampled swap set requested (%u), allocating single-sampled", pSwapTextureSetDesc->nSampleCount);

    vr::IVRIPCResourceManagerClient *l_resources = vr::VRIPCResourceManager();
    for(int i = 0; i < 3; i++)
    {
        SwapSlot &l_slot = l_set->m_slots[i];
        if(!l_resources->NewSharedVulkanImage(static_cast<uint32_t>(l_set->m_format), l_set->m_width, l_set->m_height,
            true /* renderable */, false /* mappable */, false /* compute */, 1U, 1U,
            0U, VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, &l_slot.m_handle))
        {
            LogLine("CDirectModeOutput: NewSharedVulkanImage failed (slot %d)", i);
            ReleaseSet(l_set.get());
            return;
        }

        uint64_t l_ipcHandle = 0U;
        int l_fd = -1;
        if(!l_resources->RefResource(l_slot.m_handle, &l_ipcHandle) || !l_resources->ReceiveSharedFd(l_ipcHandle, &l_fd) || (l_fd < 0))
        {
            LogLine("CDirectModeOutput: resolving shared image to fd failed (slot %d)", i);
            ReleaseSet(l_set.get());
            return;
        }
        l_slot.m_syncFd = dup(l_fd); // the import consumes l_fd
        if(!m_vk.ImportSharedImage(l_fd, l_set->m_width, l_set->m_height, l_set->m_format, l_slot.m_image, l_slot.m_memory))
        {
            ReleaseSet(l_set.get());
            return;
        }
    }

    for(int i = 0; i < 3; i++) pOutSwapTextureSet->rSharedTextureHandles[i] = l_set->m_slots[i].m_handle;
    pOutSwapTextureSet->unTextureFlags = 0U;
    m_sets.push_back(std::move(l_set));
}

void CDirectModeOutput::ReleaseSet(SwapSet *f_set)
{
    if(m_vk.IsReady()) vkQueueWaitIdle(m_vk.GetQueue());
    vr::IVRIPCResourceManagerClient *l_resources = vr::VRIPCResourceManager();
    for(SwapSlot &l_slot : f_set->m_slots)
    {
        m_vk.DestroyImage(l_slot.m_image, l_slot.m_memory);
        if(l_slot.m_syncFd >= 0) close(l_slot.m_syncFd);
        l_slot.m_syncFd = -1;
        if(l_slot.m_handle && l_resources)
        {
            l_resources->UnrefResource(l_slot.m_handle); // our RefResource
            l_resources->UnrefResource(l_slot.m_handle); // the creation reference
        }
        l_slot.m_handle = 0U;
    }
    // A released set may still be referenced by m_scene until the next Present
    for(LayerEye &l_eye : m_scene) l_eye.m_handle = 0U;
}

bool CDirectModeOutput::FindSlot(vr::SharedTextureHandle_t f_handle, SwapSet *&f_set, uint32_t &f_slot)
{
    for(auto &l_set : m_sets)
    {
        for(uint32_t i = 0U; i < 3U; i++)
        {
            if(l_set->m_slots[i].m_handle == f_handle)
            {
                f_set = l_set.get();
                f_slot = i;
                return true;
            }
        }
    }
    return false;
}

void CDirectModeOutput::DestroySwapTextureSet(vr::SharedTextureHandle_t sharedTextureHandle)
{
    std::lock_guard<std::mutex> l_lock(m_stateLock);
    for(auto l_iter = m_sets.begin(); l_iter != m_sets.end(); ++l_iter)
    {
        for(const SwapSlot &l_slot : (*l_iter)->m_slots)
        {
            if(l_slot.m_handle == sharedTextureHandle)
            {
                ReleaseSet(l_iter->get());
                m_sets.erase(l_iter);
                return;
            }
        }
    }
}

void CDirectModeOutput::DestroyAllSwapTextureSets(uint32_t unPid)
{
    std::lock_guard<std::mutex> l_lock(m_stateLock);
    for(auto l_iter = m_sets.begin(); l_iter != m_sets.end();)
    {
        if((*l_iter)->m_pid == unPid)
        {
            ReleaseSet(l_iter->get());
            l_iter = m_sets.erase(l_iter);
        }
        else ++l_iter;
    }
}

void CDirectModeOutput::GetNextSwapTextureSetIndex(vr::SharedTextureHandle_t sharedTextureHandles[2], uint32_t (*pIndices)[2])
{
    std::lock_guard<std::mutex> l_lock(m_stateLock);
    SwapSet *l_previousSet = nullptr;
    for(int i = 0; i < 2; i++)
    {
        (*pIndices)[i] = 0U;
        SwapSet *l_set = nullptr;
        uint32_t l_slot = 0U;
        if(!FindSlot(sharedTextureHandles[i], l_set, l_slot)) continue;
        if(l_set == l_previousSet)
        {
            // Both eyes in one texture (side-by-side single set): same index
            (*pIndices)[i] = (*pIndices)[i - 1];
            continue;
        }
        (*pIndices)[i] = l_set->m_nextIndex;
        l_set->m_nextIndex = (l_set->m_nextIndex + 1U) % 3U;
        l_previousSet = l_set;
    }
}

void CDirectModeOutput::SubmitLayer(const SubmitLayerPerEye_t (&perEye)[2])
{
    std::lock_guard<std::mutex> l_lock(m_stateLock);
    if(m_layersThisFrame == 0U)
    {
        // Layer 0 is the scene; overlays would have to be composited on top and are ignored
        for(int i = 0; i < 2; i++)
        {
            m_scene[i].m_handle = perEye[i].hTexture;
            m_scene[i].m_bounds = perEye[i].bounds;
        }
    }
    else if(!m_overlayLayersLogged)
    {
        LogLine("CDirectModeOutput: overlay layers submitted; only the scene layer is captured");
        m_overlayLayersLogged = true;
    }
    m_layersThisFrame++;
}

void CDirectModeOutput::Present(vr::SharedTextureHandle_t /* syncTexture */)
{
    std::lock_guard<std::mutex> l_lock(m_stateLock);

    LayerEye l_scene[2] = { m_scene[0], m_scene[1] };
    m_layersThisFrame = 0U;
    if(m_presentCount == 0U) LogLine("CDirectModeOutput: first Present (scene=%llu/%llu)",
        static_cast<unsigned long long>(l_scene[0].m_handle), static_cast<unsigned long long>(l_scene[1].m_handle));
    m_presentCount++;

    m_framePending = false;
    if(m_shuttingDown || !m_resourcesReady || (l_scene[0].m_handle == 0U)) return;

    SwapSet *l_sets[2] = {};
    uint32_t l_slots[2] = {};
    for(int i = 0; i < 2; i++)
    {
        if(!FindSlot(l_scene[i].m_handle, l_sets[i], l_slots[i])) return;
    }

    // Kernel implicit sync: the submitting app's GPU writes attach fences to the
    // shared BO; readable means every writer has finished
    for(int i = 0; i < 2; i++)
    {
        const int l_syncFd = l_sets[i]->m_slots[l_slots[i]].m_syncFd;
        if(l_syncFd >= 0)
        {
            pollfd l_wait = { l_syncFd, POLLIN, 0 };
            poll(&l_wait, 1U, 20);
        }
        if(i == 0 && l_sets[1] == l_sets[0]) break;
    }

    // Ping-pong command buffers; the fence of this parity was waited in the
    // previous PostPresent of the same parity (or is still signaled from Init)
    VkDevice l_device = m_vk.GetDevice();
    VkCommandBuffer l_command = m_vk.GetCommandBuffer(m_frameParity);
    VkFence l_fence = m_vk.GetFence(m_frameParity);
    if(vkWaitForFences(l_device, 1U, &l_fence, VK_TRUE, 100000000ULL) != VK_SUCCESS) return; // 100ms; a stuck GPU must not wedge vrserver
    vkResetFences(l_device, 1U, &l_fence);
    vkResetCommandBuffer(l_command, 0U);

    VkCommandBufferBeginInfo l_beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    l_beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(l_command, &l_beginInfo);

    const uint32_t l_foreignFamily = m_vk.HasForeignQueueFamily() ? VK_QUEUE_FAMILY_FOREIGN_EXT : VK_QUEUE_FAMILY_EXTERNAL;
    const uint32_t l_ownFamily = m_vk.GetQueueFamily();
    const bool l_singleSource = (l_sets[1] == l_sets[0]) && (l_slots[1] == l_slots[0]);
    const int l_sourceCount = l_singleSource ? 1 : 2;

    // Acquire the externally-owned images; GENERAL as the safe superset of
    // whatever layout the writer left them in
    VkImageMemoryBarrier l_barriers[3] = {};
    for(int i = 0; i < l_sourceCount; i++)
    {
        l_barriers[i] = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        l_barriers[i].srcAccessMask = 0U;
        l_barriers[i].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        l_barriers[i].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        l_barriers[i].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        l_barriers[i].srcQueueFamilyIndex = l_foreignFamily;
        l_barriers[i].dstQueueFamilyIndex = l_ownFamily;
        l_barriers[i].image = l_sets[i]->m_slots[l_slots[i]].m_image;
        l_barriers[i].subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0U, 1U, 0U, 1U };
    }
    l_barriers[l_sourceCount] = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    l_barriers[l_sourceCount].srcAccessMask = 0U;
    l_barriers[l_sourceCount].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    l_barriers[l_sourceCount].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; // previous content is discarded
    l_barriers[l_sourceCount].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    l_barriers[l_sourceCount].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    l_barriers[l_sourceCount].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    l_barriers[l_sourceCount].image = m_outputImage;
    l_barriers[l_sourceCount].subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0U, 1U, 0U, 1U };
    vkCmdPipelineBarrier(l_command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0U, 0U, nullptr, 0U, nullptr, static_cast<uint32_t>(l_sourceCount + 1), l_barriers);

    // Each eye's submitted bounds -> its half of the output, scale + vertical
    // flip (vMin > vMax on GL-sourced textures) handled by the blit itself
    const int32_t l_halfWidth = static_cast<int32_t>(m_width / 2U);
    for(int i = 0; i < 2; i++)
    {
        const SwapSet *l_set = l_sets[i];
        const vr::VRTextureBounds_t &l_bounds = l_scene[i].m_bounds;
        VkImageBlit l_blit = {};
        l_blit.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0U, 0U, 1U };
        l_blit.srcOffsets[0] = { static_cast<int32_t>(l_bounds.uMin * l_set->m_width), static_cast<int32_t>(l_bounds.vMin * l_set->m_height), 0 };
        l_blit.srcOffsets[1] = { static_cast<int32_t>(l_bounds.uMax * l_set->m_width), static_cast<int32_t>(l_bounds.vMax * l_set->m_height), 1 };
        l_blit.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0U, 0U, 1U };
        l_blit.dstOffsets[0] = { i * l_halfWidth, 0, 0 };
        l_blit.dstOffsets[1] = { (i + 1) * l_halfWidth, static_cast<int32_t>(m_height), 1 };
        vkCmdBlitImage(l_command,
            l_set->m_slots[l_slots[i]].m_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            m_outputImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1U, &l_blit, VK_FILTER_LINEAR);
    }

    VkImageMemoryBarrier l_toSource = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    l_toSource.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    l_toSource.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    l_toSource.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    l_toSource.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    l_toSource.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    l_toSource.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    l_toSource.image = m_outputImage;
    l_toSource.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0U, 1U, 0U, 1U };
    vkCmdPipelineBarrier(l_command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0U, 0U, nullptr, 0U, nullptr, 1U, &l_toSource);

    VkBufferImageCopy l_copy = {};
    l_copy.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0U, 0U, 1U };
    l_copy.imageExtent = { m_width, m_height, 1U };
    vkCmdCopyImageToBuffer(l_command, m_outputImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_staging, 1U, &l_copy);

    // Release the imported images back to their external owner
    for(int i = 0; i < l_sourceCount; i++)
    {
        l_barriers[i].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        l_barriers[i].dstAccessMask = 0U;
        l_barriers[i].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        l_barriers[i].newLayout = VK_IMAGE_LAYOUT_GENERAL;
        l_barriers[i].srcQueueFamilyIndex = l_ownFamily;
        l_barriers[i].dstQueueFamilyIndex = l_foreignFamily;
    }
    vkCmdPipelineBarrier(l_command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        0U, 0U, nullptr, 0U, nullptr, static_cast<uint32_t>(l_sourceCount), l_barriers);

    vkEndCommandBuffer(l_command);

    VkSubmitInfo l_submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    l_submitInfo.commandBufferCount = 1U;
    l_submitInfo.pCommandBuffers = &l_command;
    if(vkQueueSubmit(m_vk.GetQueue(), 1U, &l_submitInfo, l_fence) == VK_SUCCESS) m_framePending = true;
}

void CDirectModeOutput::PostPresent(const Throttling_t * /* pThrottling */)
{
    {
        std::lock_guard<std::mutex> l_lock(m_stateLock);
        if(m_framePending)
        {
            VkFence l_fence = m_vk.GetFence(m_frameParity);
            if(vkWaitForFences(m_vk.GetDevice(), 1U, &l_fence, VK_TRUE, 100000000ULL) == VK_SUCCESS)
            {
                m_publisher.PublishFrame(m_stagingMapped);
            }
            m_framePending = false;
            m_frameParity ^= 1U;
        }
    }

    // "Take more time until vsync": this is the virtual panel's refresh
    SleepToNextVsync();
}

void CDirectModeOutput::SleepToNextVsync()
{
    const double l_interval = 1.0 / static_cast<double>(m_fps);
    const double l_now = GetSeconds();
    if(m_lastVsync == 0.0) m_lastVsync = l_now;

    // Advance in whole intervals; every skipped one is a dropped frame
    double l_next = m_lastVsync + l_interval;
    uint32_t l_dropped = 0U;
    while(l_next <= l_now)
    {
        l_next += l_interval;
        l_dropped++;
    }
    m_droppedLastFrame = (l_dropped > 0U) ? (l_dropped - 1U) : 0U;

    for(;;)
    {
        const double l_remaining = l_next - GetSeconds();
        if(l_remaining <= 0.0) break;
        if(l_remaining > 0.002)
        {
            const timespec l_sleep = { 0, 1000000 }; // 1ms slices
            clock_nanosleep(CLOCK_MONOTONIC, 0, &l_sleep, nullptr);
        }
        else std::this_thread::yield();
    }
    m_lastVsync = l_next;
}

void CDirectModeOutput::GetFrameTiming(vr::DriverDirectMode_FrameTiming *pFrameTiming)
{
    if(pFrameTiming->m_nSize >= sizeof(vr::DriverDirectMode_FrameTiming))
    {
        pFrameTiming->m_nNumFramePresents = 1U;
        pFrameTiming->m_nNumMisPresented = 0U;
        pFrameTiming->m_nNumDroppedFrames = m_droppedLastFrame;
    }
    // Flags overlap VRCompositor_ThrottleMask and must always be cleared
    pFrameTiming->m_nReprojectionFlags = 0U;
}

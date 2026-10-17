#include "stdafx.h"

#include <atomic>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "linux/CFramePublisher.h"
#include "../shared/SharedFrameInfo.h"

CFramePublisher::~CFramePublisher()
{
    Destroy();
}

bool CFramePublisher::Create(uint32_t f_width, uint32_t f_height, uint32_t f_pixelFormat)
{
    if(m_header) return true;

    const uint32_t l_rowPitch = f_width * 4U;
    m_slotSize = l_rowPitch * f_height;
    const uint32_t l_headerSize = (sizeof(SharedFramePixelsInfo) + 63U) & ~63U; // slot alignment
    m_mappingSize = l_headerSize + (2U * static_cast<size_t>(m_slotSize));

    const int l_fd = shm_open(g_sharedFramePixelsName, O_CREAT | O_RDWR, 0644);
    if(l_fd < 0) return false;
    if(ftruncate(l_fd, static_cast<off_t>(m_mappingSize)) != 0)
    {
        close(l_fd);
        shm_unlink(g_sharedFramePixelsName);
        return false;
    }
    void *l_mapping = mmap(nullptr, m_mappingSize, PROT_READ | PROT_WRITE, MAP_SHARED, l_fd, 0);
    close(l_fd); // the mapping keeps the object alive
    if(l_mapping == MAP_FAILED)
    {
        shm_unlink(g_sharedFramePixelsName);
        return false;
    }

    m_header = static_cast<SharedFramePixelsInfo*>(l_mapping);
    m_header->generation = 0U;
    m_header->width = f_width;
    m_header->height = f_height;
    m_header->pixelFormat = f_pixelFormat;
    m_header->rowPitch = l_rowPitch;
    m_header->slotOffset[0] = l_headerSize;
    m_header->slotOffset[1] = l_headerSize + m_slotSize;
    m_header->slotSize = m_slotSize;
    m_header->latestSlot = 0U;
    m_header->frameCounter = 0U;
    m_header->magic = g_sharedFramePixelsMagic;
    // Fields land before the generation a consumer keys on
    std::atomic_thread_fence(std::memory_order_release);
    m_header->generation = 1U;
    return true;
}

void CFramePublisher::Destroy()
{
    if(!m_header) return;
    munmap(m_header, m_mappingSize);
    m_header = nullptr;
    shm_unlink(g_sharedFramePixelsName);
}

void CFramePublisher::PublishFrame(const void *f_pixels)
{
    if(!m_header) return;
    const uint32_t l_slot = m_nextSlot;
    m_nextSlot ^= 1U;
    memcpy(reinterpret_cast<char*>(m_header) + m_header->slotOffset[l_slot], f_pixels, m_slotSize);
    m_header->latestSlot = l_slot;
    // Pixels and slot index land before the counter a consumer keys on
    std::atomic_thread_fence(std::memory_order_release);
    m_header->frameCounter = m_header->frameCounter + 1U;
}

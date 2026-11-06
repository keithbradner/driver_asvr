#include "CFrameReader.h"
#include "CPlatform.h"
#include "../shared/SharedFrameInfo.h"

#include <cstring>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

CFrameReader::~CFrameReader()
{
    Close();
}

void CFrameReader::Update()
{
    if(m_info)
    {
        if(!StillCurrent())
        {
            CPlatform::Log("Frame block re-created, mapping the new one");
            Close();
        }
        return;
    }
    if(m_retryCountdown-- > 0) return;
    m_retryCountdown = 60; // about once a second at the window's refresh rate
    TryOpen();
}

const unsigned char* CFrameReader::GetLatestPixels() const
{
    if(!m_info || (m_info->frameCounter == 0U)) return nullptr;
    const uint32_t l_offset = m_info->slotOffset[m_info->latestSlot & 1U];
    // Never read past the mapping, whatever the header says
    if((static_cast<size_t>(l_offset) + m_info->slotSize) > m_mappingSize) return nullptr;
    return reinterpret_cast<const unsigned char*>(m_info) + l_offset;
}

// Validates the header fields that fixed the mapping; platform parts below add
// their own identity check
static bool HeaderMatches(const SharedFramePixelsInfo *f_info, uint32_t f_generation, uint32_t f_slotEnd)
{
    if(f_info->magic != g_sharedFramePixelsMagic) return false;
    if(f_info->generation != f_generation) return false;
    return ((f_info->slotOffset[1] + f_info->slotSize) == f_slotEnd);
}

#ifdef _WIN32

bool CFrameReader::TryOpen()
{
    HANDLE l_mapping = OpenFileMappingA(FILE_MAP_READ, FALSE, g_sharedFramePixelsName);
    if(!l_mapping) return false;

    // Map the header first to learn the full block size
    SharedFramePixelsInfo l_header = {};
    const void *l_view = MapViewOfFile(l_mapping, FILE_MAP_READ, 0U, 0U, sizeof(SharedFramePixelsInfo));
    if(!l_view)
    {
        CloseHandle(l_mapping);
        return false;
    }
    memcpy(&l_header, l_view, sizeof(l_header));
    UnmapViewOfFile(l_view);
    if((l_header.magic != g_sharedFramePixelsMagic) || (l_header.slotSize == 0U) || (l_header.generation == 0U))
    {
        CloseHandle(l_mapping);
        return false; // exists but not initialised yet
    }

    const size_t l_size = static_cast<size_t>(l_header.slotOffset[1]) + l_header.slotSize;
    const void *l_full = MapViewOfFile(l_mapping, FILE_MAP_READ, 0U, 0U, l_size);
    if(!l_full)
    {
        CloseHandle(l_mapping);
        return false;
    }
    m_mapping = l_mapping;
    m_mappingSize = l_size;
    m_info = static_cast<const SharedFramePixelsInfo*>(l_full);
    m_mappedGeneration = l_header.generation;
    m_mappedSlotEnd = l_header.slotOffset[1] + l_header.slotSize;
    CPlatform::Log("Frame block mapped (" + std::to_string(l_header.width) + "x" + std::to_string(l_header.height) + ")");
    return true;
}

bool CFrameReader::StillCurrent()
{
    // The name always resolves to the object we hold (it lives while mapped),
    // so only an in-place re-initialisation by a new driver can change things
    return HeaderMatches(m_info, m_mappedGeneration, m_mappedSlotEnd);
}

void CFrameReader::Close()
{
    if(m_info)
    {
        UnmapViewOfFile(const_cast<SharedFramePixelsInfo*>(m_info));
        m_info = nullptr;
    }
    if(m_mapping)
    {
        CloseHandle(static_cast<HANDLE>(m_mapping));
        m_mapping = nullptr;
    }
    m_mappingSize = 0U;
    m_retryCountdown = 0;
}

#else

bool CFrameReader::TryOpen()
{
    const int l_fd = shm_open(g_sharedFramePixelsName, O_RDONLY, 0);
    if(l_fd < 0) return false;

    SharedFramePixelsInfo l_header = {};
    void *l_view = mmap(nullptr, sizeof(SharedFramePixelsInfo), PROT_READ, MAP_SHARED, l_fd, 0);
    if(l_view == MAP_FAILED)
    {
        close(l_fd);
        return false;
    }
    memcpy(&l_header, l_view, sizeof(l_header));
    munmap(l_view, sizeof(SharedFramePixelsInfo));
    struct stat l_stat = {};
    const bool l_statOk = (fstat(l_fd, &l_stat) == 0);
    if(!l_statOk || (l_header.magic != g_sharedFramePixelsMagic) || (l_header.slotSize == 0U) || (l_header.generation == 0U))
    {
        close(l_fd);
        return false;
    }

    const size_t l_size = static_cast<size_t>(l_header.slotOffset[1]) + l_header.slotSize;
    if(static_cast<size_t>(l_stat.st_size) < l_size)
    {
        close(l_fd); // header promises more than the object holds: not ours to trust
        return false;
    }
    const void *l_full = mmap(nullptr, l_size, PROT_READ, MAP_SHARED, l_fd, 0);
    close(l_fd);
    if(l_full == MAP_FAILED) return false;
    m_mappingSize = l_size;
    m_info = static_cast<const SharedFramePixelsInfo*>(l_full);
    m_mappedGeneration = l_header.generation;
    m_mappedSlotEnd = l_header.slotOffset[1] + l_header.slotSize;
    m_inode = static_cast<unsigned long>(l_stat.st_ino);
    m_identityCountdown = 60;
    CPlatform::Log("Frame block mapped (" + std::to_string(l_header.width) + "x" + std::to_string(l_header.height) + ")");
    return true;
}

bool CFrameReader::StillCurrent()
{
    if(!HeaderMatches(m_info, m_mappedGeneration, m_mappedSlotEnd)) return false;
    // A driver that starts after a crash unlinks the old object and creates a
    // new one under the same name; we would otherwise keep showing the old
    // one's last frame. Check the inode about once a second
    if(m_identityCountdown-- > 0) return true;
    m_identityCountdown = 60;
    const int l_fd = shm_open(g_sharedFramePixelsName, O_RDONLY, 0);
    if(l_fd < 0) return false; // gone: wait for the next one
    struct stat l_stat = {};
    const bool l_same = (fstat(l_fd, &l_stat) == 0) && (static_cast<unsigned long>(l_stat.st_ino) == m_inode);
    close(l_fd);
    return l_same;
}

void CFrameReader::Close()
{
    if(m_info)
    {
        munmap(const_cast<SharedFramePixelsInfo*>(m_info), m_mappingSize);
        m_info = nullptr;
    }
    m_mappingSize = 0U;
    m_retryCountdown = 0;
    m_inode = 0UL;
}

#endif

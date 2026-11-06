#pragma once

#include <cstdint>
#include <cstddef>

struct SharedFramePixelsInfo;

// Reader side of the driver's frame block (shared/SharedFrameInfo.h): a named
// file mapping on Windows, a POSIX shm object on Linux. Opening is retried
// about once a second until the driver has published, since the block only
// appears once the compositor starts presenting. A mapped block is dropped
// again as soon as its header no longer matches what was mapped (the driver
// re-initialised it) or, on Linux, the name points at a new object (a crashed
// driver left the old one behind); the next Update maps the live one.
class CFrameReader final
{
public:
    ~CFrameReader();

    void Update(); // call every frame while SteamVR is connected
    void Close();

    bool IsMapped() const { return m_info != nullptr; }
    const SharedFramePixelsInfo* GetInfo() const { return m_info; }
    // Newest complete frame, or nullptr before the first one
    const unsigned char* GetLatestPixels() const;

private:
    bool TryOpen();
    bool StillCurrent(); // the mapped block is still the one the driver publishes to

    const SharedFramePixelsInfo *m_info = nullptr;
    size_t m_mappingSize = 0U;
    int m_retryCountdown = 0;
    // Header fields as mapped; a change means the block was re-created
    uint32_t m_mappedGeneration = 0U;
    uint32_t m_mappedSlotEnd = 0U; // slotOffset[1] + slotSize
#ifdef _WIN32
    void *m_mapping = nullptr; // HANDLE
#else
    unsigned long m_inode = 0UL;
    int m_identityCountdown = 0;
#endif
};

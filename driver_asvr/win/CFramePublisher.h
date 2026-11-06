#pragma once

// Publishes finished frames to consumers (asvr_monitor, asvr_ctl) through the
// named file mapping described in shared/SharedFrameInfo.h: fixed geometry
// header plus two pixel slots written in alternation. Same interface and
// release-fence discipline as the Linux shm publisher.
struct SharedFramePixelsInfo;

class CFramePublisher final
{
public:
    ~CFramePublisher();

    bool Create(uint32_t f_width, uint32_t f_height, uint32_t f_pixelFormat);
    void Destroy();
    bool IsReady() const { return m_header != nullptr; }

    // f_pixels must be rowPitch * height tightly packed bytes
    void PublishFrame(const void *f_pixels);

private:
    HANDLE m_mapping = NULL;
    SharedFramePixelsInfo *m_header = nullptr;
    size_t m_mappingSize = 0U;
    uint32_t m_slotSize = 0U;
    uint32_t m_nextSlot = 0U;
};

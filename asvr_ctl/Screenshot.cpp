#include "stdafx.h"

#include "Screenshot.h"
#include "../shared/SharedFrameInfo.h"

// Grabs the compositor output the driver publishes through SharedFrameInfo and
// saves it as PNG. The texture is created with legacy D3D11_RESOURCE_MISC_SHARED
// (no NT handle, no keyed mutex), which means:
// - it can only be opened by a device on the adapter that created it, so we try
//   adapters in the same order the monitor prefers them (hardware, no Microsoft
//   Basic/remote display, largest VRAM first);
// - there is no cross-process synchronization, so a copy can tear against the
//   compositor's own CopyResource; frameCounter is compared around the copy to
//   detect and retry that.

template<class T> class CComHolder
{
public:
    T *m_ptr = nullptr;
    ~CComHolder() { if(m_ptr) m_ptr->Release(); }
    T** operator&() { return &m_ptr; }
    T* operator->() const { return m_ptr; }
    operator T*() const { return m_ptr; }
};

static float HalfToFloat(uint16_t f_half)
{
    const uint32_t l_sign = (f_half & 0x8000U) << 16U;
    uint32_t l_exponent = (f_half >> 10U) & 0x1FU;
    uint32_t l_mantissa = f_half & 0x3FFU;
    if(l_exponent == 0U)
    {
        if(l_mantissa == 0U) l_exponent = 0U;
        else
        {
            // Subnormal half: normalize
            while(!(l_mantissa & 0x400U))
            {
                l_mantissa <<= 1U;
                l_exponent--;
            }
            l_exponent++;
            l_mantissa &= 0x3FFU;
            l_exponent += 127U - 15U;
        }
    }
    else if(l_exponent == 0x1FU) l_exponent = 0xFFU; // inf/nan
    else l_exponent += 127U - 15U;
    const uint32_t l_bits = l_sign | (l_exponent << 23U) | (l_mantissa << 13U);
    float l_result;
    memcpy(&l_result, &l_bits, sizeof(float));
    return l_result;
}

static uint8_t LinearToSrgb8(float f_value)
{
    const float l_clamped = std::min(std::max(f_value, 0.f), 1.f);
    const float l_srgb = (l_clamped <= 0.0031308f) ? (l_clamped * 12.92f) : (1.055f * powf(l_clamped, 1.f / 2.4f) - 0.055f);
    return static_cast<uint8_t>(l_srgb * 255.f + 0.5f);
}

// Row of source pixels -> tightly packed BGRA8
static bool ConvertRow(DXGI_FORMAT f_format, const uint8_t *f_source, uint8_t *f_target, uint32_t f_width)
{
    switch(f_format)
    {
        case DXGI_FORMAT_B8G8R8A8_UNORM:
            memcpy(f_target, f_source, static_cast<size_t>(f_width) * 4U);
            break;
        case DXGI_FORMAT_R8G8B8A8_UNORM:
            for(uint32_t i = 0U; i < f_width; i++)
            {
                f_target[i * 4U + 0U] = f_source[i * 4U + 2U];
                f_target[i * 4U + 1U] = f_source[i * 4U + 1U];
                f_target[i * 4U + 2U] = f_source[i * 4U + 0U];
                f_target[i * 4U + 3U] = f_source[i * 4U + 3U];
            }
            break;
        case DXGI_FORMAT_R10G10B10A2_UNORM:
            for(uint32_t i = 0U; i < f_width; i++)
            {
                uint32_t l_pixel;
                memcpy(&l_pixel, f_source + (i * 4U), sizeof(uint32_t));
                f_target[i * 4U + 0U] = static_cast<uint8_t>(((l_pixel >> 20U) & 0x3FFU) >> 2U);
                f_target[i * 4U + 1U] = static_cast<uint8_t>(((l_pixel >> 10U) & 0x3FFU) >> 2U);
                f_target[i * 4U + 2U] = static_cast<uint8_t>((l_pixel & 0x3FFU) >> 2U);
                f_target[i * 4U + 3U] = 255U;
            }
            break;
        case DXGI_FORMAT_R16G16B16A16_FLOAT:
            // scRGB-style linear output; approximate with an sRGB encode
            for(uint32_t i = 0U; i < f_width; i++)
            {
                const uint16_t *l_pixel = reinterpret_cast<const uint16_t*>(f_source + (i * 8U));
                f_target[i * 4U + 0U] = LinearToSrgb8(HalfToFloat(l_pixel[2U]));
                f_target[i * 4U + 1U] = LinearToSrgb8(HalfToFloat(l_pixel[1U]));
                f_target[i * 4U + 2U] = LinearToSrgb8(HalfToFloat(l_pixel[0U]));
                f_target[i * 4U + 3U] = 255U;
            }
            break;
        default:
            return false;
    }
    return true;
}

static bool OpenSharedTextureOnAnyAdapter(uint64_t f_sharedHandle, int f_forcedAdapter,
    ID3D11Device *&f_device, ID3D11DeviceContext *&f_context, ID3D11Texture2D *&f_texture)
{
    CComHolder<IDXGIFactory1> l_factory;
    if(FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&l_factory)))) return false;

    struct AdapterCandidate
    {
        int m_index;
        IDXGIAdapter1 *m_adapter;
        size_t m_vram;
        bool m_hardware;
    };
    std::vector<AdapterCandidate> l_candidates;
    for(UINT i = 0U; ; i++)
    {
        IDXGIAdapter1 *l_adapter = nullptr;
        if(l_factory->EnumAdapters1(i, &l_adapter) == DXGI_ERROR_NOT_FOUND) break;
        DXGI_ADAPTER_DESC1 l_desc;
        l_adapter->GetDesc1(&l_desc);
        AdapterCandidate l_candidate;
        l_candidate.m_index = static_cast<int>(i);
        l_candidate.m_adapter = l_adapter;
        l_candidate.m_vram = l_desc.DedicatedVideoMemory;
        l_candidate.m_hardware = !(l_desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) && (l_desc.VendorId != 0x1414U);
        l_candidates.push_back(l_candidate);
    }
    std::stable_sort(l_candidates.begin(), l_candidates.end(),
        [](const AdapterCandidate &f_a, const AdapterCandidate &f_b)
        {
            if(f_a.m_hardware != f_b.m_hardware) return f_a.m_hardware;
            return (f_a.m_vram > f_b.m_vram);
        });

    bool l_result = false;
    for(const auto &l_candidate : l_candidates)
    {
        if(l_result || ((f_forcedAdapter >= 0) && (l_candidate.m_index != f_forcedAdapter)))
        {
            l_candidate.m_adapter->Release();
            continue;
        }
        ID3D11Device *l_device = nullptr;
        ID3D11DeviceContext *l_context = nullptr;
        if(SUCCEEDED(D3D11CreateDevice(l_candidate.m_adapter, D3D_DRIVER_TYPE_UNKNOWN, NULL, 0U, NULL, 0U, D3D11_SDK_VERSION, &l_device, NULL, &l_context)))
        {
            ID3D11Texture2D *l_texture = nullptr;
            if(SUCCEEDED(l_device->OpenSharedResource(reinterpret_cast<HANDLE>(f_sharedHandle), __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&l_texture))))
            {
                f_device = l_device;
                f_context = l_context;
                f_texture = l_texture;
                l_result = true;
            }
            else
            {
                l_context->Release();
                l_device->Release();
            }
        }
        l_candidate.m_adapter->Release();
    }
    return l_result;
}

// argv arrives in the ANSI codepage
static std::wstring WidenPath(const std::string &f_path)
{
    std::wstring l_result;
    const int l_length = MultiByteToWideChar(CP_ACP, 0U, f_path.c_str(), -1, NULL, 0);
    if(l_length > 1)
    {
        l_result.resize(static_cast<size_t>(l_length));
        MultiByteToWideChar(CP_ACP, 0U, f_path.c_str(), -1, &l_result[0U], l_length);
        l_result.pop_back(); // drop the terminator
    }
    return l_result;
}

static bool SavePng(const wchar_t *f_path, const uint8_t *f_pixels, uint32_t f_width, uint32_t f_height)
{
    CComHolder<IWICImagingFactory> l_factory;
    if(FAILED(CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&l_factory)))) return false;
    CComHolder<IWICStream> l_stream;
    if(FAILED(l_factory->CreateStream(&l_stream))) return false;
    if(FAILED(l_stream->InitializeFromFilename(f_path, GENERIC_WRITE))) return false;
    CComHolder<IWICBitmapEncoder> l_encoder;
    if(FAILED(l_factory->CreateEncoder(GUID_ContainerFormatPng, NULL, &l_encoder))) return false;
    if(FAILED(l_encoder->Initialize(l_stream, WICBitmapEncoderNoCache))) return false;
    CComHolder<IWICBitmapFrameEncode> l_frame;
    if(FAILED(l_encoder->CreateNewFrame(&l_frame, NULL))) return false;
    if(FAILED(l_frame->Initialize(NULL))) return false;
    if(FAILED(l_frame->SetSize(f_width, f_height))) return false;
    WICPixelFormatGUID l_format = GUID_WICPixelFormat32bppBGRA;
    if(FAILED(l_frame->SetPixelFormat(&l_format))) return false;
    if(!IsEqualGUID(l_format, GUID_WICPixelFormat32bppBGRA)) return false;
    const UINT l_stride = f_width * 4U;
    if(FAILED(l_frame->WritePixels(f_height, l_stride, l_stride * f_height, const_cast<BYTE*>(f_pixels)))) return false;
    if(FAILED(l_frame->Commit())) return false;
    return SUCCEEDED(l_encoder->Commit());
}

int RunScreenshot(int argc, char *argv[])
{
    std::string l_outputPath;
    bool l_bothEyes = false;
    bool l_waitNewFrame = false;
    int l_forcedAdapter = -1;
    for(int i = 0; i < argc; i++)
    {
        const std::string l_arg(argv[i]);
        if(!l_arg.compare("--both")) l_bothEyes = true;
        else if(!l_arg.compare("--wait-new-frame")) l_waitNewFrame = true;
        else if(!l_arg.compare("--adapter") && ((i + 1) < argc)) l_forcedAdapter = atoi(argv[++i]);
        else if(l_outputPath.empty()) l_outputPath.assign(l_arg);
        else
        {
            fprintf(stderr, "err: unexpected argument '%s'\n", l_arg.c_str());
            return 2;
        }
    }
    if(l_outputPath.empty())
    {
        fputs("err: usage: asvr_ctl screenshot <out.png> [--both] [--wait-new-frame] [--adapter N]\n", stderr);
        return 2;
    }

    HANDLE l_mapping = OpenFileMappingA(FILE_MAP_READ, FALSE, g_sharedFrameMappingName);
    if(l_mapping == NULL)
    {
        fputs("err: frame mapping not found (is SteamVR running with the ASVR driver?)\n", stderr);
        return 2;
    }
    const SharedFrameInfo *l_info = static_cast<const SharedFrameInfo*>(MapViewOfFile(l_mapping, FILE_MAP_READ, 0U, 0U, sizeof(SharedFrameInfo)));
    if(l_info == NULL)
    {
        CloseHandle(l_mapping);
        fputs("err: failed to map the frame info\n", stderr);
        return 2;
    }
    if((l_info->magic != g_sharedFrameMagic) || (l_info->sharedHandle == 0U))
    {
        UnmapViewOfFile(const_cast<SharedFrameInfo*>(l_info));
        CloseHandle(l_mapping);
        fputs("err: no frame published yet (compositor not presenting?)\n", stderr);
        return 2;
    }

    if(l_waitNewFrame)
    {
        const uint64_t l_startCounter = l_info->frameCounter;
        const ULONGLONG l_deadline = GetTickCount64() + 2000ULL;
        while((l_info->frameCounter == l_startCounter) && (GetTickCount64() < l_deadline)) Sleep(5U);
        if(l_info->frameCounter == l_startCounter) fputs("warn: no new frame within 2s, capturing the last one\n", stderr);
    }

    int l_result = 2;
    const HRESULT l_comInit = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    ID3D11Device *l_device = nullptr;
    ID3D11DeviceContext *l_context = nullptr;
    ID3D11Texture2D *l_texture = nullptr;
    if(OpenSharedTextureOnAnyAdapter(l_info->sharedHandle, l_forcedAdapter, l_device, l_context, l_texture))
    {
        D3D11_TEXTURE2D_DESC l_desc;
        l_texture->GetDesc(&l_desc);
        D3D11_TEXTURE2D_DESC l_stagingDesc = l_desc;
        l_stagingDesc.Usage = D3D11_USAGE_STAGING;
        l_stagingDesc.BindFlags = 0U;
        l_stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        l_stagingDesc.MiscFlags = 0U;

        ID3D11Texture2D *l_staging = nullptr;
        if(SUCCEEDED(l_device->CreateTexture2D(&l_stagingDesc, NULL, &l_staging)))
        {
            const uint32_t l_outputWidth = l_bothEyes ? l_desc.Width : (l_desc.Width / 2U);
            std::vector<uint8_t> l_pixels(static_cast<size_t>(l_outputWidth) * l_desc.Height * 4U);
            bool l_converted = false;
            for(int l_attempt = 0; l_attempt < 3; l_attempt++)
            {
                const uint64_t l_counterBefore = l_info->frameCounter;
                l_context->CopyResource(l_staging, l_texture);
                l_context->Flush();
                D3D11_MAPPED_SUBRESOURCE l_mapped;
                if(FAILED(l_context->Map(l_staging, 0U, D3D11_MAP_READ, 0U, &l_mapped))) break;
                const bool l_torn = (l_info->frameCounter != l_counterBefore);
                if(!l_torn || (l_attempt == 2))
                {
                    l_converted = true;
                    for(uint32_t l_row = 0U; l_row < l_desc.Height; l_row++)
                    {
                        const uint8_t *l_source = static_cast<const uint8_t*>(l_mapped.pData) + (static_cast<size_t>(l_row) * l_mapped.RowPitch);
                        if(!ConvertRow(l_desc.Format, l_source, l_pixels.data() + (static_cast<size_t>(l_row) * l_outputWidth * 4U), l_outputWidth))
                        {
                            fprintf(stderr, "err: unsupported texture format %d\n", l_desc.Format);
                            l_converted = false;
                            break;
                        }
                    }
                }
                l_context->Unmap(l_staging, 0U);
                if(l_converted || l_torn == false) break;
            }
            l_staging->Release();

            if(l_converted)
            {
                const std::wstring l_widePath = WidenPath(l_outputPath);
                if(!l_widePath.empty() && SavePng(l_widePath.c_str(), l_pixels.data(), l_outputWidth, l_desc.Height))
                {
                    printf("ok %s %ux%u\n", l_outputPath.c_str(), l_outputWidth, l_desc.Height);
                    l_result = 0;
                }
                else fputs("err: failed to encode the PNG\n", stderr);
            }
        }
        else fputs("err: failed to create the staging texture\n", stderr);

        l_texture->Release();
        l_context->Release();
        l_device->Release();
    }
    else fputs("err: could not open the shared frame texture on any adapter (try --adapter N or restart SteamVR)\n", stderr);

    if(SUCCEEDED(l_comInit)) CoUninitialize();
    UnmapViewOfFile(const_cast<SharedFrameInfo*>(l_info));
    CloseHandle(l_mapping);
    return l_result;
}

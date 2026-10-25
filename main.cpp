#define LITEFX_DEFINE_GLOBAL_EXPORTS
#define LITEFX_AUTO_IMPORT_BACKEND_HEADERS
#include <litefx/litefx.h>

#include <Windows.h>
#include <d3d12sdklayers.h>
#include <wrl/client.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

using namespace LiteFX;
using namespace LiteFX::Rendering;
using namespace LiteFX::Rendering::Backends;

using Microsoft::WRL::ComPtr;


constexpr UInt32 BlockCount = 3;
constexpr size_t ResultBytes = 32;

struct InstanceData {
    float world[16];
    float baseColor[4];
};
static_assert(sizeof(InstanceData) == 80);
static_assert(offsetof(InstanceData, baseColor) == 64);

void Check(HRESULT result, const char* message)
{
    if (FAILED(result))
        throw std::runtime_error(message);
}

void EnableValidation()
{
    ComPtr<ID3D12Debug3> debug;
    Check(D3D12GetDebugInterface(IID_PPV_ARGS(debug.GetAddressOf())),
        "D3D12 debug interface unavailable. Install Windows Graphics Tools.");
    debug->EnableDebugLayer();
    debug->SetEnableGPUBasedValidation(TRUE);
    debug->SetEnableSynchronizedCommandQueueValidation(TRUE);
    debug->SetGPUBasedValidationFlags(D3D12_GPU_BASED_VALIDATION_FLAGS_NONE);
}

class Window {
public:
    Window()
    {
        WNDCLASSW definition{};
        definition.lpfnWndProc = DefWindowProcW;
        definition.hInstance = GetModuleHandleW(nullptr);
        definition.lpszClassName = L"LiteFXStructuredBufferRepro";
        if (!RegisterClassW(&definition))
            throw std::runtime_error("RegisterClassW failed.");

        m_handle = CreateWindowExW(0, definition.lpszClassName, L"Reproducer", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr, nullptr, definition.hInstance, nullptr);
        if (!m_handle) {
            UnregisterClassW(definition.lpszClassName, definition.hInstance);
            throw std::runtime_error("CreateWindowExW failed.");
        }
    }
    ~Window()
    {
        DestroyWindow(m_handle);
        UnregisterClassW(L"LiteFXStructuredBufferRepro", GetModuleHandleW(nullptr));
    }
    HWND handle() const { return m_handle; }
private:
    HWND m_handle{};
};

void Probe(DirectX12Device& device)
{
    UInt32 records = 8;
    //ComPtr<ID3D12InfoQueue> info;
    //Check(std::as_const(device).handle().As(&info), "D3D12 info queue unavailable.");
    // LiteFX enables these breaks in Debug. Collect diagnostics instead so the
    // console test can finish and produce a report without an attached debugger.
   // info->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, FALSE);
    //info->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, FALSE);

    SharedPtr<DirectX12ShaderProgram> shader = device.buildShaderProgram()
        .withComputeShaderModule("shaders/probe_cs.dxi", "CSMain");
    UniquePtr<DirectX12ComputePipeline> pipeline = device.buildComputePipeline("Probe")
        .shaderProgram(shader)
        .layout(shader->reflectPipelineLayout());

    const auto blockBytes = sizeof(InstanceData) * records;
    auto& inputLayout = pipeline->layout()->descriptorSet(0);
    auto& outputLayout = pipeline->layout()->descriptorSet(1);
    std::cout << "Reflected record size: " << inputLayout.descriptor(0).elementSize()
              << " bytes; expected " << sizeof(InstanceData) << "\n";

    // One LiteFX buffer element is one block of many shader records.
    auto input = device.factory().createBuffer("Packed instances", inputLayout, 0,
        ResourceHeap::Dynamic, blockBytes, BlockCount);
    auto output = device.factory().createBuffer("Probe results", outputLayout, 0,
        ResourceHeap::Resource, ResultBytes, 1,
        ResourceUsage::AllowWrite | ResourceUsage::TransferSource);
    auto readback = device.factory().createBuffer("Readback", BufferType::Storage,
        ResourceHeap::Readback, ResultBytes, 1, ResourceUsage::TransferDestination);
    auto outputBindings = outputLayout.allocate({
        { .resource = *output, .elements = 1 }
    });

    std::vector<InstanceData> data(records);
    auto& queue = device.defaultQueue(QueueType::Graphics);
    UInt32 errors = 0;
    bool allValuesMatch = true;
    for (UInt32 block = 0; block < BlockCount; ++block) {
        const float seed = static_cast<float>(10000 * (block + 1));
        for (UInt32 index = 0; index < records; ++index) {
            data[index] = {};
            data[index].world[0] = seed + index;
            data[index].baseColor[0] = seed + index + 0.25f;
        }
        input->map(static_cast<const void*>(data.data()), blockBytes, block);
        auto inputBindings = inputLayout.allocate({
            { .resource = *input, .firstElement = block, .elements = 1 }
        });

        auto commands = queue.createCommandBuffer(true);
        auto before = device.makeBarrier(
            block == 0 ? PipelineStage::None : PipelineStage::Transfer,
            PipelineStage::Compute);
        before->transition(*output,
            block == 0 ? ResourceAccess::None : ResourceAccess::TransferRead,
            ResourceAccess::ShaderReadWrite);
        commands->barrier(*before);
        commands->use(*pipeline);
        commands->bind({ inputBindings.get(), outputBindings.get() });
        commands->dispatch({ 1, 1, 1 });

        auto after = device.makeBarrier(PipelineStage::Compute, PipelineStage::Transfer);
        after->transition(*output, ResourceAccess::ShaderReadWrite, ResourceAccess::TransferRead);
        commands->barrier(*after);
        commands->transfer(*output, *readback);
        const auto fence = commands->submit();
        queue.waitFor(fence);

        std::array<std::uint32_t, 8> result{};
        readback->map(result.data(), ResultBytes, 0, false);
        const auto value = [&](size_t index) { return std::bit_cast<float>(result[index]); };
        const bool matches = result[0] == records && result[1] == sizeof(InstanceData) &&
            value(2) == seed && value(3) == seed + 1 && value(4) == seed + 7 &&
            value(5) == seed + 0.25f && value(6) == seed + 7.25f && result[7] == 0x12345678;
        allValuesMatch &= matches;

    }
    device.wait();
}

class MyApp final : public App {
public:
    static StringView Name() noexcept { return "StructuredBuffer reproducer"; }
    StringView name() const noexcept override { return Name(); }
    static AppVersion Version() noexcept { return AppVersion(1, 0, 0, 0); }
    AppVersion version() const noexcept override { return Version(); }

    MyApp()
    {
        initializing += [this](const void*, EventArgs) {
            onBackendStart<DirectX12Backend>([this](DirectX12Backend* backend) {
                const auto adapter = backend->findAdapter(GpuPreference::Performance);
                if (!adapter)
                    throw std::runtime_error("No suitable DirectX 12 adapter found.");
                std::cout << "Adapter: " << adapter->name() << '\n';
                m_device = &backend->createDevice("Probe", *adapter,
                    backend->createSurface(m_window.handle()),
                    Format::B8G8R8A8_UNORM, Size2d(64, 64), 3, false);
                return true;
            });
            onBackendStop<DirectX12Backend>([this](DirectX12Backend* backend) {
                m_device->wait();
                backend->releaseDevice("Probe");
                m_device = nullptr;
            });
        };
        startup += [this](const void*, EventArgs) 
            {
            Probe(*m_device);
        };
    }
    
private:
    Window m_window;
    DirectX12Device* m_device{};
};

int main()
{
    EnableValidation();

    UniquePtr<App> app = App::build<MyApp>()
        .logTo<ConsoleSink>(LogLevel::Warning)
        .useBackend<DirectX12Backend>();

    app->run();

    return 0;
}

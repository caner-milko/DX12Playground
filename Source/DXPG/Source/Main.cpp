#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_dx12.h"
//#include <d3d12.h>
#include <dxgi1_4.h>
#include <tchar.h>
#include <iostream>
#include <span>
#include <SDL2/SDL.h>
#include <SDL_syswm.h>
#include <io.h>
#include <fcntl.h>
#include <chrono>
#include <filesystem>
#include <variant>

#include "DXResource.h"
#include <Shlwapi.h>

#include <tiny_obj_loader.h>

#include "RendererCommon.h"
#include "Pipelines/DeferredRenderingPipeline.h"
#include "Pipelines/BlitPipeline.h"
#include "ShaderManager.h"
#include "TextureManager.h"
#include "ModelManager.h"

#include "SceneTree.h"

extern "C" { __declspec(dllexport) extern const unsigned int D3D12SDKVersion = DIRECT3D_AGILITY_SDK_VERSION; }
extern "C" { __declspec(dllexport) extern const char* D3D12SDKPath = ".\\D3D12\\"; }

namespace dxpg
{

// Data
static int const                    NUM_FRAMES_IN_FLIGHT = 3;
static FrameContext                 g_frameContext[NUM_FRAMES_IN_FLIGHT] = {};
static UINT                         g_frameIndex = 0;

static int const                    NUM_BACK_BUFFERS = 3;
static ComPtr<ID3D12Device2> g_pd3dDevice = nullptr;

static ComPtr<ID3D12CommandQueue> g_pd3dCommandQueue = nullptr;
static ComPtr<ID3D12GraphicsCommandList2> g_pd3dCommandList = nullptr;
static ComPtr<ID3D12Fence> g_fence = nullptr;
static HANDLE                       g_fenceEvent = nullptr;
static UINT64                       g_fenceLastSignaledValue = 0;
static ComPtr<IDXGISwapChain3> g_pSwapChain = nullptr;
static HANDLE                       g_hSwapChainWaitableObject = nullptr;
static DXTexture g_mainRenderTargetResource[NUM_BACK_BUFFERS];
static RenderTargetView g_mainRTVs = {};
static RenderTargetView g_mainRTVSRGBs = {};

static FrameContext FrameIndependentCtx = {};
static HWND g_hWnd = nullptr;

DeferredRenderingPipeline g_DeferredRenderingPipeline;
BlitPipeline g_BlitPipeline;
SceneTree g_SceneTree;

static int g_Width = 1920;
static int g_Height = 1080;

struct IO
{
    // Keep last & current sdl key states
    enum KEY_STATE
    {
        KEY_UP = 0,
	    KEY_DOWN = 1,
        KEY_PRESSED = 2,
        KEY_RELEASED = 3
    };
    KEY_STATE CUR_KEYS[SDL_NUM_SCANCODES];
    
    bool CursorEnabled = false;

    struct Immediate
    {
        float MouseWheelDelta = 0.0f;
        Vector2 MouseDelta = { 0, 0 };
    } Immediate;

    bool IsKeyDown(SDL_Scancode key)
    {
	    return CUR_KEYS[key] == KEY_DOWN || CUR_KEYS[key] == KEY_PRESSED;
    }

    bool IsKeyPressed(SDL_Scancode key)
    {
	    return CUR_KEYS[key] == KEY_PRESSED;
    }

    bool IsKeyReleased(SDL_Scancode key)
    {
	    return CUR_KEYS[key] == KEY_RELEASED;
    }

    bool IsKeyUp(SDL_Scancode key)
    {
	    return CUR_KEYS[key] == KEY_UP || CUR_KEYS[key] == KEY_RELEASED;
    }

} static g_IO;

struct ViewPoint
{
	float MoveSpeed = 10.0f;
	float RotSpeed = 2.0f;
    Vector4 Position = { };
    Vector4 Rotation = { };

    ViewPoint()
    {
    }

	Matrix4x4 GetRotationMatrix()
	{
		return DirectX::XMMatrixRotationRollPitchYawFromVector(Rotation);
	}
    Vector4 GetDirection()
    {
        return DirectX::XMVector4Transform(DirectX::XMVectorSet(0, 0, 1, 0), GetRotationMatrix());
    }
    Vector4 GetRight()
    {
        return DirectX::XMVector4Transform(DirectX::XMVectorSet(1, 0, 0, 0), GetRotationMatrix());
    }
    Vector4 GetUp()
    {
        return DirectX::XMVector4Transform(DirectX::XMVectorSet(0, 1, 0, 0), GetRotationMatrix());
    }

    Matrix4x4 GetViewMatrix()
    {
        const Vector4 upDirection = { 0, 1, 0, 0 };
        return DirectX::XMMatrixLookToLH(Position, GetDirection(), upDirection);
    }

    virtual Matrix4x4 GetProjectionMatrix() = 0;
    virtual void Reset() = 0;

    ViewData ToViewData()
    {
        auto view = GetViewMatrix();
        auto proj = GetProjectionMatrix();
        return { view, proj, XMMatrixMultiply(view, proj), Position, GetDirection() };
    }

};

struct Camera : ViewPoint
{
    float FoV = 45.0f;
    
    Camera()
    {
        Reset();
    }
    
	Matrix4x4 GetRotationMatrix()
	{
		return DirectX::XMMatrixRotationRollPitchYawFromVector(Rotation);
	}

    void SetFoV(float fov)
    {
		FoV = std::clamp(fov, 30.0f, 90.0f);
	}

	Matrix4x4 GetProjectionMatrix() override
	{
		float aspectRatio = static_cast<float>(g_Width) / static_cast<float>(g_Height);
		return DirectX::XMMatrixPerspectiveFovLH(DirectX::XMConvertToRadians(FoV), aspectRatio, 0.1f, 100.0f);
	}

	void Reset() override
	{
		Position = { 6, 2.50f, -1.50f, 1 };
		Rotation = { 0, -3.f * XM_PIDIV4 / 2.f, 0, 0 };
		FoV = 45.0f;
	}
} g_Cam = {};

struct DirectionalLight : ViewPoint
{
	// In degrees
    Vector3 Color = { 1.0f, 1.0f, 1.0f };
    float Intensity = 1.0f;
	Vector3 AmbientColor = { 0.1f, 0.1f, 0.1f };
	float InverseZoom = 1.0f;

    DirectionalLight()
    {
        Reset();
    }

    Matrix4x4 GetProjectionMatrix() override
    {
        return DirectX::XMMatrixOrthographicLH(InverseZoom, InverseZoom, 0.1f, 100.0f);
    }

	void Reset() override
	{
        Position = { 0, 23.2, -1.8 };
		Rotation = { 1.127, -0.964f, 0.218f };
        InverseZoom = 46.0f;
    }

	dxpg::hlsl::LightDataBuffer ToLightData()
	{
		return dxpg::hlsl::LightDataBuffer
		{
			.DirectionOrPosition = Vector3(GetDirection().m128_f32),
			.Color = Color,
			.Intensity = Intensity,
			.AmbientColor = AmbientColor
		};
	}

	void SetInverseZoom(float zoom)
	{
		InverseZoom = max(0.1f, zoom);
	}


} g_DirectionalLight = {};
ViewPoint* g_Controlled = &g_Cam;


// Forward declarations of helper functions
bool CreateDeviceD3D();
void CleanupDeviceD3D();
void CreateSwapchainRTVDSV(bool resized);
void CleanupRenderTarget();
void WaitForLastSubmittedFrame();
FrameContext* WaitForNextFrameResources();

void LoadSceneData();
void UploadToBuffer(ID3D12GraphicsCommandList* cmd, ID3D12Resource* dest, ID3D12Resource** intermediateBuf, size_t size, void* data);
void UploadToTexture(ID3D12GraphicsCommandList* cmd, ID3D12Resource* dest, ID3D12Resource** intermediateBuf, size_t width, size_t height, size_t componentCount, void* data);

void CreateConsole()
{
    if (!AllocConsole()) {
        // Add some error handling here.
        // You can call GetLastError() to get more info about the error.
        return;
    }

    // std::cout, std::clog, std::cerr, std::cin
    FILE* fDummy;
    freopen_s(&fDummy, "CONOUT$", "w", stdout);
    freopen_s(&fDummy, "CONOUT$", "w", stderr);
    freopen_s(&fDummy, "CONIN$", "r", stdin);
    std::cout.clear();
    std::clog.clear();
    std::cerr.clear();
    std::cin.clear();

    // std::wcout, std::wclog, std::wcerr, std::wcin
    HANDLE hConOut = CreateFile(_T("CONOUT$"), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    HANDLE hConIn = CreateFile(_T("CONIN$"), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    SetStdHandle(STD_OUTPUT_HANDLE, hConOut);
    SetStdHandle(STD_ERROR_HANDLE, hConOut);
    SetStdHandle(STD_INPUT_HANDLE, hConIn);
    std::wcout.clear();
    std::wclog.clear();
    std::wcerr.clear();
    std::wcin.clear();
}

void BeginFrame(FrameContext& frameCtx)
{
    frameCtx.GPUHeapPages[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV] = g_GPUDescriptorAllocator->Heaps[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV]->AllocatePage();
    frameCtx.GPUHeapPages[D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER] = g_GPUDescriptorAllocator->Heaps[D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER]->AllocatePage();
    g_pd3dCommandList->Reset(frameCtx.CommandAllocator.Get(), nullptr);
    frameCtx.Ready = true;
    auto heaps = g_GPUDescriptorAllocator->GetHeaps();
    g_pd3dCommandList->SetDescriptorHeaps(heaps.size(), heaps.data());
}

void EndFrame(FrameContext& frameCtx)
{
    frameCtx.Ready = false;
}

void ClearFrame(FrameContext& frameCtx)
{
    for (auto& [type, heapPage] : frameCtx.GPUHeapPages)
    {
        g_GPUDescriptorAllocator->Heaps[type]->FreePage(heapPage);
    }
	frameCtx.GPUHeapPages.clear();
    frameCtx.CommandAllocator->Reset();
	frameCtx.IntermediateResources.clear();
}

void UIDrawMeshTree(MeshObject* object)
{
    ImGui::PushID(object->Name.c_str());

	if (ImGui::TreeNodeEx(object->Name.c_str(), ImGuiTreeNodeFlags_Framed))
    {
		ImGui::InputFloat3("Position", &object->Position.m128_f32[0], "%.3f");
		ImGui::InputFloat3("Rotation", &object->Rotation.m128_f32[0], "%.3f");
		ImGui::InputFloat3("Scale", &object->Scale.m128_f32[0], "%.3f");

        ImGui::Checkbox("TransformOnly", &object->TransformOnly);
	    for (auto& child : object->Children)
        {
		    UIDrawMeshTree(&child);
	    }
		ImGui::TreePop();
	}   

    ImGui::PopID();
}

void UIUpdate(ImGuiIO& io, bool& showDemoWindow, bool& showAnotherWindow, ImVec4& clearCol)
{

    ImGui::NewFrame();

    {
        ImGui::Begin("Scene");
        //Camera
        if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen))
        {
			ImGui::PushID("Camera");
            ImGui::InputFloat3("Position", &g_Cam.Position.m128_f32[0], "%.3f", ImGuiInputTextFlags_ReadOnly);
            ImGui::InputFloat3("Rotation", &g_Cam.Rotation.m128_f32[0], "%.3f", ImGuiInputTextFlags_ReadOnly);
			auto dir = g_Cam.GetDirection();
			ImGui::InputFloat3("Direction", dir.m128_f32, "%.3f", ImGuiInputTextFlags_ReadOnly);
            ImGui::InputFloat("FoV", &g_Cam.FoV, 0.f, 0.f, "%.3f", ImGuiInputTextFlags_ReadOnly);

            ImGui::SliderFloat("Move Speed", &g_Cam.MoveSpeed, 0.0f, 10000.0f);
            ImGui::SliderFloat("Rotation Speed", &g_Cam.RotSpeed, 0.1f, 10.0f);
			if (ImGui::Button("Reset"))
				g_Cam.Reset();
			ImGui::PopID();
        }

		//Light
		if (ImGui::CollapsingHeader("Light", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::PushID("Light");
			ImGui::InputFloat3("Position", &g_DirectionalLight.Position.m128_f32[0], "%.3f");
			ImGui::InputFloat3("Rotation", &g_DirectionalLight.Rotation.m128_f32[0], "%.3f");
            auto dir = g_DirectionalLight.GetDirection();
			ImGui::InputFloat3("Direction", dir.m128_f32, "%.3f", ImGuiSliderFlags_NoInput);
			ImGui::InputFloat("Inverse Zoom", &g_DirectionalLight.InverseZoom, 1.0f, 100.0f, "%.3f");
            ImGui::ColorEdit3("Color", &g_DirectionalLight.Color.x);
			ImGui::SliderFloat("Intensity", &g_DirectionalLight.Intensity, 0.0f, 10.0f);
			ImGui::ColorEdit3("Ambient Color", &g_DirectionalLight.AmbientColor.x);



            float yawDegrees = XMConvertToDegrees(g_DirectionalLight.Rotation.m128_f32[0]);
            ImGui::SliderFloat("Yaw", &yawDegrees, 0.0f, 360.0f, "%.3f", ImGuiSliderFlags_NoInput);
			g_DirectionalLight.Rotation.m128_f32[0] = XMConvertToRadians(yawDegrees);
            float pitchDegress = XMConvertToDegrees(g_DirectionalLight.Rotation.m128_f32[1]);
			ImGui::SliderFloat("Pitch", &pitchDegress, -90.0f, 90.0f, "%.3f", ImGuiSliderFlags_NoInput);
			g_DirectionalLight.Rotation.m128_f32[1] = XMConvertToRadians(pitchDegress);
			if (ImGui::Button("Reset"))
				g_DirectionalLight.Reset();
            ImGui::PopID();
        }

		UIDrawMeshTree(&g_SceneTree.Root);

        ImGui::End();
    }

    // Rendering
    ImGui::Render();
}

void InitGame()
{
    memset(g_IO.CUR_KEYS, 0, sizeof(g_IO.CUR_KEYS));
}

void UpdateGame(float deltaTime)
{
    if (g_IO.IsKeyPressed(SDL_SCANCODE_ESCAPE))
	{
		SDL_Event quitEvent;
		quitEvent.type = SDL_QUIT;
		SDL_PushEvent(&quitEvent);
	}
    if (g_IO.IsKeyPressed(SDL_SCANCODE_E))
    {
		//Disable imgui navigation
		ImGui::GetIO().ConfigFlags ^= ImGuiConfigFlags_NavEnableKeyboard;
        SDL_SetRelativeMouseMode((SDL_bool)g_IO.CursorEnabled);
        g_IO.CursorEnabled = !g_IO.CursorEnabled;
    }
    if (g_IO.CursorEnabled)
        return;
    // Switch controlled object on Tab
	if (g_IO.IsKeyPressed(SDL_SCANCODE_TAB))
	{
        if (g_Controlled == &g_Cam)
            g_Controlled = &g_DirectionalLight;
        else
            g_Controlled = &g_Cam;
	}

	if (g_IO.IsKeyPressed(SDL_SCANCODE_L))
    {
		g_DirectionalLight.Position = g_Cam.Position;
		g_DirectionalLight.Rotation = g_Cam.Rotation;
    }
    
    auto& controlled = *g_Controlled;
    if (g_IO.IsKeyPressed(SDL_SCANCODE_R))
        controlled.Reset();
    if (g_Controlled == &g_Cam)
    {
		auto& cam = static_cast<Camera&>(*g_Controlled);
		cam.SetFoV(cam.FoV - g_IO.Immediate.MouseWheelDelta * 2.0f);
    }
    else
    {
		g_DirectionalLight.SetInverseZoom(g_DirectionalLight.InverseZoom - g_IO.Immediate.MouseWheelDelta * 2.0f);
    }
    Vector4 moveDir = { float(g_IO.IsKeyDown(SDL_SCANCODE_D)) - float(g_IO.IsKeyDown(SDL_SCANCODE_A)), 0
        , float(g_IO.IsKeyDown(SDL_SCANCODE_W)) - float(g_IO.IsKeyDown(SDL_SCANCODE_S)), 0 };
        
	Vector4 moveVec = XMVector4Transform(XMVector4Normalize(moveDir), controlled.GetRotationMatrix());

    moveVec.m128_f32[1] += float(g_IO.IsKeyDown(SDL_SCANCODE_SPACE)) - float(g_IO.IsKeyDown(SDL_SCANCODE_LCTRL));
    
    moveVec = XMVector4Normalize(moveVec);

    controlled.Position = controlled.Position + moveVec * deltaTime * controlled.MoveSpeed;
        
    if(!g_IO.CursorEnabled)
    {
        controlled.Rotation = controlled.Rotation + XMVectorSet(g_IO.Immediate.MouseDelta.y, g_IO.Immediate.MouseDelta.x, 0, 0) * controlled.RotSpeed * deltaTime;
        controlled.Rotation = XMVectorSetX(controlled.Rotation, std::clamp(XMVectorGetX(controlled.Rotation), -XM_PIDIV2 + 0.0001f, XM_PIDIV2 - 0.0001f));
    }
}

void Render(ImGuiIO& io, bool& showDemoWindow, bool& showAnotherWindow, ImVec4& clearCol)
{
    FrameContext* frameCtx = WaitForNextFrameResources();
    BeginFrame(*frameCtx);
    UINT backBufferIdx = g_pSwapChain->GetCurrentBackBufferIndex();
    
    SceneDataView sceneDataView{.RenderableList = g_SceneTree.SceneToRenderableList(), .Light = g_DirectionalLight.ToLightData(), .LightView = g_DirectionalLight.ToViewData()};
	g_DeferredRenderingPipeline.Run(g_pd3dCommandList.Get(), g_Cam.ToViewData(), sceneDataView, *frameCtx);

    DXTexture* selectedView = nullptr;
	DescriptorAllocationView selectedSRVView = {};
    {
        if (g_Controlled == &g_Cam)
        {
            selectedView = &g_DeferredRenderingPipeline.GetOutputBuffer();
            selectedSRVView = g_DeferredRenderingPipeline.GetOutputBufferSRV();
        }
        else
        {
			selectedView = &g_DeferredRenderingPipeline.GetShadowMap();
            selectedSRVView = g_DeferredRenderingPipeline.GetShadowMapSRV();
        }
    }

    g_BlitPipeline.Blit(g_pd3dCommandList.Get(), &g_mainRenderTargetResource[backBufferIdx],
        selectedView, g_mainRTVSRGBs.GetCPUHandle(backBufferIdx), selectedSRVView.GetIndex());
	
    TransitionVec(g_mainRenderTargetResource[backBufferIdx], D3D12_RESOURCE_STATE_RENDER_TARGET)
		.Execute(g_pd3dCommandList.Get());
    // ImGui
    {
        auto rtvHandle = g_mainRTVs.GetCPUHandle(backBufferIdx);

        g_pd3dCommandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

        ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), g_pd3dCommandList.Get());
    }
	TransitionVec(g_mainRenderTargetResource[backBufferIdx], D3D12_RESOURCE_STATE_PRESENT)
		.Execute(g_pd3dCommandList.Get());
    g_pd3dCommandList->Close();

    ID3D12CommandList* ppCommandLists[] = { g_pd3dCommandList.Get() };

    g_pd3dCommandQueue->ExecuteCommandLists(1, ppCommandLists);

    g_pSwapChain->Present(1, 0); // Present with vsync
    //g_pSwapChain->Present(0, 0); // Present without vsync

    UINT64 fenceValue = g_fenceLastSignaledValue + 1;
    g_pd3dCommandQueue->Signal(g_fence.Get(), fenceValue);
    g_fenceLastSignaledValue = fenceValue;
    frameCtx->FenceValue = fenceValue;
    EndFrame(*frameCtx);
}


// Helper functions

bool CreateDeviceD3D()
{
    // [DEBUG] Enable debug interface
#ifdef DX12_ENABLE_DEBUG_LAYER
    ComPtr<ID3D12Debug> pdx12Debug = nullptr;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&pdx12Debug))))
        pdx12Debug->EnableDebugLayer();
#endif

    // Create device
    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_12_0;
    if (D3D12CreateDevice(nullptr, featureLevel, IID_PPV_ARGS(&g_pd3dDevice)) != S_OK)
        return false;

    // [DEBUG] Setup debug interface to break on any warnings/errors
#ifdef DX12_ENABLE_DEBUG_LAYER
    if (pdx12Debug != nullptr)
    {
        ComPtr<ID3D12InfoQueue> pInfoQueue = nullptr;
        g_pd3dDevice->QueryInterface(IID_PPV_ARGS(&pInfoQueue));
        pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
        pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
        pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, true);
    }
#endif

    ShaderManager::Create();
	ShaderManager::Get().Init(g_pd3dDevice.Get());
    {
        g_CPUDescriptorAllocator = CPUDescriptorHeapAllocator::Create(g_pd3dDevice.Get());
		g_CPUDescriptorAllocator->CreateHeapType(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1024);
		g_CPUDescriptorAllocator->CreateHeapType(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 1024);
		g_CPUDescriptorAllocator->CreateHeapType(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1024);
		g_CPUDescriptorAllocator->CreateHeapType(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1024);

        g_GPUDescriptorAllocator = GPUDescriptorHeapAllocator::Create(g_pd3dDevice.Get());
		g_GPUDescriptorAllocator->CreateHeapType(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2048*12, NUM_BACK_BUFFERS, 2048*8);
		g_GPUDescriptorAllocator->CreateHeapType(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 1024, NUM_BACK_BUFFERS);

    }

    TextureManager::Create();
    TextureManager::Get().Init(g_pd3dDevice.Get());

	ModelManager::Create();
	ModelManager::Get().Init(g_pd3dDevice.Get());

    {
        D3D12_COMMAND_QUEUE_DESC desc = {};
        desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        desc.NodeMask = 1;
        if (g_pd3dDevice->CreateCommandQueue(&desc, IID_PPV_ARGS(&g_pd3dCommandQueue)) != S_OK)
            return false;
    }

    for (UINT i = 0; i < NUM_FRAMES_IN_FLIGHT; i++)
        if (g_pd3dDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g_frameContext[i].CommandAllocator)) != S_OK)
            return false;

    if (g_pd3dDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&FrameIndependentCtx.CommandAllocator)) != S_OK)
			return false;

    if (g_pd3dDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, FrameIndependentCtx.CommandAllocator.Get(), nullptr, IID_PPV_ARGS(&g_pd3dCommandList)) != S_OK ||
        g_pd3dCommandList->Close() != S_OK)
        return false;

    if (g_pd3dDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_fence)) != S_OK)
        return false;

    g_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (g_fenceEvent == nullptr)
        return false;

    g_DeferredRenderingPipeline.Setup(g_pd3dDevice.Get());
	g_BlitPipeline.Setup(g_pd3dDevice.Get());

    CreateSwapchainRTVDSV(false);
    return true;
}

void CleanupDeviceD3D()
{
	ModelManager::Destroy();
    TextureManager::Destroy();
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->SetFullscreenState(false, nullptr); g_pSwapChain = nullptr; }
    if (g_hSwapChainWaitableObject != nullptr) { CloseHandle(g_hSwapChainWaitableObject); }
    for (UINT i = 0; i < NUM_FRAMES_IN_FLIGHT; i++)
        g_frameContext[i].CommandAllocator = nullptr;
    FrameIndependentCtx.CommandAllocator = nullptr;
    g_DeferredRenderingPipeline = {};
	g_BlitPipeline = {};
    g_pd3dCommandQueue = nullptr;
    g_pd3dCommandList = nullptr;
    g_fence = nullptr;
    g_CPUDescriptorAllocator = nullptr;
    g_GPUDescriptorAllocator = nullptr;
    if (g_fenceEvent) { CloseHandle(g_fenceEvent); g_fenceEvent = nullptr; }
	ShaderManager::Destroy();
    g_pd3dDevice = nullptr;



#ifdef DX12_ENABLE_DEBUG_LAYER
    
    if (ComPtr<IDXGIDebug1> pDebug = nullptr; SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&pDebug))))
    {
        pDebug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_SUMMARY);
    }
#endif
}

void CreateSwapchainRTVDSV(bool resized)
{
    if(!resized){
        //Allocate RTV, SRGB RTV
		g_mainRTVs = static_cast<RenderTargetView>(g_CPUDescriptorAllocator->AllocateFromStatic(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, NUM_BACK_BUFFERS));
		g_mainRTVSRGBs = static_cast<RenderTargetView>(g_CPUDescriptorAllocator->AllocateFromStatic(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, NUM_BACK_BUFFERS));

        // Setup swap chain
        DXGI_SWAP_CHAIN_DESC1 sd;
        {
            ZeroMemory(&sd, sizeof(sd));
            sd.BufferCount = NUM_BACK_BUFFERS;
            sd.Width = g_Width;
            sd.Height = g_Height;
            sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            sd.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
            sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            sd.SampleDesc.Count = 1;
            sd.SampleDesc.Quality = 0;
            sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
            sd.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
            sd.Scaling = DXGI_SCALING_STRETCH;
            sd.Stereo = FALSE;
        }
        ComPtr<IDXGIFactory4> dxgiFactory = nullptr;
        ComPtr<IDXGISwapChain1> swapChain1 = nullptr;
        ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&dxgiFactory)));
        ThrowIfFailed(dxgiFactory->CreateSwapChainForHwnd(g_pd3dCommandQueue.Get(), g_hWnd, &sd, nullptr, nullptr, &swapChain1));
        ThrowIfFailed(swapChain1->QueryInterface(IID_PPV_ARGS(&g_pSwapChain)));
        g_pSwapChain->SetMaximumFrameLatency(NUM_BACK_BUFFERS);
        g_hSwapChainWaitableObject = g_pSwapChain->GetFrameLatencyWaitableObject();
    }
    else
    {
		for (UINT i = 0; i < NUM_BACK_BUFFERS; i++)
            g_mainRenderTargetResource[i] = {};

        DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
        ThrowIfFailed(g_pSwapChain->GetDesc(&swapChainDesc));
        ThrowIfFailed(g_pSwapChain->ResizeBuffers(NUM_BACK_BUFFERS, g_Width, g_Height,
            swapChainDesc.BufferDesc.Format, swapChainDesc.Flags));
    }

    //g_CPUDescriptorAllocator->Heaps[D3D12_DESCRIPTOR_HEAP_TYPE_RTV]->StaticPage->Reset();
    for (UINT i = 0; i < NUM_BACK_BUFFERS; i++)
    {
        ComPtr<ID3D12Resource> res;
        g_pSwapChain->GetBuffer(i, IID_PPV_ARGS(&res));
		DXTexture::TextureCreateInfo info = {};
		info.Width = g_Width;
		info.Height = g_Height;
		info.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		info.MipLevels = 1;

		auto& swapchainTex = g_mainRenderTargetResource[i] = DXTexture::FromExisting(g_pd3dDevice.Get(), L"Swapchain_" + std::to_wstring(i), res, info);

		D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
		rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
		rtvDesc.Texture2D.MipSlice = 0;
		swapchainTex.CreatePlacedRTV(g_mainRTVs.GetView(i), &rtvDesc);
		auto srgbDesc = rtvDesc;
		srgbDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        swapchainTex.CreatePlacedRTV(g_mainRTVSRGBs.GetView(i), &srgbDesc);
    }
    g_DeferredRenderingPipeline.OnResize(g_Width, g_Height);
}

void CleanupRenderTarget()
{
    WaitForLastSubmittedFrame();

    for (UINT i = 0; i < NUM_BACK_BUFFERS; i++)
        g_mainRenderTargetResource[i] = {};

    //g_CPUDescriptorAllocator->Heaps[D3D12_DESCRIPTOR_HEAP_TYPE_RTV]->StaticPage->Reset();
    //g_CPUDescriptorAllocator->Heaps[D3D12_DESCRIPTOR_HEAP_TYPE_DSV]->StaticPage->Reset();
}

void WaitForLastSubmittedFrame()
{
    FrameContext* frameCtx = &g_frameContext[g_frameIndex % NUM_FRAMES_IN_FLIGHT];

    UINT64 fenceValue = frameCtx->FenceValue;
    if (fenceValue == 0)
        return; // No fence was signaled

    frameCtx->FenceValue = 0;
    if (g_fence->GetCompletedValue() >= fenceValue)
        return;

    g_fence->SetEventOnCompletion(fenceValue, g_fenceEvent);
    WaitForSingleObject(g_fenceEvent, INFINITE);

    for (UINT i = 0; i < NUM_FRAMES_IN_FLIGHT; i++)
        ClearFrame(g_frameContext[i]);
}

FrameContext* WaitForNextFrameResources()
{
    UINT nextFrameIndex = g_frameIndex + 1;
    g_frameIndex = nextFrameIndex;

    HANDLE waitableObjects[] = { g_hSwapChainWaitableObject, nullptr };
    DWORD numWaitableObjects = 1;

    FrameContext* frameCtx = &g_frameContext[nextFrameIndex % NUM_FRAMES_IN_FLIGHT];
    UINT64 fenceValue = frameCtx->FenceValue;
    if (fenceValue != 0) // means no fence was signaled
    {
        frameCtx->FenceValue = 0;
        g_fence->SetEventOnCompletion(fenceValue, g_fenceEvent);
        waitableObjects[1] = g_fenceEvent;
        numWaitableObjects = 2;
    }

    WaitForMultipleObjects(numWaitableObjects, waitableObjects, TRUE, INFINITE);
    ClearFrame(*frameCtx);
    
    return frameCtx;
}

void LoadSceneData()
{

    ClearFrame(FrameIndependentCtx);
    BeginFrame(FrameIndependentCtx);


    {
        auto sponzaObj = ModelManager::Get().LoadModel(DXPG_SPONZA_DIR "sponza.obj", FrameIndependentCtx, g_pd3dCommandList.Get());
        auto* sponzaRoot = g_SceneTree.AddObject(MeshObject("SponzaRoot"));
        sponzaRoot->Scale /= 100.0f;
        for (auto& [indexed, materialInfo] : sponzaObj->Objects)
			auto* mesh = g_SceneTree.AddObject(MeshObject(indexed->Name, indexed, materialInfo), sponzaRoot);
    }

    //Execute and flush
    EndFrame(FrameIndependentCtx);
    g_pd3dCommandList->Close();
    ID3D12CommandList* ppCommandLists[] = { g_pd3dCommandList.Get() };
    g_pd3dCommandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

    ComPtr<ID3D12Fence> fence;
    g_pd3dDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    g_pd3dCommandQueue->Signal(fence.Get(), 1);
    HANDLE fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    fence->SetEventOnCompletion(1, fenceEvent);
    WaitForSingleObject(fenceEvent, INFINITE);
	CloseHandle(fenceEvent);
    ClearFrame(FrameIndependentCtx);
}

void UploadToBuffer(ID3D12GraphicsCommandList* cmd, ID3D12Resource* dest, ID3D12Resource** intermediateBuf, size_t size, void* data)
{
    // Create the intermediate upload heap
    auto intermediateHeapProp = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    auto intermediateResDesc = CD3DX12_RESOURCE_DESC::Buffer(size);
    g_pd3dDevice->CreateCommittedResource(
		&intermediateHeapProp,
		D3D12_HEAP_FLAG_NONE,
        &intermediateResDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(intermediateBuf));

    D3D12_SUBRESOURCE_DATA subresourceData = {};
    subresourceData.pData = data;
    subresourceData.RowPitch = size;
    subresourceData.SlicePitch = subresourceData.RowPitch;

    UpdateSubresources(cmd, dest, *intermediateBuf, 0, 0, 1, &subresourceData);
}


void UploadToTexture(ID3D12GraphicsCommandList* cmd, ID3D12Resource* dest, ID3D12Resource** intermediateBuf, size_t width, size_t height, size_t componentCount, void* data)
{
    // Create the intermediate upload heap
    auto intermediateHeapProp = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    auto intermediateResDesc = CD3DX12_RESOURCE_DESC::Buffer(width*height*componentCount);
    g_pd3dDevice->CreateCommittedResource(
        &intermediateHeapProp,
        D3D12_HEAP_FLAG_NONE,
        &intermediateResDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(intermediateBuf));

    D3D12_SUBRESOURCE_DATA subresourceData = {};
    subresourceData.pData = data;
    subresourceData.RowPitch = width * componentCount;
    subresourceData.SlicePitch = height * subresourceData.RowPitch;

    UpdateSubresources(cmd, dest, *intermediateBuf, 0, 0, 1, &subresourceData);
}
}

// Main code
int main(int argv, char** args)
{
    using namespace::dxpg;
    // Set working directory to executable directory
    {
        WCHAR path[MAX_PATH];
        HMODULE hModule = GetModuleHandleW(NULL);
        if (GetModuleFileNameW(hModule, path, MAX_PATH) > 0)
        {
            PathRemoveFileSpecW(path);
            SetCurrentDirectoryW(path);
        }
    }

    CreateConsole();
    // Setup SDL
    // (Some versions of SDL before <2.0.10 appears to have performance/stalling issues on a minority of Windows systems,
    // depending on whether SDL_INIT_GAMECONTROLLER is enabled or disabled.. updating to the latest version of SDL is recommended!)

    // Setup window
    SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_Window* window = SDL_CreateWindow("DX12 Playground", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, g_Width, g_Height, window_flags);
    if (window == nullptr)
    {
        printf("Error: SDL_CreateWindow(): %s\n", SDL_GetError());
        return -1;
    }
    SDL_SysWMinfo wmInfo;
    SDL_VERSION(&wmInfo.version);
    SDL_GetWindowWMInfo(window, &wmInfo);
    HWND hwnd = (HWND)wmInfo.info.win.window;
    g_hWnd = hwnd;
    // Initialize Direct3D
    if (!CreateDeviceD3D())
    {
        CleanupDeviceD3D();
        return 1;
    }

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;         // Enable Docking
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

    // Our state
    bool show_demo_window = true;
    bool show_another_window = false;
    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    //ImGui::StyleColorsLight();

    // Setup Platform/Renderer backends
    ImGui_ImplSDL2_InitForD3D(window);
    auto fontAllocation = g_GPUDescriptorAllocator->AllocateFromStatic(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
    ImGui_ImplDX12_Init(g_pd3dDevice.Get(), NUM_FRAMES_IN_FLIGHT,
        DXGI_FORMAT_R8G8B8A8_UNORM, fontAllocation.Heap->Heap.Get(),
        fontAllocation.GetCPUHandle(),
        fontAllocation.GetGPUHandle());
    // Load Fonts
    // - If no fonts are loaded, dear imgui will use the default font. You can also load multiple fonts and use ImGui::PushFont()/PopFont() to select them.
    // - AddFontFromFileTTF() will return the ImFont* so you can store it if you need to select the font among multiple.
    // - If the file cannot be loaded, the function will return a nullptr. Please handle those errors in your application (e.g. use an assertion, or display an error and quit).
    // - The fonts will be rasterized at a given size (w/ oversampling) and stored into a texture when calling ImFontAtlas::Build()/GetTexDataAsXXXX(), which ImGui_ImplXXXX_NewFrame below will call.
    // - Use '#define IMGUI_ENABLE_FREETYPE' in your imconfig file to use Freetype for higher quality font rendering.
    // - Read 'docs/FONTS.md' for more instructions and details.
    // - Remember that in C/C++ if you want to include a backslash \ in a string literal you need to write a double backslash \\ !
    //io.Fonts->AddFontDefault();
    //io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\segoeui.ttf", 18.0f);
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf", 16.0f);
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Roboto-Medium.ttf", 16.0f);
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Cousine-Regular.ttf", 15.0f);
    //ImFont* font = io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\ArialUni.ttf", 18.0f, nullptr, io.Fonts->GetGlyphRangesJapanese());
    //IM_ASSERT(font != nullptr);

    InitGame();
    LoadSceneData();
    SDL_SetRelativeMouseMode(SDL_TRUE);
    // Main loop
    bool done = false;

    std::chrono::high_resolution_clock::time_point lastTime = std::chrono::high_resolution_clock::now();

    while (!done)
    {
        // Poll and handle events (inputs, window resize, etc.)
        // You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
        // - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
        // - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
        // Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
        SDL_Event sdlEvent;
        while (SDL_PollEvent(&sdlEvent))
        {
            ImGui_ImplSDL2_ProcessEvent(&sdlEvent);
            if (sdlEvent.type == SDL_QUIT)
                done = true;
            if (sdlEvent.type == SDL_WINDOWEVENT && sdlEvent.window.event == SDL_WINDOWEVENT_CLOSE && sdlEvent.window.windowID == SDL_GetWindowID(window))
                done = true;
            if (sdlEvent.type == SDL_WINDOWEVENT && sdlEvent.window.event == SDL_WINDOWEVENT_RESIZED && sdlEvent.window.windowID == SDL_GetWindowID(window))
            {
                g_Width = sdlEvent.window.data1;
                g_Height = sdlEvent.window.data2;
                // Release all outstanding references to the swap chain's buffers before resizing.
                CleanupRenderTarget();
                CreateSwapchainRTVDSV(true);
            }
            if (sdlEvent.type == SDL_KEYDOWN)
                g_IO.CUR_KEYS[sdlEvent.key.keysym.scancode] = sdlEvent.key.repeat ? IO::KEY_DOWN : IO::KEY_PRESSED;
            if (sdlEvent.type == SDL_KEYUP)
                g_IO.CUR_KEYS[sdlEvent.key.keysym.scancode] = IO::KEY_RELEASED;
            if (sdlEvent.type == SDL_MOUSEMOTION)
                g_IO.Immediate.MouseDelta = { float(sdlEvent.motion.xrel), float(sdlEvent.motion.yrel) };
            if (sdlEvent.type == SDL_MOUSEWHEEL)
                g_IO.Immediate.MouseWheelDelta = sdlEvent.wheel.y;
        }

        // Start the Dear ImGui frame
        ImGui_ImplDX12_NewFrame();
        ImGui_ImplSDL2_NewFrame();

        UIUpdate(io, show_demo_window, show_another_window, clear_color);

        UpdateGame(io.DeltaTime);

        Render(io, show_demo_window, show_another_window, clear_color);

        for (int i = 0; i < 322; i++)
        {
            if (g_IO.CUR_KEYS[i] == IO::KEY_PRESSED)
                g_IO.CUR_KEYS[i] = IO::KEY_DOWN;
            if (g_IO.CUR_KEYS[i] == IO::KEY_RELEASED)
                g_IO.CUR_KEYS[i] = IO::KEY_UP;
        }

        g_IO.Immediate = {};
    }


    WaitForLastSubmittedFrame();

    // Cleanup
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
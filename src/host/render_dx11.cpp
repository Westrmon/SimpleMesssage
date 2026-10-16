// SimpleMessage — DX11 渲染后端：设备、交换链与 imgui 的 DX11 那一半
#include "host/render_backend.h"

#include <memory>
#include <new>

#include <d3d11.h>
#include <dxgi.h>

#include "GLFW/glfw3.h"
#define GLFW_EXPOSE_NATIVE_WIN32
#include "GLFW/glfw3native.h"

#include "imgui.h"
#include "imgui_impl_dx11.h"

#include "host/log.h"
#include "platform/window.h"

namespace sm {
namespace host {
namespace {

class Dx11Device {
 public:
  static std::shared_ptr<Dx11Device> Acquire() {
    static std::weak_ptr<Dx11Device> cached;
    if (const std::shared_ptr<Dx11Device> alive = cached.lock()) return alive;

    std::shared_ptr<Dx11Device> created(new (std::nothrow) Dx11Device());
    if (created == nullptr || !created->Create()) return nullptr;
    cached = created;
    return created;
  }

  ID3D11Device*        device() const { return device_; }
  ID3D11DeviceContext* context() const { return context_; }

  ~Dx11Device() {
    if (context_ != nullptr) context_->Release();
    if (device_ != nullptr) device_->Release();
  }

 private:
  Dx11Device() = default;
  Dx11Device(const Dx11Device&) = delete;
  Dx11Device& operator=(const Dx11Device&) = delete;

  bool Create() {
    const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
                                        D3D_FEATURE_LEVEL_10_0};
    HRESULT hr = ::D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels,
        static_cast<UINT>(sizeof(levels) / sizeof(levels[0])), D3D11_SDK_VERSION,
        &device_, nullptr, &context_);
    if (FAILED(hr)) {
      sm::log::Writef(sm::LogLevel::Warn, "ui",
                      "硬件 D3D11 设备创建失败（hr=0x%08lX），改用软件光栅化",
                      static_cast<unsigned long>(hr));
      hr = ::D3D11CreateDevice(
          nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, levels,
          static_cast<UINT>(sizeof(levels) / sizeof(levels[0])), D3D11_SDK_VERSION,
          &device_, nullptr, &context_);
    }
    if (FAILED(hr)) {
      sm::log::Writef(sm::LogLevel::Error, "ui", "创建 D3D11 设备失败（hr=0x%08lX）",
                      static_cast<unsigned long>(hr));
      return false;
    }
    return true;
  }

  ID3D11Device*        device_ = nullptr;
  ID3D11DeviceContext* context_ = nullptr;
};

class Dx11Backend final : public IRenderBackend {
 public:
  bool Init(platform::window::IWindow* window) override {
    if (device_ != nullptr) return true;
    if (window == nullptr || window->native_handle() == nullptr) {
      sm::log::Write(sm::LogLevel::Error, "ui", "没有窗口，无法建立渲染设备");
      return false;
    }
    window_ = window;
    const HWND hwnd = ::glfwGetWin32Window(
        static_cast<GLFWwindow*>(window->native_handle()));
    if (hwnd == nullptr) {
      sm::log::Write(sm::LogLevel::Error, "ui", "窗口没有 Win32 句柄，无法建立渲染设备");
      return false;
    }

    device_ = Dx11Device::Acquire();
    if (device_ == nullptr) {
      Shutdown();
      return false;
    }
    if (!CreateSwapChain(hwnd)) {
      Shutdown();
      return false;
    }
    if (!ImGui_ImplDX11_Init(device_->device(), device_->context())) {
      sm::log::Write(sm::LogLevel::Error, "ui", "imgui 的 DX11 后端初始化失败");
      Shutdown();
      return false;
    }
    imgui_ready_ = true;
    return true;
  }

  void Shutdown() override {
    if (imgui_ready_) {
      ImGui_ImplDX11_Shutdown();
      imgui_ready_ = false;
    }
    ReleaseRenderTarget();
    ReleaseSwapChain();
    device_.reset();
    window_ = nullptr;
    device_lost_ = false;
  }

  void NewFrame() override { ImGui_ImplDX11_NewFrame(); }

  bool BeginFrame(const float clear_rgba[4]) override {
    if (device_ == nullptr || render_target_ == nullptr) return false;

    int width = 0;
    int height = 0;
    window_->ClientSize(&width, &height);
    if (width > 0 && height > 0 &&
        (width != static_cast<int>(swap_width_) ||
         height != static_cast<int>(swap_height_))) {
      Resize(static_cast<unsigned int>(width), static_cast<unsigned int>(height));
      if (render_target_ == nullptr) return false;
    }

    device_->context()->OMSetRenderTargets(1, &render_target_, nullptr);
    device_->context()->ClearRenderTargetView(render_target_, clear_rgba);
    return true;
  }

  void RenderDrawData() override {
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
  }

  bool Present() override {
    if (swap_chain_ == nullptr) return false;
    const HRESULT hr = swap_chain_->Present(1, 0);
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
      sm::log::Write(sm::LogLevel::Warn, "ui", "图形设备丢失，将重建 UI 会话");
      device_lost_ = true;
      return false;
    }
    return SUCCEEDED(hr);
  }

  bool alive() const override { return device_ != nullptr && !device_lost_; }
  const char* name() const override { return "DX11"; }

 private:
  bool CreateSwapChain(HWND hwnd) {
    ID3D11Device* const device = device_->device();

    IDXGIDevice* dxgi_device = nullptr;
    if (FAILED(device->QueryInterface(__uuidof(IDXGIDevice),
                                      reinterpret_cast<void**>(&dxgi_device)))) {
      sm::log::Write(sm::LogLevel::Error, "ui", "取 DXGI 设备失败");
      return false;
    }
    IDXGIAdapter* adapter = nullptr;
    dxgi_device->GetAdapter(&adapter);
    dxgi_device->Release();
    IDXGIFactory* factory = nullptr;
    if (adapter != nullptr) {
      adapter->GetParent(__uuidof(IDXGIFactory), reinterpret_cast<void**>(&factory));
      adapter->Release();
    }
    if (factory == nullptr) {
      sm::log::Write(sm::LogLevel::Error, "ui", "取 DXGI 工厂失败");
      return false;
    }

    int client_w = 0;
    int client_h = 0;
    window_->ClientSize(&client_w, &client_h);
    const unsigned int width = client_w > 0 ? static_cast<unsigned int>(client_w) : 1;
    const unsigned int height = client_h > 0 ? static_cast<unsigned int>(client_h) : 1;

    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferCount = 2;
    desc.BufferDesc.Width = width;
    desc.BufferDesc.Height = height;
    desc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.OutputWindow = hwnd;
    desc.SampleDesc.Count = 1;
    desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    const HRESULT hr = factory->CreateSwapChain(device, &desc, &swap_chain_);
    factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
    factory->Release();
    if (FAILED(hr)) {
      sm::log::Writef(sm::LogLevel::Error, "ui", "创建交换链失败（hr=0x%08lX）",
                      static_cast<unsigned long>(hr));
      return false;
    }

    swap_width_ = width;
    swap_height_ = height;
    CreateRenderTarget();
    if (render_target_ == nullptr) {
      sm::log::Write(sm::LogLevel::Error, "ui", "交换链的后备缓冲不可用");
      return false;
    }
    SetViewport(width, height);
    return true;
  }

  void ReleaseSwapChain() {
    if (swap_chain_ != nullptr) {
      swap_chain_->Release();
      swap_chain_ = nullptr;
    }
    swap_width_ = 0;
    swap_height_ = 0;
  }

  void CreateRenderTarget() {
    if (swap_chain_ == nullptr || device_ == nullptr) return;
    ID3D11Texture2D* back_buffer = nullptr;
    if (FAILED(swap_chain_->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                      reinterpret_cast<void**>(&back_buffer))) ||
        back_buffer == nullptr) {
      sm::log::Write(sm::LogLevel::Error, "ui", "取交换链后备缓冲失败");
      return;
    }
    device_->device()->CreateRenderTargetView(back_buffer, nullptr, &render_target_);
    back_buffer->Release();
  }

  void ReleaseRenderTarget() {
    if (render_target_ != nullptr) {
      render_target_->Release();
      render_target_ = nullptr;
    }
  }

  void SetViewport(unsigned int width, unsigned int height) {
    if (device_ == nullptr) return;
    D3D11_VIEWPORT viewport{};
    viewport.Width = static_cast<float>(width);
    viewport.Height = static_cast<float>(height);
    viewport.MaxDepth = 1.0f;
    device_->context()->RSSetViewports(1, &viewport);
  }

  void Resize(unsigned int width, unsigned int height) {
    if (swap_chain_ == nullptr) return;

    if (device_ != nullptr) {
      device_->context()->OMSetRenderTargets(0, nullptr, nullptr);
    }
    ReleaseRenderTarget();

    if (FAILED(swap_chain_->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0))) {
      sm::log::Writef(sm::LogLevel::Warn, "ui", "交换链尺寸调整失败（%ux%u），本帧跳过绘制",
                      width, height);
      return;
    }
    swap_width_ = width;
    swap_height_ = height;
    CreateRenderTarget();
    if (render_target_ != nullptr) SetViewport(width, height);
  }

  platform::window::IWindow* window_ = nullptr;
  std::shared_ptr<Dx11Device> device_;
  IDXGISwapChain*            swap_chain_ = nullptr;
  ID3D11RenderTargetView*    render_target_ = nullptr;
  unsigned int               swap_width_ = 0;
  unsigned int               swap_height_ = 0;
  bool                       imgui_ready_ = false;
  bool                       device_lost_ = false;
};

}  // namespace

IRenderBackend* CreateDx11Backend() { return new (std::nothrow) Dx11Backend(); }

bool Dx11HardwareAvailable() {
  const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
  const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
                                      D3D_FEATURE_LEVEL_10_0};
  ID3D11Device* device = nullptr;
  const HRESULT hr = ::D3D11CreateDevice(
      nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels,
      static_cast<UINT>(sizeof(levels) / sizeof(levels[0])), D3D11_SDK_VERSION,
      &device, nullptr, nullptr);
  if (device != nullptr) device->Release();
  return SUCCEEDED(hr);
}

}  // namespace host
}  // namespace sm

# Research: Native Vulkan Backend Support for Swordfare (ImGui Overlay)

## Executive Summary
Currently, the modern Dear ImGui overlay (**Swordfare** and **Raijin Lua Console**) is only initialized and drawn under OpenGL. In Vulkan mode (`--vulkan`), the engine falls back to legacy bitmap-based debug drawings, and modern features like the Lua console or script editor are completely unavailable.

Attempting to run an OpenGL overlay on top of Vulkan using context sharing/interop is extremely complex, driver-sensitive, and resource-heavy. 

The **most elegant and high-performance solution** is to implement the native Dear ImGui Vulkan backend (`imgui_impl_vulkan.cpp`) using the existing Vulkan structures already initialized by the custom `VulkanBackend` engine wrapper.

---

## Technical Feasibility & Architecture

The project has a custom `VulkanBackend` class in [vulkan_backend.h](file:///home/quantumcreeper/SwordigoDesktop/src/platform/vulkan_backend.h) that encapsulates all Vulkan pipeline resources:
* `VkInstance`, `VkDevice`, `VkQueue`, `VkPhysicalDevice`
* `VkRenderPass` (compatibly handles color clear/store)
* `VkDescriptorPool` and `VkCommandPool`
* Per-frame `VkCommandBuffer` pools

### Step 1: Adding Getters to VulkanBackend
To initialize the ImGui Vulkan backend, we must expose key private pointers from `VulkanBackend` by adding getter methods to [vulkan_backend.h](file:///home/quantumcreeper/SwordigoDesktop/src/platform/vulkan_backend.h):
```cpp
VkInstance       get_instance() const { return instance_; }
VkPhysicalDevice get_physical_device() const { return physical_device_; }
VkDevice         get_device() const { return device_; }
VkQueue          get_graphics_queue() const { return graphics_queue_; }
VkRenderPass     get_render_pass() const { return render_pass_; }
VkDescriptorPool get_descriptor_pool() const { return descriptor_pool_; }
VkCommandBuffer  get_current_command_buffer() const { return command_buffers_[current_image_index_]; }
uint32_t         get_image_count() const { return (uint32_t)swapchain_images_.size(); }
```

### Step 2: Incorporating ImGui Vulkan Backend
We need to copy `imgui_impl_vulkan.h` and `imgui_impl_vulkan.cpp` into `src/imgui/backends/` and add them to the build script (`Makefile` / `CMakeLists.txt`).

### Step 3: Initialization in `swordfare_gui.cpp`
When `g_graphics_api == GraphicsAPI::VULKAN`, we initialize the Vulkan backend instead of OpenGL:
```cpp
ImGui_ImplSDL3_InitForVulkan(window);

ImGui_ImplVulkan_InitInfo init_info = {};
init_info.Instance = g_vk_backend.get_instance();
init_info.PhysicalDevice = g_vk_backend.get_physical_device();
init_info.Device = g_vk_backend.get_device();
init_info.QueueFamily = graphics_queue_family_index; // Expose from backend
init_info.Queue = g_vk_backend.get_graphics_queue();
init_info.PipelineCache = VK_NULL_HANDLE;
init_info.DescriptorPool = g_vk_backend.get_descriptor_pool();
init_info.RenderPass = g_vk_backend.get_render_pass();
init_info.Subpass = 0;
init_info.MinImageCount = g_vk_backend.get_image_count();
init_info.ImageCount = g_vk_backend.get_image_count();
init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
init_info.Allocator = nullptr;

ImGui_ImplVulkan_Init(&init_info);
```

### Step 4: Render Loop integration
Inside `src/main.cpp`, during the draw callback:
1. Start/Record the frame as usual.
2. Call `g_swordfare_gui.draw_debug(...)` / console.
3. ImGui will record draw data.
4. Right before ending the Vulkan render pass, call:
   ```cpp
   ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), g_vk_backend.get_current_command_buffer());
   ```
5. `VulkanBackend::end_frame_and_present()` will submit the command buffer and present.

---

## Conclusion & Next Steps
This native approach is:
* **Robust**: Zero driver-level context issues.
* **Efficient**: Integrates seamlessly inside the active frame render pass of `VulkanBackend`.
* **Clean**: Preserves the modern ImGui codebase without creating duplicated drawing layers or legacy OpenGL overrides.

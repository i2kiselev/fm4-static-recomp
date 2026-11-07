// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
// Exercise the actual runtime providers and GPU dispatch, without game assets.
#include <bit>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ui/vulkan/provider.h>
#include <rex/ui/vulkan/upscaler.h>

namespace {
using namespace rex::ui::vulkan;
void Check(VkResult result, const char* action) {
  if (result != VK_SUCCESS) throw std::runtime_error(std::string(action) + ": " + std::to_string(result));
}
struct Image {
  const VulkanDevice& gpu;
  VulkanUpscalerImage description{};
  VkDeviceMemory memory{};
  Image(const VulkanDevice& gpu, uint32_t width, uint32_t height, VkFormat format, bool output) : gpu(gpu) {
    const auto& fn = gpu.functions();
    description.format = format;
    description.width = width;
    description.height = height;
    description.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | (output ? VK_IMAGE_USAGE_STORAGE_BIT : 0);
    VkImageCreateInfo create{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    create.imageType = VK_IMAGE_TYPE_2D;
    create.format = format;
    create.extent = {width, height, 1};
    create.mipLevels = create.arrayLayers = 1;
    create.samples = VK_SAMPLE_COUNT_1_BIT;
    create.tiling = VK_IMAGE_TILING_OPTIMAL;
    create.usage = description.usage;
    Check(fn.vkCreateImage(gpu.device(), &create, nullptr, &description.image), "create image");
    VkMemoryRequirements requirements{};
    fn.vkGetImageMemoryRequirements(gpu.device(), description.image, &requirements);
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocation.allocationSize = requirements.size;
    const auto types = requirements.memoryTypeBits & gpu.memory_types().device_local;
    if (!types) throw std::runtime_error("no image memory type");
    allocation.memoryTypeIndex = std::countr_zero(types);
    Check(fn.vkAllocateMemory(gpu.device(), &allocation, nullptr, &memory), "allocate image");
    Check(fn.vkBindImageMemory(gpu.device(), description.image, memory, 0), "bind image");
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = description.image;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = format;
    view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    Check(fn.vkCreateImageView(gpu.device(), &view, nullptr, &description.view), "image view");
  }
  ~Image() {
    const auto& fn = gpu.functions();
    fn.vkDestroyImageView(gpu.device(), description.view, nullptr);
    fn.vkDestroyImage(gpu.device(), description.image, nullptr);
    fn.vkFreeMemory(gpu.device(), memory, nullptr);
  }
};
void Transition(const VulkanDevice& gpu, VkCommandBuffer command, VkImage image,
                VkImageLayout from, VkImageLayout to) {
  VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  barrier.oldLayout = from;
  barrier.newLayout = to;
  barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = image;
  barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  barrier.srcAccessMask = from == VK_IMAGE_LAYOUT_UNDEFINED ? 0 : VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;
  barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
  gpu.functions().vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
      VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

void Exercise(const VulkanDevice& gpu, VulkanUpscaler& upscaler) {
  constexpr uint32_t input_size = 64, output_size = 128;
  const auto device = gpu.device();
  const auto& fn = gpu.functions();
  const auto wait_idle = reinterpret_cast<PFN_vkDeviceWaitIdle>(
      gpu.vulkan_instance()->functions().vkGetDeviceProcAddr(device, "vkDeviceWaitIdle"));
  if (!upscaler.Prepare(input_size, input_size, output_size, output_size, false))
    throw std::runtime_error(upscaler.info().id + " context creation failed");
  Image color(gpu, input_size, input_size, VK_FORMAT_R8G8B8A8_UNORM, false);
  Image depth(gpu, input_size, input_size, VK_FORMAT_R32_SFLOAT, false);
  Image motion(gpu, input_size, input_size, VK_FORMAT_R16G16_SFLOAT, false);
  Image output(gpu, output_size, output_size, VK_FORMAT_R8G8B8A8_UNORM, true);
  VkBuffer buffer{};
  VkDeviceMemory memory{};
  VkCommandPool pool{};
  VkFence fence{};
  // Ensure resources remain alive through submission and clean up on failures too.
  struct Cleanup {
    const VulkanDevice& gpu; PFN_vkDeviceWaitIdle idle;
    VkBuffer& buffer; VkDeviceMemory& memory; VkCommandPool& pool; VkFence& fence;
    ~Cleanup() {
      idle(gpu.device());
      const auto& f = gpu.functions();
      if (fence) f.vkDestroyFence(gpu.device(), fence, nullptr);
      if (pool) f.vkDestroyCommandPool(gpu.device(), pool, nullptr);
      if (buffer) f.vkDestroyBuffer(gpu.device(), buffer, nullptr);
      if (memory) f.vkFreeMemory(gpu.device(), memory, nullptr);
    }
  } cleanup{gpu, wait_idle, buffer, memory, pool, fence};
  VkBufferCreateInfo create_buffer{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  create_buffer.size = output_size * output_size * 4;
  create_buffer.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  Check(fn.vkCreateBuffer(device, &create_buffer, nullptr, &buffer), "create readback");
  VkMemoryRequirements requirements{};
  fn.vkGetBufferMemoryRequirements(device, buffer, &requirements);
  VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  allocate.allocationSize = requirements.size;
  auto types = requirements.memoryTypeBits & gpu.memory_types().host_visible;
  if (!types) throw std::runtime_error("no readback memory type");
  allocate.memoryTypeIndex = std::countr_zero(types);
  Check(fn.vkAllocateMemory(device, &allocate, nullptr, &memory), "allocate readback");
  Check(fn.vkBindBufferMemory(device, buffer, memory, 0), "bind readback");
  VkCommandPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  pool_info.queueFamilyIndex = gpu.queue_family_graphics_compute();
  pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  Check(fn.vkCreateCommandPool(device, &pool_info, nullptr, &pool), "create command pool");
  VkCommandBufferAllocateInfo command_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  command_info.commandPool = pool;
  command_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  command_info.commandBufferCount = 1;
  VkCommandBuffer command{};
  Check(fn.vkAllocateCommandBuffers(device, &command_info, &command), "allocate commands");
  VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
  Check(fn.vkCreateFence(device, &fence_info, nullptr, &fence), "create fence");
  for (uint32_t frame = 0; frame < 3; ++frame) {
    Check(fn.vkResetCommandPool(device, pool, 0), "reset command pool");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    Check(fn.vkBeginCommandBuffer(command, &begin), "begin commands");
    if (!frame) {
      for (Image* image : {&color, &depth, &motion}) {
        Transition(gpu, command, image->description.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        const VkClearColorValue clear = image == &color ? VkClearColorValue{{0.25f, 0.5f, 0.75f, 1.0f}}
            : image == &depth ? VkClearColorValue{{0.5f, 0, 0, 0}} : VkClearColorValue{{0, 0, 0, 0}};
        const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        fn.vkCmdClearColorImage(command, image->description.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear, 1, &range);
        Transition(gpu, command, image->description.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
      }
    }
    Transition(gpu, command, output.description.image,
        frame ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
    VulkanUpscalerInputs inputs{};
    inputs.color = color.description;
    inputs.depth = depth.description;
    inputs.motion_vectors = motion.description;
    inputs.output = output.description;
    inputs.frame.render_width = inputs.frame.render_height = input_size;
    inputs.frame.output_width = inputs.frame.output_height = output_size;
    inputs.frame.motion_vector_scale_x = inputs.frame.motion_vector_scale_y = input_size;
    inputs.frame.reset = frame == 0;
    if (!upscaler.Dispatch(command, inputs)) throw std::runtime_error("dispatch refused");
    Transition(gpu, command, output.description.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {output_size, output_size, 1};
    fn.vkCmdCopyImageToBuffer(command, output.description.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1, &copy);
    VkMemoryBarrier host_barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    host_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    host_barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    fn.vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &host_barrier, 0, nullptr, 0, nullptr);
    Check(fn.vkEndCommandBuffer(command), "end commands");
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &command;
    {
      auto queue = gpu.AcquireQueue(gpu.queue_family_graphics_compute(), 0);
      Check(fn.vkQueueSubmit(queue.queue(), 1, &submit, fence), "submit upscale");
    }
    Check(fn.vkWaitForFences(device, 1, &fence, VK_TRUE, 30000000000ull), "wait for upscale");
    Check(fn.vkResetFences(device, 1, &fence), "reset fence");
    void* mapped{};
    Check(fn.vkMapMemory(device, memory, 0, VK_WHOLE_SIZE, 0, &mapped), "map readback");
    VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
    range.memory = memory;
    range.size = VK_WHOLE_SIZE;
    Check(fn.vkInvalidateMappedMemoryRanges(device, 1, &range), "invalidate readback");
    const auto* bytes = static_cast<const uint8_t*>(mapped);
    const size_t center = (output_size / 2 * output_size + output_size / 2) * 4;
    const int r = bytes[center], g = bytes[center + 1], b = bytes[center + 2];
    fn.vkUnmapMemory(device, memory);
    std::cout << upscaler.info().id << " frame " << frame << " readback RGB " << r << ',' << g << ',' << b << std::endl;
    if (std::abs(r - 64) > 12 || std::abs(g - 128) > 12 || std::abs(b - 191) > 12)
      throw std::runtime_error("upscaled color differs from input");
  }
  // Exercise resizing too, as the window can change size between frames.
  Check(wait_idle(device), "idle before resize");
  if (!upscaler.Prepare(input_size, input_size, 192, 128, false))
    throw std::runtime_error("resized context creation failed");
  upscaler.Release();
}
}

int main(int argc, char** argv) {
  try {
    rex::cvar::Init(argc, argv);
    rex::cvar::ApplyEnvironment();
    rex::InitLoggingEarly();
    auto provider = VulkanProvider::Create(false, false);
    if (!provider) throw std::runtime_error("Vulkan device creation failed");
    const auto& gpu = *provider->vulkan_device();
    std::cout << "GPU: " << gpu.properties().deviceName << std::endl;
    auto upscalers = CreateVulkanUpscalers(&gpu);
    unsigned passed = 0;
    for (const auto& upscaler : upscalers) {
      const auto& info = upscaler->info();
      std::cout << info.id << ": " << (info.available ? info.version : info.unavailable_reason) << std::endl;
      if (info.id != "fsr2" && info.id != "fsr3") continue;
      if (!info.available) throw std::runtime_error(info.id + " unavailable: " + info.unavailable_reason);
      Exercise(gpu, *upscaler);
      ++passed;
    }
    if (passed != 2) throw std::runtime_error("both FSR providers must pass");
    std::cout << "PASS: FSR 2 and FSR 3 GPU dispatch, history and context resize" << std::endl;
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "Upscaler smoke failed: " << error.what() << std::endl;
    return 1;
  }
}

// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <rex/logging.h>
#include <rex/graphics/shared_memory.h>
#include <rex/system/xmemory.h>

static void Check(bool value, const char* text) {
  if (!value) { std::fprintf(stderr, "FAIL: %s\n", text); std::exit(1); }
}
class TestSharedMemory final : public rex::graphics::SharedMemory {
 public:
  explicit TestSharedMemory(rex::memory::Memory& memory) : SharedMemory(memory) {
    InitializeCommon();
  }
  uint32_t uploads = 0;
 private:
  bool UploadRanges(const std::vector<std::pair<uint32_t, uint32_t>>& ranges) override {
    for (const auto& [first, count] : ranges) {
      uploads += count;
      MakeRangeValid(first << page_size_log2(), count << page_size_log2(), false);
    }
    return true;
  }
};
int main() {
  rex::InitLogging();
  rex::memory::Memory memory;
  Check(memory.Initialize(), "initialize guest memory");
  if (rex::memory::page_size() != 16384) {
    std::puts("SKIP: requires 16 KB host pages");
    return 0;
  }
  auto* heap = static_cast<rex::memory::PhysicalHeap*>(memory.LookupHeap(0xA0000000));
  uint32_t address = 0;
  Check(heap->Alloc(65536, 65536,
      rex::memory::kMemoryAllocationReserve | rex::memory::kMemoryAllocationCommit,
      rex::memory::kMemoryProtectRead | rex::memory::kMemoryProtectWrite, false, &address),
      "allocate watched physical memory");
  const uint32_t physical = heap->GetPhysicalAddress(address);
  auto* cpu = memory.TranslateVirtual<uint8_t*>(address);
  std::memset(cpu, 0, 16384);
  {
    uint32_t cpu_invalidations = 0;
    TestSharedMemory shared(memory);
    shared.RangeWrittenByGpu(physical, 16384);
    Check(shared.CountValidPages(physical, 16384).second == 4, "four GPU-owned guest pages");
    shared.WatchMemoryRange(physical + 4096, 4096,
        [](const std::unique_lock<std::recursive_mutex>&, void* context, void*, uint64_t, bool gpu) {
          if (!gpu) ++*static_cast<uint32_t*>(context);
        }, &cpu_invalidations, nullptr, 0);
    *reinterpret_cast<volatile uint32_t*>(cpu) = 1;
    Check(shared.CountValidPages(physical + 4096, 4096).second == 1,
          "neighbor GPU page preserved after first fault");
    Check(!shared.CheckUnprotectedGpuPages(), "unchanged neighbors remain GPU-owned");
    *reinterpret_cast<volatile uint32_t*>(cpu + 4096) = 2;
    Check(shared.CountValidPages(physical + 4096, 4096).second == 1,
          "second store has no host-page fault");
    Check(cpu_invalidations == 0, "preserved texture watch has not fired spuriously");
    Check(shared.CheckUnprotectedGpuPages(), "detect later write to unprotected neighbor");
    Check(cpu_invalidations == 1, "hidden CPU write invalidates texture watches");
    Check(shared.CountValidPages(physical + 4096, 4096).first == 0,
          "changed neighbor must be uploaded again");
    Check(shared.CountValidPages(physical + 8192, 8192).second == 2,
          "untouched resolve pages must survive");
    Check(shared.RequestRange(physical + 4096, 4096), "upload changed CPU page");
    Check(shared.uploads == 1, "only changed page is uploaded");
    Check(!shared.CheckUnprotectedGpuPages(), "stable neighbors do not invalidate repeatedly");
    std::array<uint8_t, 4096> readback;
    readback.fill(3);
    shared.CopyReadbackToGuest(physical + 8192, readback.data(), uint32_t(readback.size()));
    Check(!shared.CheckUnprotectedGpuPages(), "GPU readbacks refresh the baseline without CPU invalidation");
    Check(shared.CountValidPages(physical + 8192, 4096).second == 1,
          "GPU readback retains GPU ownership");
    // A new GPU result rearms protection and supersedes its old CPU snapshot.
    shared.RangeWrittenByGpu(physical + 8192, 4096);
    Check(!shared.CheckUnprotectedGpuPages(), "new GPU result is not mistaken for a CPU store");
    // A bulk store faults only on its first guest page, then reaches another
    // GPU page within the same writable host page.
    std::memset(cpu + 8192, 7, 8192);
    Check(shared.CheckUnprotectedGpuPages(), "detect bulk store beyond the faulting guest page");
    Check(shared.CountValidPages(physical + 8192, 8192).first == 0,
          "both guest pages of the bulk store are invalidated");
    shared.InvalidateAllPages();
    Check(!shared.CheckUnprotectedGpuPages(), "cache invalidation retires snapshots");
  }
  heap->Release(address, nullptr);
  std::puts("PASS: real 16 KB write fault, hidden 4 KB write, selective upload, GPU preservation");
}

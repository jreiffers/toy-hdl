#include "cpu/fpga.h"

#include "absl/types/span.h"

absl::Span<const FpgaResource> AllFpgaResources() {
  static std::array<FpgaResource, 7> res{
      FpgaResource::kIn,   FpgaResource::kNor, FpgaResource::kNand,
      FpgaResource::kLut2, FpgaResource::kFF,  FpgaResource::kMux,
      FpgaResource::kOut};
  return res;
}

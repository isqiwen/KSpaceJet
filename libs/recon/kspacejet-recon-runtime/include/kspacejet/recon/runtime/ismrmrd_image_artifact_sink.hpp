#pragma once

#include "kspacejet/base/result.hpp"
#include "kspacejet/ismrmrd/dataset_reader.hpp"
#include "kspacejet/recon/scan_descriptor.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace ksj::recon::runtime {

class EgressInputLease;

// The ISMRMRD fields and runtime provenance that describe one terminal
// magnitude image. The image payload remains in the graph egress lease until
// the Sink has written and verified the artifact.
struct IsmrmrdMagnitudeImageArtifactDescriptor final {
  std::string source_xml;
  ksj::ismrmrd::AcquisitionHeader source_acquisition{};
  FieldOfViewMm field_of_view_mm{};
  std::uint32_t rows{0U};
  std::uint32_t cols{0U};

  // Each pair becomes one value in the standard ISMRMRD MetaContainer. Names
  // under the KSpaceJet namespace are provenance extensions stored in the
  // standard MetaAttributes XML container, never a private sidecar file.
  std::vector<std::pair<std::string, std::string>> provenance_attributes;
};

// Runtime-owned terminal Sink for one standard ISMRMRD HDF5 magnitude image
// artifact. It is not a Provider Operator: Providers produce internal image
// values, while the runtime owns output path selection, publication and
// verification. `commit()` retains no borrowed graph payload after it
// returns, and acknowledges the graph egress only after the file has passed
// its readback verification and atomic publication.
class IsmrmrdImageArtifactSink final {
public:
  IsmrmrdImageArtifactSink(std::filesystem::path output_file, IsmrmrdMagnitudeImageArtifactDescriptor descriptor);

  [[nodiscard]] const std::filesystem::path& output_file() const noexcept;

  // Writes a standard ISMRMRD HDF5 image artifact with group `dataset` and
  // series `image_0`. The Sink closes and reads back a unique sibling
  // temporary file before atomically replacing the requested destination. It
  // expects exactly one `ksj.image-frame` payload and no graph metadata.
  // Publication and egress acknowledgement cannot be one atomic operation;
  // a failed acknowledgment is reported after the artifact is published.
  [[nodiscard]] ksj::base::Status commit(EgressInputLease& image);

private:
  std::filesystem::path output_file_;
  IsmrmrdMagnitudeImageArtifactDescriptor descriptor_;
  bool published_{false};
};

} // namespace ksj::recon::runtime

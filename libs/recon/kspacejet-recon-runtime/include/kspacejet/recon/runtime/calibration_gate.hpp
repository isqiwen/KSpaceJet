#pragma once

#include "kspacejet/base/result.hpp"

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace ksj::recon::runtime {

struct CalibrationGateLimits {
  std::uint64_t max_active_keys{0};
  std::uint64_t max_waiting_items_per_key{0};
  std::uint64_t max_waiting_bytes_per_key{0};
  std::uint64_t max_waiting_items_total{0};
  std::uint64_t max_waiting_bytes_total{0};
};

struct PendingCalibrationFrame {
  std::uint64_t sequence{0};
  std::uint64_t charged_bytes{0};
};

struct CalibrationToken {
  std::string digest;
  std::uint32_t epoch{0};
};

enum class CalibrationDisposition {
  ready,
  waiting,
};

// Bounded per-CalibKey gate.  Payload ownership remains in the caller's
// BufferHandle/ledger; this primitive only accounts its declared retained size
// and returns the sequence records to release when CalibrationReady arrives.
class CalibrationGate {
public:
  explicit CalibrationGate(CalibrationGateLimits limits);

  [[nodiscard]] ksj::base::Result<CalibrationDisposition> await_or_pass(std::string key,
                                                                          PendingCalibrationFrame frame);
  [[nodiscard]] ksj::base::Result<std::vector<PendingCalibrationFrame>> publish_ready(std::string key,
                                                                                         CalibrationToken token);
  [[nodiscard]] ksj::base::Result<CalibrationToken> token_for(const std::string& key) const;

  // Called after EndOfInput.  It returns every key that still has retained
  // imaging data but no CalibrationReady token, so the caller can fail the
  // scan with CALIBRATION_MISSING_AT_EOI rather than silently dropping data.
  [[nodiscard]] std::vector<std::string> close_missing();

  [[nodiscard]] std::uint64_t waiting_items() const;
  [[nodiscard]] std::uint64_t waiting_bytes() const;

private:
  struct KeyState {
    bool ready{false};
    bool closed_missing{false};
    CalibrationToken token{};
    std::uint64_t waiting_bytes{0};
    std::vector<PendingCalibrationFrame> waiting{};
  };

  [[nodiscard]] bool can_add_key(const std::string& key) const noexcept;
  [[nodiscard]] bool can_retain(const KeyState& state, PendingCalibrationFrame frame) const noexcept;

  CalibrationGateLimits limits_;
  mutable std::mutex mutex_;
  std::unordered_map<std::string, KeyState> keys_;
  std::uint64_t waiting_items_{0};
  std::uint64_t waiting_bytes_{0};
};

} // namespace ksj::recon::runtime

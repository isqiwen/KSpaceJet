#pragma once

#include "kspacejet/recon/operator_contract.hpp"
#include "kspacejet/recon/result.hpp"

#include <string>
#include <string_view>

namespace ksj::recon::graph {

// Strictly parse the one current OperatorContract artifact shape:
// {"kind":"OperatorContract","operator_id":...,"ports":[...]}.
// It rejects duplicate keys, floating point values, oversized documents,
// unknown/stale fields, and invalid current port shapes before resolving every
// TypeRef through OperatorContract::create().
[[nodiscard]] Result<OperatorContract> parse_operator_contract_json(std::string_view document);

} // namespace ksj::recon::graph

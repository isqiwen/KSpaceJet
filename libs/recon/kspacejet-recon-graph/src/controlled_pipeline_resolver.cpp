#include "kspacejet/recon/graph/controlled_pipeline_resolver.hpp"

#include "kspacejet/recon/type_registry.hpp"

#include <algorithm>
#include <iterator>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ksj::recon::graph {
namespace {

[[nodiscard]] Status validation(std::string message) {
  return Status::ValidationError(std::move(message));
}

struct NodeContractView final {
  const PipelineNode* node{nullptr};
  const OperatorContract* contract{nullptr};
};

[[nodiscard]] const ControlledProviderSnapshot* find_provider(const std::vector<ControlledProviderSnapshot>& providers,
                                                              const std::string_view provider_id) {
  const auto found = std::find_if(providers.begin(), providers.end(), [&](const ControlledProviderSnapshot& provider) {
    return provider.provider_id == provider_id;
  });
  return found == providers.end() ? nullptr : &*found;
}

[[nodiscard]] const OperatorContract* find_contract(const ControlledProviderSnapshot& provider,
                                                    const std::string_view operator_id) {
  const auto found = std::find_if(provider.operator_contracts.begin(), provider.operator_contracts.end(),
                                  [&](const OperatorContract& contract) {
                                    return contract.operator_id() == operator_id;
                                  });
  return found == provider.operator_contracts.end() ? nullptr : &*found;
}

[[nodiscard]] const NodeContractView* find_node(const std::vector<NodeContractView>& nodes,
                                                const std::string_view node_id) {
  const auto found = std::find_if(nodes.begin(), nodes.end(), [&](const NodeContractView& value) {
    return value.node->id == node_id;
  });
  return found == nodes.end() ? nullptr : &*found;
}

[[nodiscard]] const ResolvedPort* find_port(const OperatorContract& contract, const std::string_view port_name) {
  const auto found = std::find_if(contract.ports().begin(), contract.ports().end(), [&](const ResolvedPort& port) {
    return port.name == port_name;
  });
  return found == contract.ports().end() ? nullptr : &*found;
}

[[nodiscard]] Result<const ResolvedPort*> require_port(const std::vector<NodeContractView>& nodes,
                                                       const NodePortReference& endpoint, const PortDirection direction,
                                                       const std::string_view context) {
  const auto* node = find_node(nodes, endpoint.node);
  if (node == nullptr) {
    return validation(std::string(context) + " references unknown node '" + endpoint.node + "'.");
  }
  const auto* port = find_port(*node->contract, endpoint.port);
  if (port == nullptr) {
    return validation(std::string(context) + " references unknown port '" + endpoint.node + "." + endpoint.port +
                      "' on OperatorContract '" + node->contract->operator_id() + "'.");
  }
  if (port->direction != direction) {
    return validation(std::string(context) + " requires a " +
                      std::string(direction == PortDirection::input ? "input" : "output") + " port but '" +
                      endpoint.node + "." + endpoint.port + "' declares the opposite direction.");
  }
  return port;
}

[[nodiscard]] Status require_exact_type(const TypeDescriptor& expected, const TypeDescriptor& actual,
                                        const std::string_view context) {
  if (!expected.exactly_matches(actual)) {
    return validation(std::string(context) + " has incompatible TypeRef/TypeDescriptor ('" +
                      expected.type_ref().value() + "' versus '" + actual.type_ref().value() + "').");
  }
  return Status::Ok();
}

[[nodiscard]] Result<TypeDescriptor> resolve_type(const std::string_view type_ref, const std::string_view context) {
  auto descriptor = types::resolve(type_ref);
  if (!descriptor.ok()) {
    return validation(std::string(context) +
                      " names a TypeRef absent from the checked-in registry: " + descriptor.status().message());
  }
  return descriptor;
}

[[nodiscard]] std::string address(const NodePortReference& endpoint) {
  return endpoint.node + "\x1f" + endpoint.port;
}

[[nodiscard]] Status add_input(std::map<std::string, unsigned int>& counts, const NodePortReference& endpoint,
                               const std::string_view context) {
  auto& count = counts[address(endpoint)];
  ++count;
  if (count > 1U) {
    return validation(std::string(context) + " gives input '" + endpoint.node + "." + endpoint.port +
                      "' more than one source; insert an explicit merge Operator.");
  }
  return Status::Ok();
}

[[nodiscard]] void add_output(std::map<std::string, unsigned int>& counts, const NodePortReference& endpoint) {
  ++counts[address(endpoint)];
}

[[nodiscard]] Status validate_contract_port_coverage(const std::vector<NodeContractView>& nodes,
                                                     const std::map<std::string, unsigned int>& inputs,
                                                     const std::map<std::string, unsigned int>& outputs) {
  for (const auto& node : nodes) {
    for (const auto& port : node.contract->ports()) {
      const auto key = node.node->id + "\x1f" + port.name;
      const auto& counts = port.direction == PortDirection::input ? inputs : outputs;
      const auto found = counts.find(key);
      if (found == counts.end() || found->second == 0U) {
        return validation("Node '" + node.node->id + "' leaves " +
                          std::string(port.direction == PortDirection::input ? "input" : "output") + " port '" +
                          port.name + "' of OperatorContract '" + node.contract->operator_id() + "' unbound.");
      }
    }
  }
  return Status::Ok();
}

[[nodiscard]] Status validate_graph_contracts(const PipelineDefinition& definition,
                                              const std::vector<NodeContractView>& nodes) {
  std::map<std::string, unsigned int> input_counts;
  std::map<std::string, unsigned int> output_counts;

  for (const auto& edge : definition.edges()) {
    const auto source = require_port(nodes, edge.from, PortDirection::output, "Edge '" + edge.id + "' source");
    if (!source.ok()) {
      return source.status();
    }
    const auto destination = require_port(nodes, edge.to, PortDirection::input, "Edge '" + edge.id + "' destination");
    if (!destination.ok()) {
      return destination.status();
    }
    const auto type_status = require_exact_type(source.value()->type_descriptor, destination.value()->type_descriptor,
                                                "Edge '" + edge.id + "'");
    if (!type_status.ok()) {
      return type_status;
    }
    const auto count_status = add_input(input_counts, edge.to, "Edge '" + edge.id + "'");
    if (!count_status.ok()) {
      return count_status;
    }
    add_output(output_counts, edge.from);
  }

  for (const auto& ingress : definition.ingress_ports()) {
    const auto destination =
      require_port(nodes, ingress.to, PortDirection::input, "Ingress '" + ingress.id + "' destination");
    if (!destination.ok()) {
      return destination.status();
    }
    const auto type = resolve_type(ingress.type, "Ingress '" + ingress.id + "'");
    if (!type.ok()) {
      return type.status();
    }
    const auto type_status =
      require_exact_type(type.value(), destination.value()->type_descriptor, "Ingress '" + ingress.id + "'");
    if (!type_status.ok()) {
      return type_status;
    }
    const auto count_status = add_input(input_counts, ingress.to, "Ingress '" + ingress.id + "'");
    if (!count_status.ok()) {
      return count_status;
    }
  }

  for (const auto& egress : definition.egress_ports()) {
    const auto source = require_port(nodes, egress.from, PortDirection::output, "Egress '" + egress.id + "' source");
    if (!source.ok()) {
      return source.status();
    }
    const auto type = resolve_type(egress.type, "Egress '" + egress.id + "'");
    if (!type.ok()) {
      return type.status();
    }
    const auto type_status =
      require_exact_type(source.value()->type_descriptor, type.value(), "Egress '" + egress.id + "'");
    if (!type_status.ok()) {
      return type_status;
    }
    add_output(output_counts, egress.from);
  }

  for (const auto& calibration : definition.calibration_bindings()) {
    const auto source = require_port(nodes, calibration.producer, PortDirection::output,
                                     "Calibration binding '" + calibration.id + "' producer");
    if (!source.ok()) {
      return source.status();
    }
    add_output(output_counts, calibration.producer);
    for (const auto& consumer : calibration.consumers) {
      const auto destination =
        require_port(nodes, consumer, PortDirection::input, "Calibration binding '" + calibration.id + "' consumer");
      if (!destination.ok()) {
        return destination.status();
      }
      const auto type_status = require_exact_type(source.value()->type_descriptor, destination.value()->type_descriptor,
                                                  "Calibration binding '" + calibration.id + "'");
      if (!type_status.ok()) {
        return type_status;
      }
      const auto count_status = add_input(input_counts, consumer, "Calibration binding '" + calibration.id + "'");
      if (!count_status.ok()) {
        return count_status;
      }
    }
  }

  return validate_contract_port_coverage(nodes, input_counts, output_counts);
}

} // namespace

Result<ControlledPipelineResolver>
ControlledPipelineResolver::create(std::vector<ControlledProviderSnapshot> providers) {
  if (providers.empty()) {
    return validation("ControlledPipelineResolver requires at least one executable Provider snapshot.");
  }

  std::map<std::string, bool> provider_ids;
  for (const auto& provider : providers) {
    if (provider.provider_id.empty()) {
      return validation("ControlledProviderSnapshot provider_id must not be empty.");
    }
    if (!provider_ids.emplace(provider.provider_id, true).second) {
      return validation("ControlledPipelineResolver contains duplicate Provider id '" + provider.provider_id + "'.");
    }
    if (provider.operator_contracts.empty()) {
      return validation("Controlled Provider '" + provider.provider_id + "' has no executable OperatorContract.");
    }
    std::map<std::string, bool> operator_ids;
    for (const auto& contract : provider.operator_contracts) {
      if (contract.operator_id().empty()) {
        return validation("Controlled Provider '" + provider.provider_id + "' has an empty OperatorContract id.");
      }
      if (!operator_ids.emplace(contract.operator_id(), true).second) {
        return validation("Controlled Provider '" + provider.provider_id + "' contains duplicate OperatorContract '" +
                          contract.operator_id() + "'.");
      }
    }
  }
  return ControlledPipelineResolver{std::move(providers)};
}

Result<ControlledPipelineResolution> ControlledPipelineResolver::resolve(const PipelineDefinition& definition) const {
  std::vector<ResolvedProvider> resolved_providers;
  resolved_providers.reserve(definition.provider_requirements().size());
  std::vector<const ControlledProviderSnapshot*> selected_providers;
  selected_providers.reserve(definition.provider_requirements().size());
  for (const auto& requirement : definition.provider_requirements()) {
    const auto* provider = find_provider(providers_, requirement.provider_id);
    if (provider == nullptr) {
      return validation("Pipeline Provider alias '" + requirement.alias + "' requires Provider '" +
                        requirement.provider_id + "', which is absent from the controlled executable snapshot.");
    }
    selected_providers.push_back(provider);
    resolved_providers.push_back({.alias = requirement.alias,
                                  .provider_id = provider->provider_id,
                                  .bundle_digest = provider->bundle_digest,
                                  .operators = {}});
  }

  std::vector<NodeContractView> node_views;
  node_views.reserve(definition.nodes().size());
  std::vector<ResolvedNodeContract> node_contracts;
  node_contracts.reserve(definition.nodes().size());
  for (const auto& node : definition.nodes()) {
    const auto provider_position =
      std::find_if(definition.provider_requirements().begin(), definition.provider_requirements().end(),
                   [&](const ProviderSelection& value) {
                     return value.alias == node.provider_alias;
                   });
    if (provider_position == definition.provider_requirements().end()) {
      return Status::InternalError("PipelineDefinition lost a parsed Provider alias while resolving node '" + node.id +
                                   "'.");
    }
    const auto provider_index =
      static_cast<std::size_t>(std::distance(definition.provider_requirements().begin(), provider_position));
    const auto* provider = selected_providers[provider_index];
    const auto* contract = find_contract(*provider, node.operator_id);
    if (contract == nullptr) {
      return validation("Node '" + node.id + "' requires OperatorContract '" + node.operator_id + "' from Provider '" +
                        provider->provider_id + "', but it is absent from the controlled executable snapshot.");
    }
    auto& resolved_provider = resolved_providers[provider_index];
    const auto existing = std::find_if(resolved_provider.operators.begin(), resolved_provider.operators.end(),
                                       [&](const ResolvedOperator& value) {
                                         return value.id == contract->operator_id();
                                       });
    if (existing == resolved_provider.operators.end()) {
      resolved_provider.operators.push_back(
        {.id = contract->operator_id(), .contract_digest = contract->artifact_digest()});
    }
    node_views.push_back({.node = &node, .contract = contract});
    node_contracts.push_back({.node_id = node.id, .contract = *contract});
  }

  const auto graph_status = validate_graph_contracts(definition, node_views);
  if (!graph_status.ok()) {
    return graph_status;
  }
  auto pipeline = ResolvedPipeline::resolve(definition, std::move(resolved_providers));
  if (!pipeline.ok()) {
    return pipeline.status();
  }
  return ControlledPipelineResolution{.pipeline = std::move(pipeline).value(),
                                      .node_contracts = std::move(node_contracts)};
}

} // namespace ksj::recon::graph

#include "kspacejet/program/program.hpp"

int main(int argc, char* argv[]) {
  constexpr ksj::program::ProgramDescription kDescription{
    .executable_name = "ksj-gateway",
    .role = "KSpaceJet external-system integration gateway",
    .usage = "ksj-gateway --config <gateway.json> [--format text|json]",
    .commands = "Responsibilities: external-session authentication, connector supervision, routing, and transparent "
                "public MRD/ISMRMRD session forwarding",
  };
  return ksj::program::run_program(argc, argv, kDescription);
}

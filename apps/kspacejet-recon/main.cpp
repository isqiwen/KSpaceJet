#include "kspacejet/program/program.hpp"

int main(int argc, char* argv[]) {
  constexpr ksj::program::ProgramDescription kDescription{
    .executable_name = "ksj-recon",
    .role = "KSpaceJet bounded reconstruction service",
    .usage = "ksj-recon --config <recon.json> [--format text|json]",
    .commands = "Responsibilities: scan admission, execution-plan verification, resource accounting, bounded runtime "
                "scheduling, and Provider execution",
  };
  return ksj::program::run_program(argc, argv, kDescription);
}

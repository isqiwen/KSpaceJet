#include "kspacejet/program/program.hpp"

int main(int argc, char* argv[]) {
  constexpr ksj::program::ProgramDescription kDescription{
    .executable_name = "ksj-research",
    .role = "KSpaceJet reproducible-research runner",
    .usage = "ksj-research <lock|dataset|schedule|case|run|report|claims> [options] [--format text|json]",
    .commands = "Commands: lock verify, dataset freeze, schedule compile, case compile, run, report, and claims audit",
  };
  return ksj::program::run_program(argc, argv, kDescription);
}

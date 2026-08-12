#pragma once

#include <string_view>

namespace ksj::program {

struct ProgramDescription {
  std::string_view executable_name;
  std::string_view role;
  std::string_view usage;
  std::string_view commands;
};

int run_program(int argc, char* argv[], const ProgramDescription& description);

} // namespace ksj::program

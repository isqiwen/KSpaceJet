# DPC repository-level cmake-format configuration.
#
# Keep the core policy explicit so CMake formatting does not depend on the
# formatter's built-in defaults changing between developer machines.

with section("format"):
  line_width = 120
  tab_size = 2
  use_tabchars = False
  fractional_tab_policy = "use-space"

  max_subgroups_hwrap = 2
  max_pargs_hwrap = 6
  max_rows_cmdline = 2
  max_lines_hwrap = 2

  separate_ctrl_name_with_space = False
  separate_fn_name_with_space = False

  dangle_parens = False
  dangle_align = "prefix"
  min_prefix_chars = 4
  max_prefix_chars = 10

  line_ending = "unix"
  command_case = "canonical"
  keyword_case = "unchanged"

  enable_sort = True
  autosort = False
  require_valid_layout = False

with section("markup"):
  enable_markup = True
  first_comment_is_literal = False
  literal_comment_pattern = None
  bullet_char = "*"
  enum_char = "."
  fence_pattern = "^\\s*([`~]{3}[`~]*)(.*)$"
  ruler_pattern = "^\\s*[^\\w\\s]{3}.*[^\\w\\s]{3}$"
  explicit_trailing_pattern = "#<"
  hashruler_min_length = 10
  canonicalize_hashrulers = True

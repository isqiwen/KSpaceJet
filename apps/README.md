# KSpaceJet applications

KSpaceJet has four explicitly named executable projects. Their output names use the
`ksj` prefix; their source directories use the repository-wide `kspacejet-*` form.
When `KSJ_BUILD_APPLICATIONS=ON`, all four targets participate in the default build.
When `KSJ_ENABLE_INSTALL_RULES=ON`, all four executables are installed. The experiment
workloads below `tests/research` remain independently controlled by `KSJ_BUILD_RESEARCH`.

`apps/` contains process entry points only. Every executable declares and parses its
own public command surface with CLI11; reusable operational behavior belongs in its
own owning framework library, not in a second command-line parser target.

| Source directory | CMake target | Executable | Role |
| --- | --- | --- | --- |
| `kspacejet-cli` | `ksj_cli` | `ksj` | Pipeline validation and Provider-scaffold tools. |
| `kspacejet-gateway` | `ksj_gateway` | `ksj-gateway` | Installed application scaffold; operations are unimplemented. |
| `kspacejet-recon` | `ksj_recon` | `ksj-recon` | Offline HDF5 Cartesian/non-Cartesian RSS reference executable. |
| `kspacejet-research` | `ksj_research` | `ksj-research` | Installed research application scaffold; operations are unimplemented. |

`ksj-gateway` and `ksj-research` currently provide only scaffold help/version behavior; a
requested gateway configuration or research operation reports `unimplemented`. They do not
implement external-system integration, a data-plane service, session forwarding, Connector
management, scanner integration, routing, or relay behavior.

The current `ksj-recon` reference routes receive caller-selected HDF5 input together with
explicit Provider modules and OperatorContracts. Provider loading is in-process and is not a
fault-isolation claim.

The default application build creates and installs `ksj`, `ksj-gateway`, `ksj-recon`,
and `ksj-research`. The four executables share the same application build and installation
policy; no executable-specific build option or preset is needed. `KSJ_BUILD_RESEARCH`
remains reserved for `tests/research` and does not control any application executable.

Each executable implements CLI11 `--help`/`-h`, `--version`, and its documented
`--format text|json` behavior. JSON is a machine-readable command-result protocol;
core logging records and log files remain plain text. Operational behavior is added only through
shared framework libraries; applications must not grow independent copies of the planner,
runtime, or Provider ABI.

When an application is asked for `--format json`, its complete structured command
report—success or failure—is written to stdout. Runtime diagnostics use the core
logging facility and are written to stderr, so callers can consume stdout as one
JSON document. Text-mode user diagnostics remain on stderr.

For a new Provider, run `ksj provider init <provider-slug> <operator-id> --output
<directory>`. The command creates the mandatory functional source layout from the
installed template without overwriting an existing directory; it does not register an
unfinished Provider in the catalog.

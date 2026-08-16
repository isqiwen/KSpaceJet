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
| `kspacejet-cli` | `ksj_cli` | `ksj` | User, developer and operations CLI. |
| `kspacejet-gateway` | `ksj_gateway` | `ksj-gateway` | Integration gateway for external systems and site connectors. |
| `kspacejet-recon` | `ksj_recon` | `ksj-recon` | Admission, bounded runtime and Provider execution service. |
| `kspacejet-research` | `ksj_research` | `ksj-research` | Cross-framework experiment runner. |

```mermaid
flowchart LR
    externalSystem["Scanner, PACS or external service"] --> siteConnector["separately deployed site Connector"]
    siteConnector --> gateway["ksj-gateway"]
    gateway -->|"public MRD/ISMRMRD session"| recon["ksj-recon"]
    recon --> runtime["bounded runtime and Provider Operators"]
    runtime --> recon
    recon --> gateway
    gateway --> siteConnector
    siteConnector --> externalSystem
    directClient["conforming MRD client or ksj replay"] --> recon
    cli["ksj"] -.->|"configure, diagnose, replay and operate"| gateway
    cli -.->|"configure, diagnose and operate"| recon
    research["ksj-research"] -.->|"external experiment orchestration only"| cli
```

`ksj-gateway` does not define or translate a KSpaceJet-private raw-data protocol. It
accepts or forwards only the selected public MRD/ISMRMRD streaming-session binding;
site-specific proprietary adaptation must remain in a separately deployed connector.
`ksj-recon` alone owns scan admission, the resource ledger, backpressure, pipeline
execution and Provider scheduling. The gateway never chooses an algorithm or directly
owns Provider buffers.

Each reconstruction extension is a dynamically loaded Provider library (a shared
library on Linux or DLL on Windows), not a separate KSpaceJet executable. The reconstruction service
loads compatible trusted Providers in-process through the Provider ABI.

The default application build creates and installs `ksj`, `ksj-gateway`, `ksj-recon`,
and `ksj-research`. The four executables share the same application build and installation
policy; no executable-specific build option or preset is needed. `KSJ_BUILD_RESEARCH`
remains reserved for `tests/research` and does not control any application executable.

Each executable implements CLI11 `--help`/`-h`, `--version`, and its documented
`--format text|json` behavior. JSON is a machine-readable command-result protocol;
core logging records and log files remain plain text. Operational behavior is added
only through shared framework libraries; applications must not grow independent
copies of the planner, runtime, Provider ABI, or MRD data plane.

When an application is asked for `--format json`, its complete structured command
report—success or failure—is written to stdout. Runtime diagnostics use the core
logging facility and are written to stderr, so callers can consume stdout as one
JSON document. Text-mode user diagnostics remain on stderr.

For a new Provider, run `ksj provider init <provider-slug> <operator-id> --output
<directory>`. The command creates the mandatory functional source layout from the
installed template without overwriting an existing directory; it does not register an
unfinished Provider in the catalog.

# `ksj`

`ksj` is the single public command-line entry point. It is not an online data
proxy and must not implement a second reconstruction runtime. Offline execution,
inspection, replay, Provider authoring, diagnostics, and service operations are
subcommands that reuse the same schemas and libraries as `ksj-gateway` and
`ksj-recon`.

The initial skeleton provides stable `--help`, `--version`, and
`--format text|json` behavior. Future commands are added only after their shared
runtime contracts and JSON schemas are defined.

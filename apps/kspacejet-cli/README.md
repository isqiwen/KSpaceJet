# `ksj`

`ksj` is the single public command-line entry point. It is not an online data
proxy and must not implement a second reconstruction runtime. Commands use
CLI11 subcommands and options; use `ksj --help` or `<command> --help` to inspect
the accepted syntax.

## Pipeline validation

```bash
ksj pipeline validate path/to/pipeline.json --format text
ksj pipeline validate path/to/pipeline.json --format json
```

The command is a thin product entry point over the shared `PipelineDefinition`
parser. It neither loads Providers nor schedules reconstruction work. Text is
for people; JSON is a machine-readable command-result protocol for automation.
The successful report identifies the one ISMRMRD HDF5 input profile, canonical
PipelineDefinition digest, and declared parameter/graph counts. Neither form is
a diagnostic log format.

The reported `input_profile.container` is authored selection intent, not a
result of opening a scan. It is either `{"mode":"auto"}` or
`{"mode":"explicit","path":"/absolute-hdf5-container"}`. The parser
accepts only that closed form; it does not search for raw data. P2-007's
runtime-owned source adapter will discover standard raw-container candidates:
`auto` requires exactly one candidate, while `explicit` must resolve to the
named standard raw container. Neither mode gives `/dataset` special status.

## Create a Provider scaffold

```bash
ksj provider init example-filter image_filter --output providers
```

This materializes `providers/kspacejet-example-filter` from the installed
Provider template. The Provider slug must be lowercase hyphenated text without
the `kspacejet-` prefix; the Operator id must be a lowercase underscore-separated
identifier. The command creates a complete staging directory first and publishes
it only when `kspacejet-example-filter` does not already exist. It never accepts
a force/overwrite mode.

The generated `contracts/image_filter.json` deliberately retains
`@INPUT_PORT@`, `@INPUT_TYPE_REF@`, `@OUTPUT_PORT@`, and `@OUTPUT_TYPE_REF@`.
Replace them with the exact typed interface before adding the Provider to the
catalog and build.

# `ksj-recon`

`ksj-recon` is the only process that performs reconstruction. It owns scan admission,
`ExecutionPlan` and certificate verification, the framework-managed resource ledger,
bounded dataflow scheduling, Provider lifecycle, and standard image production.

It receives only standard ISMRMRD HDF5 through the offline path or the selected public
MRD/ISMRMRD streaming-session binding through the online path. It does not load
site-specific scanner, PACS, or workflow integrations; those belong to `ksj-gateway` and
external connectors.

The executable skeleton deliberately contains no copied legacy reconstruction-service behavior. New
service behavior is introduced through the shared runtime libraries and their validated
contracts.

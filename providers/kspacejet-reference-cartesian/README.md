# kspacejet-reference-cartesian

`ksj-reference-cartesian` is the deliberately small, open Provider ABI v1
reference plugin.  It is a bounded Cartesian lifecycle/conformance operator:
it accepts at most 64 input batches and 256 input items per firing, consumes
their host-declared counts, uses no scratch/retention/async work/private
threads, and emits no MRI data.

This is not a diagnostic or clinical reconstruction algorithm.  Its role is
to give the loader and runtime an independently loadable reference Provider
whose lifecycle is easy to audit before open image-producing reference
operators are added.

Its only normal terminal path is `on_scan_end`, which returns a zero-output
`Done` result.  Its cancellation/failure path is `on_cancel`; the ABI gives it
no firing lease or output grant, and the implementation performs only a
bounded no-op cleanup.  It never opens files, creates network connections, or
starts private threads.

The plugin root is platform-independent, with the platform-native filename:

```text
Linux:   <prefix>/lib/kspacejet/providers/libksj-reference-cartesian.so
Windows: <prefix>/lib/kspacejet/providers/ksj-reference-cartesian.dll
```

On Windows the sibling `.lib` is the compiler import library produced for the
DLL; it is not a static Provider payload.  A caller still loads the `.dll`
through the explicit trusted-path Provider loader.

The descriptor's current `bundle_digest` is a stable development fixture
identity for ABI/load tests.  It is not yet a content-addressed package
manifest; production registration must replace it with the frozen bundle
manifest and trust-policy flow described in the architecture documents.

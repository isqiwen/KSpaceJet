# `ksj-viewer`

`ksj-viewer` is KSpaceJet's local, read-only Qt Widgets inspection application.
It is standard-ISMRMRD-first: a normal `.mrd`, `.h5`, `.hdf5`, or `.ismrmrd`
file is opened in place. The viewer never converts it to a KSpaceJet-private
format and never requires a group named `/dataset`.

## Standard MRD containers

Opening a file performs a bounded recursive HDF5 discovery pass. It reports
absolute container paths and classifies standard content as `[RAW]`, `[IMAGE]`,
`[WAVEFORM]`, or `[HEADER]`:

- An XML-header container can expose standard metadata, acquisitions/k-space,
  waveforms, and named image series at any HDF5 path.
- A standalone standard image-series container with direct `header`,
  `attributes`, and `data` datasets can be selected and viewed even when it
  has no XML-header or raw-acquisition owner.
- A standard image artifact containing XML plus image series but no raw
  acquisitions is also directly viewable. This is the normal shape of a
  result-only ISMRMRD artifact.

The navigation tree chooses a discovered container rather than asking the user
to type a group name. Opening an MRD initially shows that semantic tree and
the HDFView-style **Object Attribute Info** / **General Object Info**
inspector; its typed-data area stays hidden. Tree selection is non-destructive:
it updates the inspector only. `Inspect`, `Open As…`, a double click, or the
semantic-object context menu explicitly activates a container and opens a
typed view; moving to another container clears k-space and image display
derivatives. The Pipeline view is independent and remains parse-only.

## Desktop workflow

The native C++/Qt shell follows the useful desktop workflow of HDFView without
copying or linking its Java/SWT implementation:

1. Use **File** or `Ctrl+O` to open a local standard MRD file. The compact
   current-file bar also accepts a local path on Enter and holds files opened
   in the current session. URLs are rejected. The initial workspace is the
   semantic tree plus the selected object's inspector, not a file-level
   dashboard.
2. Inspect the left **ISMRMRD File Hierarchy**. It contains only recursively
   verified standard ISMRMRD semantic objects, not a generic HDF5 traversal.
3. Select an object to see its read-only **General Object Info** page. It
   follows HDFView's compact object form: Name, Path, Type, and Access, then
   standard dataset semantics and standard ISMRMRD member tables. **Object
   Attribute Info** is a table of standard image `MetaAttributes` after an
   image is explicitly inspected; it is not a generic HDF5 attribute browser.
   The bounded XML header preview belongs to the dedicated **XML** typed view:
   select the semantic Header/XML object and explicitly inspect it, rather than
   expecting a file-level metadata dashboard.
4. Use **Inspect**, **Open As…**, a double click, or the context menu to open
   an XML header, an acquisition header table/k-space projection, an `image_x`
   image view, or a PipelineDefinition view. The File/Window/Tools/Help menu
   structure, toolbar, info panel, flat split-pane layout, and dense tables
   follow the same hierarchy → inspector → typed-view pattern.

The bottom **Info** panel and status bar record open, close, inspection, and
export actions. The interface has no generic object editor, source-file save,
URL loader, or non-MRD format support.

Formal reconstruction output uses only standard ISMRMRD image series such as
`image_0`, `image_1`, and so on; it never needs `/ksj_recon`. The authored
PipelineDefinition remains a separate required JSON input. If a future product
case truly needs to embed pipeline material in an explicitly derived file, it
may reserve one optional `/ksj_pipeline` extension, but that extension is never
a prerequisite for opening, viewing, or reconstructing a standard ISMRMRD
file. The viewer never changes the source file.

## Read-only views and exports

- **XML** shows a bounded header preview only after the selected Header/XML
  object is explicitly inspected. It does not show a file-level overview or an
  empty image-series table.
- Image series belong to the semantic **Images** object and its Image typed
  view. A raw-acquisition source normally has zero reconstructed image series;
  that is expected data state, not missing viewer content.
- K-space displays a bounded acquisition-magnitude projection, explicitly
  labelled as *not a reconstructed image*, alongside a header-only acquisition
  table.
- Image inspection reads one selected `[z, channel]` plane and creates a
  bounded grayscale display derivative. It offers ordinal cine and auto/manual
  window-level, zoom, pixel probe, and histogram; each update rereads the
  selected standard image and retains only the bounded derivative.
- Waveform storage is discovered and labelled, but waveform sample inspection
  is not implemented yet.
- **File → Open pipeline** parses a public `PipelineDefinition` and displays
  canonical authored JSON. It never resolves, loads, compiles, or executes a
  Provider.
- PNG, SVG, CSV, and JSON exports are marked `visualization-derivative`; they
  are local display data, not MRI artifacts. The exporter rejects `.mrd`,
  `.h5`, `.hdf5`, and `.ismrmrd` destinations so a display derivative cannot
  be mistaken for an MRI artifact.

The viewer never reconstructs, loads a Provider, connects a gateway, or
becomes a runtime/data-plane dependency.

See the [ISMRMRD inspection reader contract](../../docs/architecture/KSpaceJet_ismrmrd_inspection_reader.md)
for the ownership model, bounded discovery, image axes, and metadata rules.

On Windows, the target uses Qt's `windeployqt` to deploy the dynamic platform
plugin required by the Qt application. Verify that the installed executable
takes the real Qt path without user input:

```powershell
ksj-viewer --ui-smoke --format json
ksj-viewer --export-smoke --format json
```

`--export-smoke` creates, atomically publishes, and reads back a temporary PNG
display derivative. It is used by the Windows install smoke to verify that the
deployed Qt runtime can write PNG even though optional `imageformats` plugins
are not installed.

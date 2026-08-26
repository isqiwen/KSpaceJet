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
to type a group name. Opening an MRD initially shows that semantic tree and a
single HDFView-style primary inspector: **Object Attribute Info** and
**General Object Info** are always present, while contextual **K-space**,
**XML**, and **Image** tabs appear only for the selected, active standard MRD
object. **Pipeline** is a peer tab only after a `PipelineDefinition` has been
opened successfully; there is no empty Pipeline placeholder or a second,
lower typed-data tab bar. Tree selection remains non-destructive with respect
to raw payloads: it updates object information and applicable tabs only.
Selecting a contextual tab, `Inspect`, `Open As…`, a double click, or the
semantic-object context menu explicitly activates its bounded inspection
surface; rendering K-space or image pixels still requires its explicit
bounded inspection action. Moving to another container clears K-space and
image display derivatives. The Pipeline view is independent and remains
parse-only.

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
   A container with no reconstructed image series or standard waveform storage
   has no corresponding **Images** or **Waveforms** tree entry: unavailable
   content is omitted instead of being shown as a disabled `(none)` action.
   The separate JSON source appears as a root-level **Pipeline** entry only
   after **File → Open pipeline** has successfully parsed a
   `PipelineDefinition`; it is not an empty MRD dataset placeholder.
3. Select an object to see its read-only **General Object Info** page. It
   follows HDFView's compact object form: Name, Path, Type, and Access, then
   standard dataset semantics and standard ISMRMRD member tables. The compact
   semantic table derives its height from the actual styled Qt rows and table
   frame, so its content is not clipped; any outer overflow belongs to the
   General Object Info scroll surface. **Object
   Attribute Info** separately shows the actual native HDF5 Attributes attached
   to the selected concrete standard-MRD object. Names and type/shape remain
   visible under a bounded read-only policy; oversized values are explicitly
   shown as omitted previews, and an object with no HDF5 Attributes says so
   explicitly. The semantic **Images** collection is not itself one HDF5
   object, and standard image `MetaAttributes` belong only in Image details,
   not in this tab. This is still not an arbitrary-path HDF5 attribute browser
   or editor. The bounded XML header preview belongs to the contextual **XML**
   tab: select the semantic Header/XML object and explicitly inspect it,
   rather than expecting a file-level metadata dashboard.
4. **K-space** follows General Object Info in the same primary tab group;
   **XML**, **Image**, and a successfully opened **Pipeline** are its peer
   contextual tabs. No second lower tab strip duplicates these pages. Use
   **Inspect**, **Open As…**, a double click, the context menu, or an
   applicable contextual tab to open an XML header, a Cartesian K-space view
   with its reference-acquisition header, an `image_x` image view, or a
   PipelineDefinition view. The File/Tools/Help menus, toolbar, info panel,
   flat split-pane layout, and dense tables follow the same hierarchy →
   inspector → bounded data-view pattern.

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
- **K-space** is a bounded raw Cartesian K-space viewer, not a single-readout
  `sample × channel` projection. Its reference-acquisition header remains
  visible alongside the rendered plane.
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

### Cartesian K-space Viewer

The K-space view needs a standard ISMRMRD XML encoding and a selected
reference acquisition. The reference is a frame selector, not the only raw
acquisition displayed. The viewer first indexes headers without materializing
their payloads, then groups raw acquisitions whose frame key exactly matches
the reference:

- `encoding_space_ref` and `idx.kspace_encode_step_2`;
- `idx.average`, `idx.slice`, `idx.contrast`, `idx.phase`, `idx.repetition`,
  `idx.set`, and `idx.segment`; and
- all eight `idx.user` counters.

`idx.kspace_encode_step_1` is deliberately not part of that key: it is the
vertical K-space axis. The horizontal coordinate of every included source
sample is `sample - center_sample`; samples in `discard_pre` and `discard_post`
are excluded. The vertical range comes from the selected encoding's standard
`kspace_encode_step_1` limit when it is declared, otherwise from the matching
raw acquisitions. The selected `kspace_encode_step_2` and all other frame
counters remain fixed, so one view is one explicit raw Cartesian plane.

The coil selector offers **RSS** across all active coils or a single active
coil. RSS sums the squared complex magnitude across active coils for each raw
sample. Each displayed cell is then the RMS of all of its raw contributions;
this applies both to repeated raw grid cells and to source coordinates grouped
into one display cell during bounded downsampling. No later acquisition can
silently overwrite an earlier one. Empty cells retain magnitude zero, and the
plane summary and JSON export report occupied, empty, and
multi-contribution-cell counts.

Display intensity is `log10(1 + RMS magnitude)` and is normalized only for the
grayscale visualization. It contains no reconstructed image, inverse Fourier
transform, phase image, or new MRD artifact. CSV rows describe *display bins*,
including their represented readout and `kspace_encode_step_1` coordinate
ranges plus contribution counts; CSV output is bounded just like the display.

The current view accepts at most 16,384 matching acquisition lines and 32 Mi
complex source values, and creates no more than 2,048 pixels along either axis
or 2 Mi display cells. It retains only the resulting display derivative and
owned header records, never a raw-acquisition cache. A limit, malformed shape,
or non-finite value fails the render rather than silently truncating source
data.

This is intentionally a Cartesian-only view. It rejects an acquisition with
trajectory samples, a non-Cartesian XML encoding, or matching records with
inconsistent Cartesian geometry. It does not grid radial, spiral, or other
trajectory-space data into a fabricated Cartesian matrix; a future
trajectory-space viewer must be an explicit separate mode.

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

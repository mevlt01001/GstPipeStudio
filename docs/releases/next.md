## Templates

- Save a selection as a template: its border is traced around it from one corner along both arms (and erased the same way when it is cancelled, detached or deleted), then give it a name and Add parameter (an element, then one of its properties or a field of its fixed caps).
- Add template opens the library in the right drawer; hover a template, or a collapsed one on the canvas, for a peek inside: a small canvas of its real pipeline, laid out like the canvas, branches and all.
- A collapsed template is one element whose Properties are its parameters. + expands it into a green frame with its pads on the border; − (or five idle seconds) collapses it. Templates nest.
- A project's conf imports a template like a module: one section with its name, revision and parameter values, instead of its elements. The project folder keeps a copy of each template it uses under templates/, so it runs on another machine with gps-run or gstgpsconf; elsewhere GPS_TEMPLATE_PATH lists more template folders.
- Edit template, from a template's right click on the canvas, a template in the Explorer, or the library (right click a template in Add template), opens it in a tab of its own as "Template: <name>". There Pipeline > Template parameters… chooses its parameters and Save writes its next revision, which the other tabs offer to their copies. The library menu also has Rename…, Export… and Delete…; Import… adds a template file.
- Changing a copy asks: Update template, Save as new template, Detach or Undo. An update reaches the other copies at once and other projects when they open.
- Add source, Save custom, Combine into group and the Expose checkbox are gone: Add element lists every factory, and parameters belong to templates.

## Layout

- Nice alignment (ELK in QuickJS, on its own thread) places the canvas when a file opens, when a template expands or collapses, and on Arrange pipeline or Auto align. Links avoid boxes and crossings, and start and end on their pads in both link styles.
- Nothing on screen jumps: a layout keeps the centre of what it was asked about. A template opens out of its centre and closes back into it, and the boxes around it slide aside.
- View > Auto alignment: every edit is followed by a nice alignment, and a dragged box slides back into place. Off, only opening or closing a template and adding one move other boxes.
- View > Link style: curves or right-angle lines. A conf no longer stores positions.

## Projects and history

- File > New project… asks a name and makes its folder under ~/workspace/GstPipeStudio (Change… picks another place, remembered). Save of a canvas never saved does the same, so Ctrl+S always lands in a project folder; Save as… still chooses any file.
- A saved conf's folder gets a .gpsproj listing its confs and the templates they use. File > Version history restores one of the last 50 saves.
- User folders follow the XDG base directories; the template library moves from custom_elements once.

## Pipeline-wide live metrics

- Compare Avg. Processing Time and QoS-reported Drop Rate across all elements on the canvas and in live charts.
- Age is visible on every canvas element, including sources: pipeline running time minus output buffer running time derived from PTS. Missing timing information stays unknown.
- Inspect pending time, buffer rates, timestamp age and queue occupancy.
- Try three hardware-free monitoring examples, including a deliberately slow pipeline with reported QoS drops.

## Property editing

- A calmer Properties panel: one line per property, a changed value marked in green with a small ↺ that restores the default, and Reset all beside the search. Save applies the changes; Cancel discards them.
- The mouse wheel never changes a value: it scrolls the form. Values are typed or chosen.

## Release workflow

- Manual releases now require a maintainer-written Markdown summary.
- GitHub appends generated pull-request notes, grouped into features, fixes and other changes.

## Measurement limits

Processing time measures first sink arrival to first src departure, including waiting. Drop Rate requires buffer-unit QoS counters; missing reports appear as unknown. QoS does not identify the exact internal processing stage where a buffer was dropped. NVIDIA/DeepStream hardware validation remains outstanding.

## Desktop experience

- Menus in the usual order: File (New, Open, Open recent, Save, Save as…), Edit (Undo, Redo, Cut, Copy, Paste, Duplicate, Delete, Select all, Find), View, Pipeline, Help.
- Log, Validation, Live metrics, Properties and Caps are dock panels: move, tab, split, float or close them; View > Panels brings one back, Reset layout restores the row under the canvas, and the window remembers the layout.
- Every open file is a tab over the canvas, as in code editors: ● marks unsaved changes (× on hover closes), a file already open comes to the front, closing an unsaved tab asks Save / Don't save / Cancel, middle click or Ctrl+W closes, Ctrl+Tab and Ctrl+PgDn/PgUp switch. The panels and menus follow the tab in front, and the open tabs come back on the next start.
- The Explorer, left of the canvas: the open project's files, every project in the projects folder and the template library. Double click a project or a .conf to open it in a tab, a template to edit it.
- Validation follows what runs: adding or changing an element no source feeds keeps the last result (Save stays available); a change to the fed part is validated again.
- Unified menu and window controls, animated About dialog, and visible version/build identity.
- Development and release builds use separate settings; support reports include the real build version and commit.
- Larger pipeline file picker, slightly lighter canvas, animated pad feedback and retracting connection previews.
- Optional connected-component dragging with one undoable position update.
- Shorter, visual README and a concise contributor ownership map with shared formatting rules.

## Templates

- Save a selection as a template: its border is traced around it from one corner along both arms (and erased the same way when it is cancelled, detached or deleted), then give it a name and Add parameter (an element, then one of its properties or a field of its fixed caps).
- Add template opens the library in the right drawer; hover a template, or a collapsed one on the canvas, for a peek inside: a small canvas of its real pipeline, laid out like the canvas, branches and all.
- A collapsed template is one element whose Properties are its parameters. + expands it into a green frame with its pads on the border; − (or five idle seconds) collapses it. Templates nest.
- A project's conf imports a template like a module: one section with its name, revision and parameter values, instead of its elements. The project folder keeps a copy of each template it uses under templates/, so it runs on another machine with gps-run or gstgpsconf; elsewhere GPS_TEMPLATE_PATH lists more template folders.
- Edit template, from a template's right click on the canvas, a template in the Explorer, or the library (right click a template in Add template), opens it in a tab of its own as "Template: <name>". There Pipeline > Template parameters… chooses its parameters and Save writes its next revision, which the other tabs offer to their copies. The library menu also has Rename…, Export… and Delete…; Import… adds a template file.
- Changing a copy asks: Update template, Save as new template, Detach or Undo. An update reaches the other copies at once and other projects when they open.
- One file per template: a name whose file another template already has ("a b" and "a_b") is refused, and a template an earlier version saved under another file name can still be renamed and deleted. A newer revision is offered to a copy once.
- Add after a free pad, then a template: the copy now lands linked to that pad, through its first free sink pad (or a new Request pad). Dropping a wire on a box, Connect selected and dragging onto a link use the same rule, and a box takes a wire on a Request sink too.
- A copy shows the current name of its template, also right after a rename.
- Add source, Save custom, Combine into group and the Expose checkbox are gone: Add element lists every factory, and parameters belong to templates.

## Layout

- Nice alignment (ELK in QuickJS, on its own thread) places the canvas when a file opens, when a template expands or collapses, and on Arrange pipeline or Auto align. Links avoid boxes and crossings, and start and end on their pads in both link styles.
- Nothing on screen jumps: a layout keeps the centre of what it was asked about. A template opens out of its centre and closes back into it, and the boxes around it slide aside.
- View > Auto alignment: every edit is followed by a nice alignment, and a dragged box slides back into place. Off, only opening or closing a template and adding one move other boxes.
- View > Link style: curves or right-angle lines. A conf no longer stores positions.

## Projects and history

- File > New project… asks a name; its folder under ~/workspace/GstPipeStudio (Change… picks another place, remembered) is made by its first save. Save of a canvas never saved asks the same, so Ctrl+S always lands in a project folder; Save as… still chooses any file.
- Save always writes, validated or not: validation gates Run and Monitor only. A file saved before it was validated is marked incomplete.
- A tab is modified when it differs from its last save: undo back to it clears the ●, moving boxes does not set it, and opening a file never does. New tabs are Untitled-1, Untitled-2…
- File > Save all (Ctrl+Alt+S) saves every unsaved tab; File > Revert file reads the file again.
- Hot exit: about a second after a change, every unsaved tab (Untitled ones too) is backed up. Quitting asks nothing; the next start reopens the tabs with their unsaved changes, and so does a start after a crash. Closing a single tab still asks.
- A file changed on disk by another program is read again when its tab has no unsaved changes (the status bar says so); a tab with unsaved changes asks Reload or Keep mine (a tab in the background asks when it comes to the front), and Save over the changed file asks Overwrite or Cancel. A deleted file keeps its tab, marked deleted and unsaved.
- Files are written whole or not at all (a temporary file beside it, then renamed).
- A saved conf's folder gets a .gpsproj listing its confs and the templates they use. File > Version history restores one of the last 50 saves as one undoable change; a library template's restored version stays that template.
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

- Menus in the usual order: File (New, Open, Open recent, Save, Save as…, Save all, Revert file), Edit (Undo, Redo, Cut, Copy, Paste, Duplicate, Delete, Select all, Find), View, Pipeline, Help.
- Log, Validation, Live metrics, Properties and Caps are dock panels: move, tab, split, float or close them; View > Panels brings one back, Reset layout restores the row under the canvas, and the window remembers the layout.
- Every open file is a tab over the canvas, as in code editors: ● marks unsaved changes (× on hover closes), a file already open comes to the front, closing an unsaved tab asks Save / Don't save / Cancel, middle click or Ctrl+W closes, Ctrl+Tab and Ctrl+PgDn/PgUp switch. The panels and menus follow the tab in front, and the open tabs come back on the next start.
- The Explorer, left of the canvas: the open project's files, every project in the projects folder, Templates and Plugins (empty for now: custom elements will appear there). Double click (or Enter) a project or a .conf to open it in a tab, a template to edit it.
- The Explorer follows the tab in front: its file is selected, open files are bold, unsaved ones carry a ●, a file deleted under its tab is struck through, and the panel's title counts the unsaved tabs. A project using older template revisions than the library is drawn in orange, and a conf its project lists but that is gone is shown muted.
- F2 renames a file or template in place, checking the name as you type; a renamed conf keeps its tab, its project manifest entry and its version history. Del moves a file, folder or project to the trash after asking (and warns when it is open with unsaved changes). Right click offers Open, Rename, Delete, Reveal in file manager, Copy path and New project; on a template Edit, Rename, Export and Delete; Import and Refresh on the section headers.
- A template renamed while its tab is open keeps the tab, which saves under the new name; every open tab's copies follow the new name.
- A template tab whose template got a newer revision from another tab says so in those words instead of "changed by another program": a clean tab reads it, a modified one asks Reload or Keep mine. Deleted templates go to the trash.
- A new tab opens at once: every tab shares one GStreamer check, one element list and one template library, so a template saved in one tab is offered to the copies in all the others. The View menu's link style, auto alignment and connected move apply to every tab.
- Validation follows what runs: adding or changing an element no source feeds keeps the last result; a change to the fed part is validated again.
- Unified menu and window controls, animated About dialog, and visible version/build identity.
- Development and release builds use separate settings; support reports include the real build version and commit.
- Larger pipeline file picker, slightly lighter canvas, animated pad feedback and retracting connection previews.
- Optional connected-component dragging with one undoable position update.
- Shorter, visual README and a concise contributor ownership map with shared formatting rules.

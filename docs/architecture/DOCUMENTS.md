# Documents, saving and the Explorer

The target architecture of the application layer. It follows the editors the users already know (VS Code, JetBrains, Qt Creator) and one rule: **every concept has one owner and one interface, and every caller uses it**. There is one save path, one notion of dirty, one writer per folder, one provider interface for what can be placed, and one name table.

## Decided behaviour (user, 2026-11-25)

| Question | Answer |
|---|---|
| Save and validation | **Save always writes.** Validation gates only Run, Monitor and export. A file saved before it was validated is marked `complete=false`, as today. |
| Unsaved work | **VS Code hot exit.** About 1 s after a change, every modified tab (untitled ones too) is backed up to the state folder. Quitting does not ask; the next start restores the tabs with their unsaved changes. Closing a single tab still asks Save / Don't save / Cancel. A crash loses at most about 1 s of work. |
| Untitled tabs | Named `Untitled-N` with the lowest free N. They live in memory (and in their backup) until the first Save, which asks for a project (New project dialog). |
| Run | **Runs a temporary copy** of the canvas as it is. The file is written only by Save. |
| Plugins | The Explorer shows a **Plugins** section now, empty with a hint. Templates and plugins use one provider interface. |
| Preview tabs | No. A double click opens a tab; the same file opens once. |
| Auto save | Off. Backups cover crashes. |

## Application services (one per process)

`AppServices` is built once in `main.cpp`. Every `Controller` (one per tab) and the window receive it.

| Service | Owns | Replaces |
|---|---|---|
| `EnvironmentService` | the GStreamer probe, run once and on Retry; starts the catalog load when ready | a `StartupCheck` per controller |
| `FactoryCatalog` (shared) | element list and infos | a catalog (and a gps-inspect process) per tab |
| `TemplateLibrary` (shared QObject) | the only reader and writer of the library folder; name validation (`acceptName`), revisions, rename, delete, import, export; emits `changed()` | 7 controller functions and the `libraryChanged` relay between tabs |
| `ViewPreferences` | link routing, auto alignment, connected move; `changed()` | loops over every tab in the View menu |
| `Settings` | every QSettings key, typed | keys spelled in 6 files |
| `BackupService` | hot-exit backups | — |

A new tab costs one `Controller`, its canvas and its panel pages: no probe and no helper process.

## Persistence

### One writer

`gps::files` (Pipeline, pure C++): `writeAtomic(path, bytes)` (temporary file in the same folder, flush, rename) and `safeName(name)`, the only sanitizer. Every file the application writes (conf, project manifest, registry, library, history copy, backup) goes through it.

### One save target per document

A document knows **what it is saved to** through a `SaveTarget`. This replaces the `path_` + `libraryTemplate_` string pair and every `if` on it.

```cpp
class SaveTarget {
public:
    virtual ~SaveTarget() = default;
    virtual QString title() const = 0;            // tab text: "door.conf", "Template: Tail", "Untitled-2"
    virtual QString path() const = 0;             // "" while untitled
    virtual QString sessionKey() const = 0;       // what the session remembers: "file:…", "template:…", "untitled:N"
    virtual bool needsDestination() const = 0;    // untitled: Save asks where first
    virtual SaveResult write(const gps::Graph&) = 0;  // its own side effects: history, templates/, manifest, registry
};
// UntitledTarget(n), ProjectConfTarget(confPath), LibraryTemplateTarget(name, TemplateLibrary&). Later: PluginTarget.
```

Loading another version or reverting replaces the content only; the target stays.

### One dirty rule

A document is modified when the editor's undo index differs from the index at the last save or load (`markSaved()`). Undo back to the saved state clears the dot by itself. Loading never makes a document modified. `hasUnsavedWork() = modified() && !empty()` is the only predicate the UI reads.

### One save flow

`SaveFlow` (UI) is the only place that saves on behalf of the user and the only place that shows a save error or asks about unsaved changes:

- `save(doc)`: an untitled target asks for a project (New project dialog) first; then `target->write`.
- `saveAs(doc)`: a project conf elsewhere; the tab is re-bound to the new target. Off for a target that is not a project conf (`saveAsBlockedReason`).
- `saveAll()`: every modified tab, in tab order; stops at the first Cancel. A tab that cannot be saved now (it runs) is passed over and named.
- `confirmClose(docs)`: one tab: "Do you want to save the changes you made to X?" (Save / Don't save / Cancel). Several: one dialog listing them (Save all / Don't save / Cancel). Save stops a tab's run and monitor first, as closing does.

### Hot exit

`BackupService` writes `<state>/backups/<id>.conf` (+ a small `.json` with the target's session key and title) about 1 s after a modified tab changes, deletes it when the tab is clean or closed, and on start restores the tabs (modified) from the backups. A backup the saved session names is never dropped: its tab comes back, and a tab that could not load it (GStreamer not ready, say) keeps naming it. Any other backup is counted at every start that offers it and its tab writing it again resets the count: one no tab took up in three starts (it cannot be loaded) is dropped. Quitting the window saves the session and does not ask. The `.json` also keeps the hash of the file content the tab's changes were based on, so a file changed while the application was closed is said to its reopened tab, and the application that writes it: another one running at once holds a lock in the folder, and its backups are not reopened, counted or dropped.

### Files changed on disk

One `QFileSystemWatcher` over the open files (the document registry):

- A clean tab reloads silently, with a status-bar note.
- A modified tab is not reloaded: it asks Reload / Keep mine.
- Saving over a file that changed on disk since it was read asks Overwrite / Cancel.
- A deleted file keeps its tab, marked deleted and modified; back with the content it had, its save point is back too.
- A library template's file written by the library for another tab (a newer revision) is not another program's edit: the `TemplateLibrary::changed()` path catches the registry up and says "has a newer revision, saved from another tab" (a clean tab reloads, a modified one asks Reload / Keep mine).

## Model: one definition per concept

| Concept | One definition | Replaces |
|---|---|---|
| a pad's address | `gps::PadRef` (element, identity, direction, `key()`, `fromKey()`, `linkName()`); `Graph::srcOf/sinkOf(link)` | `element|pad|src` strings in 3–4 files, `PadAddress` |
| a link's address | `gps::LinkRef::id()` | `linkId` copies |
| a node (element, template copy, later a plugin node) | `gps::NodeRef` + `gps::NodeShape` (members, external and free pads, Request sinks, role, `firstFree`) computed from factory definitions and links, so drafts work too; the pad rule itself is one function, `gps::padsOf` | free-pad and is-source questions answered three ways; "Add after → template" never linking |
| what is drawn as one box | `Outline` (box of an element, members and anchor of a box, frames around it, visible frames, selection expansion), kept per revision and expanded set | box/member questions in Controller, CanvasLayout and CanvasScene |
| every human name | `gps::Names`, kept per revision: element, pad (its label read from the graph's pads, which the last run names), template copy (from `templateName`, never parsed from its id), parameter | display names rebuilt per element; 4 texts for one pad |
| what can be placed | `ComponentProvider` (kind, title, empty hint, components with ports and role, peek, place on a graph, actions); `TemplateLibrary` implements it, `PluginLibrary` is the empty Plugins section; `Controller::place/placeAfter` place any `ComponentRef` | picker and Explorer hard-coding templates |

## Explorer

One tree, built from **sections** that implement the same `ExplorerSource` interface (label, entries, folders to watch, actions, empty-state hint, Refresh):

1. **Project** (`ProjectSection`): the front tab's project folder: confs, the vendored `templates/`, other files, and the confs its manifest lists (or a tab has open) that are not on disk.
2. **Projects** (`ProjectsSection`): the folders of the projects location and the registry, merged once.
3. **Templates** (`ComponentSection` over the template provider).
4. **Plugins** (`ComponentSection` over the plugin provider): empty for now, with the hint "Custom elements will appear here".

Every row is an `ExplorerEntry {kind, origin, path/name, state}` with `state ∈ {Ok, Open, Unsaved, Outdated, Missing, Deleted}`. The sections give Ok, Outdated (the manifest holds older template revisions than the library) and Missing; the window's `EditorState` (front tab, its project, every tab's state by session key) lays Open, Unsaved and Deleted over them. `ExplorerEntry::key()` is the session key of the tab an entry opens in. Icon, decoration and tooltip are chosen from the entry in one place (`ExplorerLook`); the menu is the section's `actions(entry)`.

Behaviour, as in IDEs:

- It follows the editor: the front tab's file is selected and revealed. Open files are bold, unsaved files carry a dot, a file deleted under its tab is struck through, and the dock title shows the unsaved count ("Explorer · 2 unsaved").
- Double click or Enter opens (a file already open comes to the front). F2 renames inline with live validation (Enter does not take a refused name). Del moves to the trash (`QFile::moveToTrash`) after a confirm, with a warning when the file is open and unsaved.
- Context menus: a conf: Open, Rename, Delete, Reveal in file manager, Copy path; another file: Rename, Delete, Reveal, Copy path; a folder: Delete, Reveal, Copy path; a project: Open, Delete, Reveal, Copy path, New project; a template: Edit, Rename, Export, Delete. Section headers: New project (project sections), Import (Templates), Refresh. Folders and projects are not renamed from here.
- File operations go through one `FileOperations` service, used by the Explorer, the picker's component menu and File > New project. Renaming a conf moves its manifest entry and version history and re-binds its tab's `SaveTarget`; deleting one drops its manifest entry. A provider says `renamed(from, to)` whoever renamed: the tab editing that component is re-bound, and every tab's copies follow.
- The watcher is debounced (150 ms); a section whose entries did not change is not rebuilt, so expansion and selection stay. Each section also has Refresh.

## Window

- `DocumentTabs`: the tab bar, the page stack, the document list in tab order, `find(sessionKey)` (a file or a template, also while it opens: `Controller::intendedTarget`), `target()` (a pristine tab is reused), `open(target, backup)`, close (with its backup, run and monitor), session save and restore. `MainWindow` keeps one `Document* front()`; every part it shows is the front document's.
- `CommandSet`: every action once, in one table `{id, name, text, keys, run, off}` where `off()` says why the command is off ("" when it is on) and the tooltip shows it; labels that change (Run / Stop, a template's name) and check marks are rules too. `refresh()` is one loop over the table. Context menus (canvas, element, selection, template frame, link, tab) are assembled from the same actions by `ContextMenus`; a command about one element, link or copy acts on the menu's target.
- The window refreshes its commands, the Explorer, the status bar and the title once per turn of the event loop, however many changes asked for it. A document locks its own canvas and pages while it cannot be edited.
- One `Prompt` helper for questions, confirms, errors and name inputs (SaveFlow, the window, FileOperations and the library operations).

## Controller (per document)

The Controller is a facade over five sessions; it sequences what one session's work means for the others (an edit is validated, an opened file is laid out, the edited template copies are committed on save):

- `EditSession`: transactions and gps actions through one `perform()`, undo/redo, the save point, the one edit-blocked guard, the support-report note.
- `ValidationSession`: the pause that gathers a burst of edits, dispatch, slow watch, cancel, result.
- `RunSession`: run and monitor through one helper path (`HelperClient`, streaming gps-monitor's lines), on a temporary copy.
- `TemplateInstances`: expanded / edited / modified copies, commit, outdated and update, parameters, renames in the library.
- `DocumentViews`: the canvas and drawer views; names and the outline kept per revision (and expanded set).

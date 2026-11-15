# Working together on one board

Two or more people work on the same board over the internet: one starts a
session and passes an invitation on, the others join, and everybody draws on
the same board. The line between the computers goes through a relay that only
passes encrypted messages on; how to set one up is described in
[relay/README.md](../relay/README.md). This document covers the board side:
how to use it, what is shared, the limits, and how the sync works.

## Using it

**Work together** is the button with the two people at the top right of the
board, next to the mode buttons. It opens a small panel.

### Starting a session

1. Open the document you want to share on the board.
2. In the **Relay** section of the panel, enter the address of the relay and,
   if the relay asks one, its key. Both come from whoever set up the relay; they
   are kept with the other settings and are only needed to start a session.
3. Optionally enter **Your name**. Nothing is taken from the computer: without
   a name you are shown to the others as "Participant" with a colour.
4. Press **Start session**. The panel shows the invitation, one line of text
   that holds the address of the relay and the code of the session. **Copy
   invitation** puts it on the clipboard; pass it on by mail or chat.

### Joining a session

Paste the invitation into the field under **Join a session** and press **Join**.
The relay address and the code are in the invitation, nothing has to be set up.
The board of the host arrives as a new document, named "Shared board <date>
(<name of the document>)"; the panel shows the progress for large boards. The
board switches to it. It stays a normal document after the session.

### During a session

* The button shows a small badge with the number of people in the session.
* The cursors of the others appear on the board as a dot in their colour with
  their name, while they are on the same page. While someone draws, his stroke
  shows as a line that follows his pen; the real stroke replaces it when he
  lifts the pen.
* **Follow page** (on by default): when someone turns the page, you go along,
  and the pages you turn take the others along. Switched off, you browse on
  your own: you do not follow the others and they do not follow you. Switching
  it on again brings you to the page of the group.
* Everybody keeps his own zoom, scroll position, tool, colours, selection and
  undo history: **Undo** undoes your own actions only.

### Leaving and the end of a session

**Leave session** leaves; for the host the button is **End session**: when the
host leaves, the session ends for everybody. Everybody keeps his copy of the
board. When a session ends without you leaving, a message says why:

| Cause | Message (shortened) |
|---|---|
| The host ended the session or left | The host ended / left the session. You keep your copy. |
| The line dropped and did not come back | The connection was lost. You keep your copy. |
| The relay cannot be reached | Check the address of the relay and the internet connection. |
| The relay refuses to start a session | Check the relay key. |
| No session with this code | There is no session for this invitation. |
| The session is full | The session is full. |
| The code does not decrypt the messages | The invitation is not right. |
| Another version of the program | The other uses a different version of OpenBoard-ML. |
| The host sent impossible data | The session was ended; you keep your copy. |
| The board did not arrive | The board of the host could not be received. |
| Nobody answers as host | Nobody answers for this session. |

Settings (user settings, group `Collab`): `RelayAddress`, `RelayKey`,
`DisplayName`, `FollowPage`.

## What is shared

| Shared | Notes |
|---|---|
| Pen and marker strokes, straight lines, the ink of formulas | When the pen is lifted; a live line meanwhile |
| The eraser, also partial erasing | The erased stroke is sent as a whole |
| Shapes of the Shapes tool | |
| Pictures and svg pictures (pasted, dropped, imported) | The file travels with the first item that needs it |
| Texts | After a pause of 1.2 s in typing, or when the typing ends |
| Moving, resizing, rotating, deleting, duplicating, grouping items, the order (z) | |
| The editable copies of the Capture tool | |
| Clearing a page | |
| The background of a page (light/dark, ruling, grid size), the page size | |
| Adding, removing, duplicating, moving pages | |
| The page the group is on | See *Follow page* |

Not shared, and never accepted from someone else: videos and audio, apps
(widgets), web content, PDF pages and other background objects set during the
session, the drawing tools (ruler, compass, protractor, triangle, axes,
magnifier, curtain, spotlight) and the laser pointer. Such things stay on the
board of whoever added them; the first time he gets a notice ("... are not
shared: they stay on your board only"). In the initial board, PDF page
backgrounds that were already there are part of the document that is sent;
widgets, media and web content are left out (with a notice to the guest).

## Limits

* Strokes arrive when the pen is lifted, texts after a pause in typing.
* Two people who change the same item at the same moment: the last change wins
  for the whole item (a move and an erase of the same stroke: one of them).
  Two people who erase the same piece of the same stroke at the same moment may
  get a result neither expected, but the same on every computer.
* A delete wins over a change made at the same moment; an undo of the delete
  brings the item back for everybody.
* Two items added at the same moment by two people may have the same z value;
  which one is on top can then differ between computers.
* When someone adds, removes or moves pages, the board of the others may jump
  to the page it showed at its new place, and their undo history is cleared
  (the board does that on every page change).
* The last page of a document cannot be removed; when everybody removes pages
  at the same moment until none is left, the host makes a new empty page.
* A group that holds both shared and local-only items is shared with the shared
  ones only.
* Limits on what is accepted: a page of 64 MB, a file of 64 MB, an item of
  16 MB, 2000 pages, a board of 1 GB. Larger things stay local (with a notice).
* Everybody needs the same version of the protocol (the same OpenBoard-ML).
* If the host himself misses a file of a picture (lost message), the picture
  cannot be restored from the others; the digest then removes it everywhere.

## How it works

### Layers

| Layer | Class | Role |
|---|---|---|
| Line | `UBCollabChannel`, `UBCollabRelayChannel` | Opaque messages between participants (relay, encryption); `UBCollabLoopbackChannel` for tests |
| Session | `UBCollabSession` | Greeting and version, the initial board, digests, page requests, cursors, the end of a session |
| Document | `UBCollabDocumentSync`, `UBCollabRules` | Sees the changes of the user, sends operations, applies those of the others (the rules of the registers are one function), digest |
| Items | `UBCollabItems`, `UBCollabSvgFilter` | Which items are shared, fingerprints, item text in and out, cleaning what arrives |
| Board | `UBCollabBoardLink`, `UBCollabController`, `UBCollabPanel`, `UBCollabCursorOverlay` | The board on the screen, the button, the panel, the cursors |

A session is bound to a document, not to whatever the board shows: changes on
a page that is not on the board are applied to that page through the scene
cache and saved. That is also how one process can hold a host on one document
and a guest on another (the self-test does).

### Seeing the changes of the user

The sync does not hook into every place where the board changes items. After
every change of the undo stack (push, undo, redo), after a page turn and every
half second it compares the page on the board with what it last sent or
applied: the set of shared items at the top of the page and a fingerprint per
item (geometry, transform, z, colours, children; for texts the html with black
and white treated as one, as they swap with the background). Pages are compared
by the list of page uuids of the document. Whatever path a change took (a tool,
a menu, undo, a script), it is seen; an undo is just another change. Nothing is
compared while a press on the board is not released, so a stroke or a drag is
sent once it is complete. With no session the cost is nil: nothing is
connected.

### Operations

All messages start with a magic number and a type; the first message to every
participant is `Hello` with the protocol version. Fields are little endian with
a length before variable data.

| Message | Content |
|---|---|
| `Hello`, `Bye` | protocol version, role (host/guest), name; reason |
| `SnapshotRequest`, `SnapshotBegin`, `SnapshotPage`, `SnapshotAsset`, `SnapshotEnd` | the initial board |
| `Asset` | a file an item refers to (a picture), sent before the item |
| `ItemOps` | for one page: items added, changed, removed (removals first) |
| `PageOp` | a page added (with its content), removed or moved |
| `PageProps` | background and size of a page |
| `CurrentPage` | the page the group is on |
| `Cursor` | pointer position, pen down, colour, width, the points drawn since the last update |
| `Digest`, `DigestRequest` | summary of the registers (host) |
| `PageRequest`, `PageContent`, `DocStateRequest`, `DocState` | a page or the list of pages as the host has it |

The content of an item is a page in the format of the page files that holds
only that item: `UBSvgSubsetAdaptor::itemsToSvg` writes it, the normal page
reader reads it. There is no second format to keep in step with the page
files. Applying keeps the item that is on the page and gives it the new content
(the polygons of a stroke are swapped, a text gets the new html), because the
undo steps of the user refer to that item; an operation never pushes an undo
step, never takes the selection or the keyboard focus, and waits while the user
draws. A text the user is typing in keeps his text until he is done.

Pictures have names made from their content, so a file that is there already
is the same file; a file that is missing when an item arrives makes the page be
fetched again with its files.

### Conflict rules

Every item of a page and every page has a register: the *incarnation* (the
version of the add that made it exist), the *version* of its last change and
whether it is *alive*. Versions are Lamport clock values with the participant
as tie-break, so two operations are never equal. On every computer:

* an operation of an older incarnation is ignored;
* within an incarnation a removal wins over every change, and of two changes the
  higher version wins (last writer wins per item);
* an add with a newer incarnation brings an item back (an undo of a delete);
* adding, changing and removing are idempotent (a message that arrives twice
  changes nothing);
* the register of a removed item holds the highest removal of its incarnation,
  not the version of a change that happened to arrive before the removal, so
  that it is the same everywhere.

`UBCollabRules::decide` is the one place of these rules; a randomized test
(several replicas making concurrent adds, changes and removals, delivered in
random orders per sender and sometimes twice) ends in the same registers and
content on every replica.

Items of the initial board have no register until they change. Pages are
ordered by keys: strings compared byte by byte, with a key between any two
keys, so a page can be put anywhere without renumbering the others; the order is
by key, then uuid. The background of a page, its size and the page of the group
are registers with the last writer winning.

### Digest and resync

The host sends a digest now and then: a hash over the registers of every page
and over the list of pages (not over geometry or svg text: numbers are written
differently on different computers and a page read again is not identical to
the bytes it came from). A guest compares it with his own; a page that differs
in two digests in a row (one after the line came back, `resumed()`) is fetched
from the host, who is the authority, and replaces the page of the guest. A
difference of the list of pages fetches the list. When the line of a guest
comes back, he first sends again everything whose last version is his (the
others ignore what they have), so that a resync does not undo his work.
Someone who joins late gets the current board.

### The initial board

The host saves the board, then sends the page files as they are on disk and the
files they refer to, in parts of at most 1 MB, holding back while more than 8 MB
waits on the line. The guest writes them into a new folder of his documents as
they arrive and makes it a document only when everything arrived and fits; on
any error the folder is removed. The uuids of the pages and items are those of
the host, so the operations that follow find them. What others sent while the
board was on its way is applied after it.

### Security

Everything that arrives is untrusted input from another computer:

* every read is checked against the bytes that are left and against a limit
  (lengths, counts, numbers of pages, items, points); nothing is allocated before
  it is known to be there; numbers must be finite;
* item and page text is cleaned by `UBCollabSvgFilter` before the page reader
  sees it: only strokes, pictures, svg pictures, texts, shapes and groups (and for
  whole pages PDF pages and the ruling) are kept; widgets, web content, audio,
  video, tools, scripts, event attributes and document type definitions are left
  out; coordinates beyond 10^8 are refused; images and links are taken out of
  texts;
* file names are plain names in `images/` (and `objects/` for PDFs of the initial
  board) with a known extension, never a path with `..`, a drive or an absolute
  path; files are only written inside the document of the session;
* only the host may replace pages or the list of pages, send the initial board
  or digests; a participant whose messages are refused often is no longer
  listened to (the host: the session ends);
* a refused message is logged and changes nothing.

The relay sees who is connected and how much is sent, not what: the channel
encrypts with a key made from the session code (see `relay/README.md`).

### Hooks in the existing code

* `UBSvgSubsetAdaptor`: the writer can write to memory and a subset of items
  (`sceneToSvg`, `itemsToSvg`); `persistScene` uses the same code.
* `UBBoardPaletteManager`: the button in the right cluster of the board.

Everything else uses signals that were there (undo stack, persistence manager,
board controller) and an event filter on the board view (presses and the
pointer, without taking events).

### Testing

`UBCollabLoopbackChannel` connects participants inside one process, with delay,
lost and duplicated messages and `resumed()`. A self-test (not part of the
product) runs a host and guests in one process with synthetic input on the
board and compares the documents after every step.

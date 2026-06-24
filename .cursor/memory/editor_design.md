# Editor Design Memory

Editor features may depend on engine runtime systems, but runtime modules must not depend on editor UI/tooling.

Asset writes should be transactional where possible. Undo/redo should be considered for state-mutating UI actions.

Long operations should not block the UI thread without progress and cancellation.


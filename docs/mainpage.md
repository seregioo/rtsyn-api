# RTSyn Runtime

RTSyn Runtime is the C runtime layer for executing RTSyn states.

## Public API

The public API is declared in:

- `include/rtsyn/runtime.h`: runtime creation, destruction, and state processing.
- `include/rtsyn/runtime/state.h`: state identifiers and the functions that help the abstraction for the runtime.

## Basic Usage

```c
#include "include/rtsyn/runtime.h"

int main(void) {
  rtsyn_runtime_t *runtime = rtsyn_runtime_create();

  if (runtime == NULL) {
    return 1;
  }

  rtsyn_runtime_destroy(runtime);
  return 0;
}
```

## Generating Documentation

Run:

```sh
xmake doxygen
```

Then open `build/html/index.html`.

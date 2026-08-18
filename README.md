# RTSyn API

The RTSyn API is a C++ HTTP bridge over the RTSyn SPSC queues. It produces
command messages for the realtime engine and consumes telemetry events from the
engine. Value telemetry is copied out of the telemetry values SPSC and appended
as JSON lines to a file, `/tmp/rtsyn-values` by default.

It is intended to run as a small container/process next to the runtime process.
It does not own runtime execution, plugin loading, or graph construction.

## HTTP

- `GET /health`
- `GET /telemetry/events`
- `GET /telemetry/values-file`
- `POST /commands/global`
- `POST /commands/plugin`
- `POST /commands/plugin/load`
- `POST /commands/plugin/add`
- `POST /commands/device/load`
- `POST /commands/device/add`
- `POST /commands/port-values`
- `POST /commands/variables`

Global command example:

```bash
curl -X POST localhost:17190/commands/global \
  -d '{"command":"pause"}'
```

## Environment

- `RTSYN_API_HOST`, default `0.0.0.0`
- `RTSYN_API_PORT`, default `17190`
- `RTSYN_COMMAND_QUEUE`, default `/rtsyn_commands`
- `RTSYN_TELEMETRY_QUEUE`, default `/rtsyn_telemetry`
- `RTSYN_TELEMETRY_VALUES`, default `/rtsyn_telemetry_values`
- `RTSYN_VALUES_FILE`, default `/tmp/rtsyn-values`
- `RTSYN_API_CREATE_QUEUES=1` creates the shared-memory objects instead of opening existing ones

## Usage

### Update

Make sure you have last version of the dependencies:

```bash
xrepo update-repo
xmake require --upgrade
```

For development you may need to run:

```bash
xmake require --upgrade -fy <dependency_name>
```

### Compiling

For compiling:

```bash
xmake
```

### Tests

For running test:

```bash
xmake test
```

For enabling valgrind, before running tests:

```bash
xmake f --valgrind=y
```

For disabling valgrind, just replace `y` for `n`.

### Local development

If you want to test your changes locally from different parts of RTSyn, export the path where you have all the repos:

```bash
export RTSYN_WORKSPACE=<PATH>
```

> [!WARNING]
> This expects you also the `rtsyn-xmake-repo`.

### Cleaning

To remove all generated build artifacts:

```bash
xmake clean --all
```

To also reset cached configuration and tool detection:

```bash
xmake f -c
```

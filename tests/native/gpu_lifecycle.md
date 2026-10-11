# NVIDIA D-Bus reply leak

The standalone, manually run `native_gpu_lifecycle` executable links SDL and Catch2 only. It
contains no game, asset, audio, or SurRender code. Its three cases separate video
initialization, GPU device creation, and window attachment. Every resource is
released before `SDL_Quit()`, including on assertion failures.

On the investigated host (2026-10-11), NVIDIA 595.104.02, SDL 3.4.2 and D-Bus
1.16.2 exhibit a real missing-reference-release bug during GPU initialization.
Video initialization alone exits cleanly. GPU initialization with NVIDIA leaks
approximately 4 KB in six allocations. All three cases exit cleanly with Mesa
llvmpipe, with AddressSanitizer and LeakSanitizer enabled.

## Reproduce

```sh
cmake --build --preset linux-asan --target native_gpu_lifecycle
SDL_VIDEODRIVER=offscreen \
  VK_DRIVER_FILES=/usr/share/vulkan/icd.d/nvidia_icd.json \
  build/linux-asan/native_gpu_lifecycle 'SDL GPU device*'

# The video-only negative control on the same host:
SDL_VIDEODRIVER=offscreen \
  build/linux-asan/native_gpu_lifecycle 'SDL video startup*'

# The software-driver control exercises all three cases:
SDL_VIDEODRIVER=offscreen \
  VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.json \
  build/linux-asan/native_gpu_lifecycle
```

The executable is deliberately not registered with CTest: testing the SDL/Vulkan driver in isolation does not validate game behavior and can introduce host-dependent failures into otherwise meaningful CI runs. Run it explicitly when investigating a driver leak. The executable prints the selected device and driver. ICD filenames vary between
installations. No suppression file, `detect_leaks=0`, preload, or disabled D-Bus
connection is required for these controls. The isolated test sets
`SDL_HINT_SHUTDOWN_DBUS_ON_QUIT` to release D-Bus caches, following SDL's documented
leak-test usage; this hint must not be added to the game because other libraries
can still own D-Bus objects when SDL quits.

## Ownership evidence

The unknown-module caller in the leak stack maps to
`libnvidia-glcore.so.595.104.02`. Running the same minimal SDL program with an
unmodified debug build of D-Bus 1.16.2 and the real system-bus address gives full
symbols. Set `DBUS_VERBOSE=1 DBUS_MESSAGE_TRACE=1` to trace message references.
For each of two private NVIDIA system-bus connections, the final sequence is:

```text
ListNames method call sent (serial 4)
ListNames method return received (signature 'as', reply serial 4)
reply 1 -> 2 refs: _dbus_pending_call_set_reply_unlocked
reply 2 -> 1 refs: check_for_reply_and_update_dispatch_unlocked
reply ref stolen: dbus_pending_call_steal_reply
(no matching dbus_message_unref; final refcount remains 1)
```

A debugger breakpoint on `dbus_connection_send_with_reply_and_block` identifies
the `ListNames` caller as `libnvidia-glcore.so.595.104.02`, at module offset
`0x6c5f4` after the call, reached through `0x6cbd0`. NVIDIA closes and unreferences
both connections, and SDL does the same for its connections. SDL then calls
`dbus_shutdown()`, but those calls cannot free messages still referenced by the
NVIDIA caller.

Some messages were originally allocated while SDL registered its bus connection,
then cached and reused by NVIDIA. LeakSanitizer records the original allocation
stack, so an SDL frame in that stack does not identify the leaking owner.

The required driver change is to unreference each `ListNames` reply after
processing it, on every exit path. The game never receives these message pointers
and cannot release them correctly. This report and reproducer prepare that fix
for the driver owner; the NVIDIA leak remains unresolved. The repository keeps
leak detection enabled and the failing control visible, rather than introducing
a driver-specific interposer or shutting down another library's bus.

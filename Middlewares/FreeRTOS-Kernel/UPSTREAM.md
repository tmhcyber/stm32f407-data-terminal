# Vendored FreeRTOS kernel subset

- Official upstream: https://github.com/FreeRTOS/FreeRTOS-Kernel/tree/V11.1.0
- Fixed release: V11.1.0 (MIT; see LICENSE.md). Not a claim to be the latest release.
- Archive: https://codeload.github.com/FreeRTOS/FreeRTOS-Kernel/zip/refs/tags/V11.1.0
- Archive SHA-256: `d5875acad2479817b35b2031dc1c7c22d100f45c7f3c9f6039cb217437a298a2`
- Imported unchanged: root C sources, include/, portable/RVDS/ARM_CM4F/,
  portable/MemMang/heap_4.c, LICENSE.md, README.md, History.txt.
- Used by Experiments/FreeRTOS_Queue only; not added to the bare-metal target.
- Linked subset: tasks.c, queue.c, list.c, port.c, heap_4.c.
- Configuration belongs to the experiment, not to these upstream files.

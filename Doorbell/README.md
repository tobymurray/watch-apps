# Doorbell: can an app on this watch say something to the phone, when it wants to?

A probe, not a product. It exists to answer one question with a wire capture:
**does an ordinary third-party `.uapp` cause the watch to push a BLE notification
at a moment the wearer chooses?**

> Unofficial. Not affiliated with, endorsed or sponsored by UNA Watch Ltd.
> It writes one file inside its own sandbox and sends one kernel message. It
> touches no hardware register and no firmware.

## Why the question is open

The SDK's whole app-visible message surface is enumerated in
`Libs/Header/SDK/Messages/MessageTypes.hpp`, and there is no companion channel in
it: no phone message, no notification egress, no BLE transmit. An app can write
files, and the phone can read them over the file-transfer service, but nothing in
the SDK lets an app say *now*.

One message looks like it might, by accident. `CommandAppNewActivity` tells the
kernel an activity session has ended, and the kernel answers it by pushing a
ten-byte frame over its custom command service that names the app that sent it.
That frame was decoded off real hardware for Treadmill and Walk, which are UNA's
own apps. Whether an app you wrote yourself can cause it is what this measures.

If it can, an app has a doorbell: a push the phone receives inside about one
connection interval, carrying the app's own identity, at a time of the app's
choosing. Pair it with a file the app wrote first and the phone has an arbitrary
payload to go with it.

## The four buttons, and the variable each one isolates

The wearer's gesture is **three clicks of R1**, the top-right button. The other
three buttons exist so that a run which sees no frame can say *which* thing was
responsible, instead of just failing.

| Button | What it does | What it isolates |
| --- | --- | --- |
| **R1** ×3 within 2 s | writes `/command.json`, flushes it, then rings from the GUI | the whole gesture, mailbox and all |
| **L1** click | rings from the GUI, writing nothing | whether a saved file is a precondition |
| **L1** long press | rings three times back to back | whether the kernel coalesces or rate limits |
| **L2** click | asks the service to ring instead | whether the sending process matters |
| **R2** click | exits | |

Every ring vibrates, so the wearer knows the app thinks it fired even when
nothing at all appears on the wire.

The screen shows the total ring count in large digits, three blocks that fill as
the R1 gesture banks clicks, and a background colour naming the last ring:
green wrote a file, blue did not, amber went through the service, red was a burst.

**Why the service half sends it too.** Every SDK example, meaning Treadmill,
Running, Workout, Cycling and Hiking, sends `CommandAppNewActivity` from its *service*, after
writing a `.fit`. That is the path with known-good precedent, and the GUI sending
it directly is the convenient path. They are separate buttons because a run has to
be able to tell them apart.

## The service outlives its GUI on purpose

Most apps exit their service when the GUI closes. This one does not: on
`COMMAND_APP_NOTIF_GUI_STOP` it logs the time, stays up, beats every 30 seconds,
and **rings once, 30 seconds later, with nothing on screen**. That single ring is
the cheapest possible test of whether egress needs a foreground app, which is the
difference between "tap my watch" and "open my app, then tap my watch".

It exits ten minutes after the GUI goes, or immediately on `COMMAND_APP_STOP`. A
service with no deadline is a stuck process.

## No screen taps, and why

There are none available. `STM32TouchController::sampleTouch` in
`Libs/Source/Port/TouchGFX/STM32TouchController.cpp` returns `false`
unconditionally, and `cmake/una-sdk.cmake` compiles exactly that file into every
device build; no message type carries touch; and the sensor layer's `TOUCH_DETECT`
is worn/unworn, not fingers. Across every example app and tutorial in the SDK,
nothing implements `handleClickEvent` and 112 headers implement `handleKeyEvent`.

The simulator is the trap here: it links TouchGFX's `SDL2TouchController` instead,
so a touch-driven screen works at the desk and is dead on the wrist.

## Reading the result

`Debug/doorbell.log` on the watch carries a line per button, per ring and per
heartbeat, each with the kernel's millisecond clock, so a frame seen on the phone
can be attributed to a press. The mailbox is `/command.json` inside the sandbox,
which is `/Apps/Doorbell/command.json` over the file-transfer service.

## Building

Needs `$UNA_SDK` on **`apps-v1.4.0`** or `main` at 1.4.0.

```sh
export UNA_SDK=/path/to/una-sdk
cd Software/Apps/Doorbell-CMake
cmake -B build -G "Unix Makefiles" -DBUILD_VERSION=0.1.0 . && cmake --build build
```

The `.uapp` lands in `Output/`. Two packaging notes, both cost a build each:
`app_packer.py` imports `PIL` unconditionally even with icons off, and
`app_merging.py` rejects a non-Glance app that has no icon, so `APP_USE_ICONS Off`
does not work here whatever CMake accepts. `Resources/make_icon.py` draws the two
PNGs it wants.

Install by the procedure in [`Docs/INSTALLING.md`](../Docs/INSTALLING.md), and
check the app registered before believing anything it wrote.

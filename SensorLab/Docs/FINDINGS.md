# Findings

Things that look like platform defects, each with a minimal reproduction. Written
so that any one of them could be pasted into an issue by the repository owner.

**Nothing here has been posted anywhere.** This repository does not comment on
`UNAWatch/una-sdk` PRs or issues; upstream communication is the owner's.

Two kinds of entry, kept apart because they cost different amounts to act on:

- **Found by reading**, which is most of this file today. Reading settles what
  the spec says. It can refute a claim about the device and it can never confirm
  one, so every entry here says which it is.
- **Found by measuring**, which is what a hardware run adds. There are none yet:
  SensorLab has not been run on the watch. The ones below that say *measured* were
  measured on the **host or simulator build**, and they say so.

Each entry names the SDK commit it was found against: `apps-v1.4.0`
(`edf2feeb`), which is also `main` as of 2026-08-21.

---

## 1. `Docs/SensorsLayer.md` is six sensor types behind `SensorTypes.hpp`

**Kind:** documentation. **Found by:** reading, mechanically — the check is a
committed script.

`Libs/Header/SDK/SensorLayer/SensorTypes.hpp` declares 37 distinct sensor types.
`Docs/SensorsLayer.md`'s table lists 31. Missing entirely:

| Type | Value | The header's own comment |
| --- | --- | --- |
| `HEART_RATE_EX` | 0x43 | "Opt-in multi-source HR: arbitrated + source + raw optical + raw external. 7 fields." |
| `STEP_COUNTER_DAILY` | 0x52 | "Step count for the current day." |
| `RUNNING_CADENCE` | 0x53 | "Running cadence (steps/min); step length is derived SDK-side." |
| `FLOOR_COUNTER_DAILY` | 0x61 | "Floor counter for the current day." |
| `ACTIVITY_TIME` | 0xE0 | "Active minutes since boot (monotonic)." |
| `GRADE` | 0x150 | "Barometric terrain grade (%) + validity." |

`ACTIVITY_TIME` is the subtle one. The doc lists `ACTIVITY` at 0xE0, but the
header defines `ACTIVITY` as an **alias for `ACTIVITY_TIME_DAILY` (0xE1)** and
gives 0xE0 to the new since-boot `ACTIVITY_TIME`. So a reader following the doc
who writes `ACTIVITY` gets 0xE1 and believes they asked for 0xE0. That is not a
missing row; it is a row whose meaning changed underneath it.

**Reproduction.** `SensorLab/Tools/gen_catalogue.py` parses both and fails if
they disagree in a way it has not been told about:

```sh
UNA_SDK=/path/to/una-sdk python3 SensorLab/Tools/gen_catalogue.py \
    --check --out SensorLab/Software/Libs/Header/Catalogue/SensorTypeTable.generated.hpp
```

**Why it matters more than a stale table usually does.** This is the reason
SensorLab does not have a typed type list: an app that copied that table would
inherit whichever version its author read. Six types is 16 % of the sensor
surface invisible to anyone working from the documentation.

---

## 2. Field-count divergences between the doc and the parsers

**Kind:** documentation. **Found by:** reading.

Beyond the missing types, the doc's field counts and units disagree with the
shipped parsers for types it *does* list:

| Type | `SensorsLayer.md` says | The parser declares |
| --- | --- | --- |
| `GPS_SPEED` (0x111) | `SPEED (f m/s) - 1` | **3** fields: `SPEED`, `SPEED_VALID`, `DEAD_RECKONING` |
| `ACTIVITY` (0xE0) | description "Active minutes (minutes)", field `DURATION (u32 ms)` | `SensorDataParserActivity.hpp`: "Activity duration in **minutes** (uint32_t)" |
| `AMBIENT_TEMPERATURE` (0x70) | `TEMP (float °C?)` — a literal question mark | "Temperature value (units are device-specific)" |

`GPS_SPEED` is the one with teeth. An app sized to one field and handed three
gets a frame its `isDataValid()` rejects — and 28 of the 29 parsers test field
count for exact equality, so it rejects *every sample*, silently. The doc's own
`ACTIVITY` row contradicts itself in the same line: the description says minutes
and the field says milliseconds.

**Reproduction.** Compare `Docs/SensorsLayer.md`'s table against
`Libs/Header/SDK/SensorLayer/DataParsers/SensorDataParserGpsSpeed.hpp`'s
`Field::COUNT`.

---

## 3. `GpsLocation::isDataValid()` reads a field before checking the field count

**Kind:** likely out-of-bounds read. **Found by:** reading. **Severity:** this is
the one worth acting on first.

`Libs/Header/SDK/SensorLayer/DataParsers/SensorDataParserGpsLocation.hpp:58`:

```cpp
bool isDataValid() const
{
    return ((mData.u[Field::COORDS_VALID] <= 1) &&
            (mData.getFieldCount() == Field::COUNT));
}
```

`&&` evaluates left to right, so `mData.u[1]` is dereferenced **before** the
field count is known to be 5. `DataView::U32View::operator[]` does have a bounds
check —

```cpp
uint32_t operator[](uint16_t idx) const noexcept
{
    assert(idx < fieldCount);
    return data.mValue[idx].u32;
}
```

— but it is an `assert`, and apps are built `-Os` with `NDEBUG`, so in any shipped
build it is compiled out. A one-field `GPS_LOCATION` frame therefore reads one
`Field` past the end of the sample.

It is the only parser of the 29 that does this; the check is mechanical and lives
in `gen_catalogue.py` as `readsBeforeCount`.

**Why nobody has hit it.** Nothing on this platform has ever met a frame that
does not match its parser, because no app subscribes to a type and then questions
the frame. A profiler is the first thing that will, which is why SensorLab derives
the field count from the batch stride and validates it **before** constructing any
parser (`Service::onSensorData`).

**Fix.** Reorder the `&&`. One line.

**Reproduction (host).** `SensorLab/Tests/Pipeline_test.cpp`,
`AFrameNarrowerThanItsParserIsAlsoRecordedRatherThanParsedBlind`, delivers a
one-field frame on a `GPS_LOCATION` handle. It passes because SensorLab does not
construct the parser; an app that did would read out of bounds on the same input.

---

## 4. `SDK::Sensor::Connection` truncates the sensor handle to 8 bits

**Kind:** silent data corruption, latent. **Found by:** reading.

`Libs/Header/SDK/SensorLayer/SensorConnection.hpp:163` stores

```cpp
uint8_t mHandle;
```

while `SDK::Message::Sensor::RequestDefault::handle` is a `uint32_t`
(`SensorLayerMessages.hpp:42`) and `Connection::matchesDriver()` takes a
`uint16_t` (`SensorConnection.hpp:140`). So:

- `subscribe()` does `mHandle = req->handle;` — anything above 255 truncates,
  with no diagnostic;
- `matchesDriver(handle)` then compares an untruncated argument against a
  truncated member, so **every batch is dropped**; or, if two handles collide
  modulo 256, one sensor's samples are attributed to another.

The three widths are also mutually inconsistent regardless of the truncation:
a 32-bit field, a 16-bit comparison and an 8-bit store.

**Why nobody has hit it.** Every handle observed on this firmware so far is
small. A profiler subscribing to all 37 types at once is the app most likely to
be handed a large one.

**Fix.** `uint32_t mHandle`, and `matchesDriver(uint32_t)`.

**Reproduction (host).** `SensorLab/Tests/Pipeline_test.cpp`,
`AHandleAboveTwoHundredAndFiftyFiveIsNotTruncated`, gives the accelerometer handle
`0x1234` and asserts its batches are still attributed to type 0x10. SensorLab
passes because it keeps handles at full width in `Probes::SensorBus`; the same
scenario through `Connection` would drop every sample.

---

## 5. `RequestList` and `RequestGetDesc` are declared, implemented, and used by nothing

**Kind:** unused capability. **Found by:** reading both repositories.

`SensorLayerMessages.hpp` declares `RequestList` (up to 10 handles per type) and
`RequestGetDesc` (a 32-char descriptor per handle). The simulator's
`KernelMessageDispatcher.cpp` implements both. **No app in `una-sdk` or in
`watch-apps` sends either**, because `SDK::Sensor::Connection` only ever sends
`RequestDefault`.

They are the cheapest existence-and-identity probe on the platform.
`RequestGetDesc` in particular is the kernel naming its own driver — the closest
thing to an authoritative part identification an app can obtain, and the only
app-side check available on the hardware investigation's I²C sweeps.

Not a defect. Recorded because a capability nobody uses is a capability that will
be removed, and because SensorLab's layer 1 now depends on both.

**`RequestGetDesc::desc` is a `char[32]` with no documented terminator.** An app
that treats it as a C string reads past the pool block when a driver name fills
the field exactly. SensorLab copies bounded and terminates itself, and
`Tests/Pipeline_test.cpp`'s `ADescriptorFillingAllThirtyTwoBytesDoesNotRunOn`
pins that.

---

## 6. `isDataValid()` is exact field-count equality in 28 of 29 parsers

**Kind:** design asymmetry with a real consequence. **Found by:** reading,
mechanically.

`HeartRateEx` is the only parser that accepts a wider frame than it declares:

```cpp
return (mData.getFieldCount() >= Field::COUNT);
```

The other 28 test `==` (some as `!=` in an early-return guard, which is the same
predicate). So **one appended field is a no-op for `HEART_RATE_EX` and total,
silent data loss for every other type on the platform** — `isDataValid()` returns
false, every getter returns its zero, and nothing logs anything.

That this has already happened once is on the record: the comment at the top of
`SensorDataParserRunningCadence.hpp` says *"This is a breaking wire-format
change: Field::COUNT shrank from 4 to 2."*

Eight parsers additionally range-check a value, not just the width:
`ActivityRecognition`, `BatteryCharging`, `BatteryLevel`, `GpsLocation`,
`MotionDetect`, `StepDetector`, `Touch`, `WristMotion`. Two of those are worth
their own note — see §7.

**Reproduction.** `SensorLab/Tests/CatalogueGeneration_test.cpp`,
`OnlyHeartRateExAcceptsAWiderFrameThanItDeclares` and
`EightParsersRangeCheckAValueAsWellAsTheWidth`.

---

## 7. `StepDetector` and `WristMotion` return a bool from a getter documented as a count

**Kind:** API/doc mismatch. **Found by:** reading.

`SensorDataParserStepDetector.hpp`:

```cpp
bool isDataValid() const
{
    return (mData.getFieldCount() == Field::COUNT) && (mData.u[Field::STEP_DETECTED] == 1);
}

/** @return Step count as uint32_t (0 if invalid) */
bool getStepCount() const { return isDataValid(); }
```

The doc comment promises a step count; the declared return type is `bool`; and
the body returns validity. `WristMotion::getWristMotion()` is the same shape.

For an event sensor whose only field is a flag, `isDataValid()` and "the event
happened" collapse into one predicate, so the *behaviour* is arguably right. The
doc comment is not, and a caller who believed it would write
`total += p.getStepCount()` and count events instead of steps — which, for a step
*detector*, happens to be correct, and for anything else would not be.

**Fix.** Change the doc comments, or rename the getters `detected()`.

---

## 8. `HeartRateEx::getSource()` reads an enum through the float member

**Kind:** undocumented encoding. **Found by:** reading; the encoding itself is
settled by an existing measurement.

Every other enum field on the platform is read through `mData.u[...]`.
`SensorDataParserHeartRateEx.hpp:93` reads `SOURCE` through `mData.f[...]`:

```cpp
switch (static_cast<uint8_t>(mData.f[SOURCE])) { ... }
```

If the kernel wrote the integer 1 for `OPTICAL`, the float reinterpretation of
`0x00000001` is ~1.4e-45 and the cast yields 0 = `UNKNOWN`. So this only works if
the kernel writes `1.0f`.

**It does.** SleepLab's ledger row S8 measured 30 169 `HEART_RATE_EX` samples over
one night, all attributed `OPTICAL`, none `UNKNOWN`. The parser is right and the
type's own comment — `SOURCE, ///< Which source was chosen (Source)`, next to an
`enum class Source : uint8_t` — is what misleads.

**Fix.** Say in the header that the field is a float-encoded enum. It is the only
one, and a reader who "corrects" the parser to `mData.u[]` would silently turn
every reading into `UNKNOWN`.

**Reproduction.** `SensorLab/Tests/CatalogueGeneration_test.cpp`,
`HeartRateExsSourceIsTheOnlyFloatEncodedEnumOnThePlatform`.

---

## 9. `JsonStreamWriter::add(int32_t)` writes garbage for negative values on 64-bit builds

**Kind:** defect. **Found by:** **measuring**, on the host build. Not a reading.

`Libs/Source/JSON/JsonStreamWriter.cpp:523`:

```cpp
void JsonStreamWriter::writeInt(int32_t value)
{
    char buf[32] { };
    int len = snprintf(buf, sizeof(buf), "%ld", value);   // %ld against an int32_t
    writeData(buf, len);
}
```

`%ld` expects a `long`. On any LP64 target — the host tests, the Linux simulator —
`long` is eight bytes and `int32_t` is four, so this is undefined behaviour. In
practice on x86-64 the varargs slot holds the value zero-extended, so a **negative
number comes out as its unsigned reinterpretation**.

Measured: SensorLab wrote a decimal exponent of `-5` and the file contained
`4294967291`. That is what led to this entry.

ARM is unaffected, because `long` is four bytes there. **Which is exactly why the
defect survives**: it cannot be caught by the build that ships, only by the two
that do not.

The compiler already says so and the warning is not acted on:

```
JsonStreamWriter.cpp:526:45: warning: format '%ld' expects argument of type
'long int', but argument 4 has type 'int32_t' {aka 'int'} [-Wformat=]
```

`writeUint` has the same mismatch (`%lu` against a `uint32_t`, line 533) and
happens to produce correct output on x86-64 because the zero-extension is
harmless for an unsigned value. It is still UB.

**Fix.** `%d` and `%u`, or `PRId32`/`PRIu32`.

**Who else is affected.** Any app writing a negative `int32_t` to JSON on the host
or in the simulator. SleepLab's summary casts everything to `int32_t`, so any of
its negative values — a signed current, a signed delta — is wrong in its host
tests and right on the watch.

**Reproduction (host).**

```cpp
char buf[64];
SDK::JsonStreamWriter w(buf, sizeof(buf));
{ SDK::JsonStreamWriter::MapScope m(w); w.add("x", static_cast<int32_t>(-5)); }
w.flush();
// buf contains {"x":4294967291}
```

---

## 10. `JsonStreamWriter::add(int64_t)` routes through `double`, so a timestamp loses its seconds

**Kind:** defect. **Found by:** **measuring**, on the host build.

`JsonStreamWriter.cpp:403`:

```cpp
void JsonStreamWriter::add(int64_t value) { add(static_cast<double>(value)); }
void JsonStreamWriter::add(uint64_t value) { add(static_cast<double>(value)); }
```

and `writeDouble` formats with `%g` — six significant digits. So a UNIX timestamp
written through `add(int64_t)` comes out as `1.75555e+09`: the year survives and
the seconds do not. Any value above 2^53 is additionally lossy in the `double`
itself.

It also inherits the float-`printf` risk: `%g` is a floating-point conversion, and
the watch's newlib may not link those. When it does not, `%f` and `%g` emit
nothing **at runtime** rather than failing at link time — which is why every app
in this repository scales to an integer instead, and why SleepLab's Sleep Probe
format spec says outright that "a diagnostic that silently prints empty strings
for its own measurements is worse than one that scales by ten".

**Fix.** Format 64-bit integers as integers (`%lld` / `%llu` against a
correctly-cast `long long`), which is what SensorLab does for itself in
`Profile/ProfileWriter.cpp`.

**Consequence for this app.** Taken together with §9, **the only SDK number path
SensorLab trusts is `add(uint32_t)`.** Everything fractional, negative or wider
than 32 bits is written as a string. That is the whole reason `profile.json`
carries `"value": "20.4"` rather than `"value": 20.4`, and
`Tests/Pipeline_test.cpp`'s
`EveryNumberIsWrittenWithoutTouchingAFloatOrASignedSdkPath` is what keeps it that
way.

**Reproduction (host).**

```cpp
char buf[64];
SDK::JsonStreamWriter w(buf, sizeof(buf));
{ SDK::JsonStreamWriter::MapScope m(w); w.add("t", static_cast<int64_t>(1755553500)); }
w.flush();
// buf contains {"t":1.75555e+09}
```

---

## 11. The simulator resolves no sensor drivers for a service

**Kind:** simulator limitation. **Found by:** **measuring**, in the simulator.

A SensorLab simulator run reports `existence sweep: 0 of 37 types resolved a
driver`, and every sensor-layer request goes unanswered — the log fills with
`App.DualAppComm::sendMessage ... Queue is full` as 100+ synchronous requests
time out with nothing draining them.

Linking the entire simulator sensor layer changed nothing. Tried, and reverted:
`KernelMessageDispatcher.cpp`, `SensorManager.cpp`, `InstanceSensorLayer.cpp`,
`SampleRateAdapter.cpp`, `SensorDataQueue.cpp`, `SensorDataSample.cpp`,
`SensorDriver.cpp`, `SensorListener.cpp`, `ComponentSimulator.cpp`, all eleven
simulated drivers, `SwTimer.cpp`, `OneShotTimer.cpp` and `Timer.cpp` — the same
list `Docs/Tutorials/Sensors` uses, plus a `ConfigurationSimulator.hpp`. It built
and linked; the requests were still never completed.

This is the same class of limitation as SleepLab's ledger row T5, where the
simulator turned out not to deliver `COMMAND_APP_NOTIF_GUI_RUN` to a service at
all. The likely shape is that the simulator's kernel-side sensor dispatch is
reachable from the GUI process and not from a `Utility` app's service, but that
has not been established and is not this app's to establish.

**Not a blocker.** A simulator run still exercises the settings reader, the
manifest, the sweep's own code path, the claim store and its promotion rules, the
run log's append discipline, the profile writer, the roster burst and the screen.
It wrote a complete `profile-unknown.json` with 40 log rows. What it says about a
sensor is nothing, which was already the rule.

---

## 12. The GUI's custom-message queue is ten deep and discards the oldest

**Kind:** contract worth documenting. **Found by:** **measuring**, in the
simulator.

`SDK::TouchGFXCommandProcessor` holds app-specific messages in a
`FixedQueue<MessageBase*, 10>` (`TouchGFXCommandProcessor.hpp:72`) and, when it is
full, drops the **oldest** with a warning
(`TouchGFXCommandProcessor.cpp:118-120`).

An app whose GUI update is a burst therefore has a hard ceiling on how many
messages it may have in flight, and exceeding it loses the *first* of them —
which for an indexed burst means a silently incomplete data set rather than a
visibly missing one. SensorLab publishes four messages per update (a status plus
three roster bursts) and produced the warning when the GUI's `onStart` and
`onResume` both asked for one inside a frame.

Not obviously a defect — dropping the oldest is a defensible policy for a screen —
but it is undocumented, and "your burst must be under ten messages and you must
not publish twice in a frame" is a contract an app author would want stated.
SensorLab rate-limits publishing to once per 250 ms (`kPublishMinGapMs`) rather
than trusting callers to ask politely.

---

## Still to be found

Everything a hardware run would add. SensorLab has not been run on the watch, so
this file contains no measured sensor behaviour at all — every entry above came
from reading the SDK or from the host and simulator builds. The claims waiting on
a device are in `SensorLab/Docs/LEDGER.md`, and the profile itself lists them by
the method that would settle each one.

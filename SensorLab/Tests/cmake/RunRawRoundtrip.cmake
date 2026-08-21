# Write real raw chunks with the real writer, then read them with the real
# decoder -- and make the decoder cross-check itself against the run manifest.
#
# This round trip matters more than the report one. `profile.json` and
# `runs/<id>.csv` are text: a person can open them and see whether they hold
# anything. A raw chunk is binary, so **nothing except a decoder can say whether
# it contains what it claims to**, and a writer that quietly emitted zeroes would
# look exactly like a writer that worked.

file(MAKE_DIRECTORY "${WORKDIR}")

execute_process(COMMAND "${EXPORT}" "${WORKDIR}"
                RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "profile-export failed (${rc}):\n${out}\n${err}")
endif()

set(RAWDIR "${WORKDIR}/fw-1.4.0/raw")
if(NOT EXISTS "${RAWDIR}")
    message(FATAL_ERROR
        "profile-export wrote no raw/ directory. Raw capture is on by default "
        "for a soak, so either the soak did not start or capture is broken.")
endif()

file(GLOB CHUNKS "${RAWDIR}/*.bin")
if(CHUNKS STREQUAL "")
    message(FATAL_ERROR "no .bin chunks under ${RAWDIR}")
endif()
list(LENGTH CHUNKS N_CHUNKS)
message(STATUS "raw round trip: ${N_CHUNKS} chunk(s)")

# ---------------------------------------------------------------------------
# Decode, and verify against the manifest the same run wrote
# ---------------------------------------------------------------------------

execute_process(COMMAND "${PYTHON}" "${DECODE}" "${RAWDIR}"
                        --app-root "${APPROOT}"
                        --verify "${WORKDIR}/fw-1.4.0/runs/2.json"
                RESULT_VARIABLE rc OUTPUT_VARIABLE decoded ERROR_VARIABLE err)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR
        "raw_decode.py --verify failed (${rc}). Either the chunks disagree with "
        "the manifest, or the app dropped batches:\n${decoded}\n${err}")
endif()

# The verification's own conclusion, in words, so a reader of the test log sees
# the claim rather than only an exit code.
if(NOT decoded MATCHES "raw log is complete")
    message(FATAL_ERROR
        "the decoder did not confirm the raw log is complete:\n${decoded}")
endif()

# The fixture subscribes the accelerometer, heart rate, touch and battery, so
# those types must appear by name -- a decoder that read the records but resolved
# no type would pass a byte-count check and tell you nothing.
foreach(name ACCELEROMETER HEART_RATE BATTERY_LEVEL)
    if(NOT decoded MATCHES "${name}")
        message(FATAL_ERROR
            "decoded output does not mention ${name}:\n${decoded}")
    endif()
endforeach()

# ---------------------------------------------------------------------------
# One row per sample, and the fields three ways
# ---------------------------------------------------------------------------

execute_process(COMMAND "${PYTHON}" "${DECODE}" "${RAWDIR}"
                        --app-root "${APPROOT}"
                        --type 0x10 --kinds
                        --csv "${WORKDIR}/accel.csv"
                RESULT_VARIABLE rc OUTPUT_VARIABLE out2 ERROR_VARIABLE err)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "raw_decode.py --csv failed (${rc}):\n${out2}\n${err}")
endif()

if(NOT EXISTS "${WORKDIR}/accel.csv")
    message(FATAL_ERROR "raw_decode.py --csv wrote nothing")
endif()

file(READ "${WORKDIR}/accel.csv" CSV)

# The header must offer each field all three ways, because the frame does not say
# which member of the union it is and choosing would be interpreting.
if(NOT CSV MATCHES "f0_f,f0_u,f0_i")
    message(FATAL_ERROR
        "the CSV does not emit fields as float, uint and int:\n"
        "the frame does not say which a field is, so the decoder must not choose")
endif()

# --kinds must label what the SDK's own parser says each field is *meant* to be.
if(NOT CSV MATCHES "f0=X:Float")
    message(FATAL_ERROR
        "--kinds did not label the accelerometer's fields from the generated "
        "type table")
endif()

# And there must be many more sample rows than there were batches -- the whole
# point is that a batch is not the unit of record.
string(REGEX MATCHALL "\n0," ROWS "${CSV}")
list(LENGTH ROWS N_ROWS)
if(N_ROWS LESS 100)
    message(FATAL_ERROR
        "only ${N_ROWS} accelerometer sample rows decoded; the fixture delivers "
        "~48 Hz for six minutes, so this is not a whole run")
endif()
message(STATUS "raw round trip: ${N_ROWS} accelerometer sample rows from chunk 0")

# ---------------------------------------------------------------------------
# A run with capture off is a valid run, not a broken one
# ---------------------------------------------------------------------------
#
# The existence sweep is run 1, and it writes no raw chunks by design: it
# connects and immediately lets go, so a chunk file for it would be a header and
# nothing else. Its manifest must say capture was off rather than say nothing.

file(READ "${WORKDIR}/fw-1.4.0/runs/1.json" SWEEP)
if(NOT SWEEP MATCHES "\"capture\":false")
    message(FATAL_ERROR
        "the existence sweep's manifest does not record that raw capture was "
        "off for it:\n${SWEEP}")
endif()

message(STATUS "raw decode round trip OK")

# Write two real profiles with the real writers, then read them with the real
# host scripts. Fails if either step fails, or if the tools do not report the
# things the fixtures deliberately contain -- a script that parses a file and
# says nothing useful about it has not been tested.

file(MAKE_DIRECTORY "${WORKDIR}")

execute_process(COMMAND "${EXPORT}" "${WORKDIR}"
                RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "profile-export failed (${rc}):\n${out}\n${err}")
endif()

set(P140 "${WORKDIR}/fw-1.4.0/profile-1.4.0.json")
set(P150 "${WORKDIR}/fw-1.5.0/profile-1.5.0.json")

foreach(p "${P140}" "${P150}")
    if(NOT EXISTS "${p}")
        message(FATAL_ERROR "profile-export did not write ${p}")
    endif()
endforeach()

# ---------------------------------------------------------------------------
# profile_report.py
# ---------------------------------------------------------------------------

execute_process(COMMAND "${PYTHON}" "${REPORT}" "${P140}"
                RESULT_VARIABLE rc OUTPUT_VARIABLE report ERROR_VARIABLE err)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "profile_report.py failed (${rc}):\n${report}\n${err}")
endif()

# The primary key has to be in the report, prominently: a profile whose firmware
# version is unknown cannot be diffed, and one that can be diffed but does not
# say which firmware it is is worse -- it looks usable.
if(NOT report MATCHES "1\\.4\\.0")
    message(FATAL_ERROR "report does not name the firmware:\n${report}")
endif()

# Completeness, alongside the results, always. A report that showed findings
# without showing how much is missing would read as finished.
if(NOT report MATCHES "[Cc]ompleteness")
    message(FATAL_ERROR "report does not state completeness:\n${report}")
endif()

# The fixture's battery channel is stuck at 100 % for the whole run, which is
# ledger row S18's failure mode. The report must call it out: absent, silent and
# stuck are three different findings.
if(NOT report MATCHES "stuck")
    message(FATAL_ERROR
        "report did not surface the fixture's stuck field:\n${report}")
endif()

# The fixture's SPO2 and HEART_BEAT resolve no driver. A negative result is a
# result, and the report has to carry it rather than omitting the sensor.
if(NOT report MATCHES "no producer|no driver|does not resolve")
    message(FATAL_ERROR
        "report did not surface a type with no producer:\n${report}")
endif()

# The six types the SDK's own documentation does not mention -- finding number
# one, and the cheapest thing in this project for UNA to act on.
if(NOT report MATCHES "SensorsLayer|undocumented|missing from")
    message(FATAL_ERROR
        "report did not surface the doc/header divergence:\n${report}")
endif()

# The reader's to-do list. A profile that is 40 % complete and says so is a
# useful document; one that is 40 % complete and looks finished is a liability.
if(NOT report MATCHES "UNVERIFIED")
    message(FATAL_ERROR
        "report does not list what is still unverified:\n${report}")
endif()

# ---------------------------------------------------------------------------
# profile_diff.py
# ---------------------------------------------------------------------------

execute_process(COMMAND "${PYTHON}" "${DIFF}" "${P140}" "${P150}"
                RESULT_VARIABLE rc OUTPUT_VARIABLE diff ERROR_VARIABLE err)
# A diff that finds changes is the expected outcome here, and the tool signals
# that with exit code 1 so it can be used in a gate. Anything above that is a
# real failure.
if(rc GREATER 1)
    message(FATAL_ERROR "profile_diff.py failed (${rc}):\n${diff}\n${err}")
endif()

# The second fixture delivers the accelerometer in a four-field frame where the
# first delivered three, which is exactly the change `RUNNING_CADENCE`'s
# 4 -> 2 shrink says is not hypothetical. The diff must find it, keyed on
# claim_id.
if(NOT diff MATCHES "0x10\\.frame\\.field_count")
    message(FATAL_ERROR
        "diff did not find the field-count change:\n${diff}")
endif()

# ...and it must say the conformance verdict moved, because that is the half a
# reader acts on.
if(NOT diff MATCHES "conformance|MATCHES|DIFFERS")
    message(FATAL_ERROR
        "diff did not report the conformance change:\n${diff}")
endif()

# A profile diffed against itself must be empty, and must exit 0. Without this,
# a tool that reported everything as changed would pass every assertion above.
execute_process(COMMAND "${PYTHON}" "${DIFF}" "${P140}" "${P140}"
                RESULT_VARIABLE rc OUTPUT_VARIABLE same ERROR_VARIABLE err)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR
        "a profile diffed against itself should report no changes (${rc}):\n${same}\n${err}")
endif()

message(STATUS "profile report and diff round trip OK")

# Write a synthetic probe night with the real writer, then parse it with the
# real host script. Fails if either step fails, or if the script's report does
# not mention the awkward cases the fixture deliberately contains -- a script
# that parses a file and says nothing useful about it has not been tested.

file(MAKE_DIRECTORY "${WORKDIR}")

execute_process(COMMAND "${EXPORT}" "${WORKDIR}"
                RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "probe-log-export failed (${rc}):\n${out}\n${err}")
endif()

execute_process(COMMAND "${PYTHON}" "${SCRIPT}" "${WORKDIR}/probe_log.csv"
                RESULT_VARIABLE rc OUTPUT_VARIABLE report ERROR_VARIABLE err)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "probe_report.py failed (${rc}):\n${report}\n${err}")
endif()

# The fixture contains a twelve-minute delivery hole, so the script must say
# so. This is the assertion that matters: the parse succeeding proves the
# columns line up, and this proves the script does something with them.
if(NOT report MATCHES "gap\\(s\\) longer than 90 s")
    message(FATAL_ERROR
        "report did not detect the fixture's delivery gap:\n${report}")
endif()

# ...and it must report the expected HEART_BEAT answer rather than the alarm.
if(NOT report MATCHES "consistent with PR #167")
    message(FATAL_ERROR
        "report did not reach the expected HEART_BEAT verdict:\n${report}")
endif()

if(NOT report MATCHES "2 launch")
    message(FATAL_ERROR
        "report did not split the file into its two launches:\n${report}")
endif()

message(STATUS "probe report round trip OK")

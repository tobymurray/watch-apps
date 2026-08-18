# Write real nights with the real writer, then read them with the real host
# script. Fails if either step fails, or if the report does not reach the
# conclusions the fixture was built to produce -- a script that parses a file
# and says nothing useful about it has not been tested.

file(REMOVE_RECURSE "${WORKDIR}")
file(MAKE_DIRECTORY "${WORKDIR}")

execute_process(COMMAND "${EXPORT}" "${WORKDIR}"
                RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "night-log-export failed (${rc}):\n${out}\n${err}")
endif()

# -- thresholds --------------------------------------------------------------
execute_process(COMMAND "${PYTHON}" "${SCRIPT}" thresholds
                        --worn "${WORKDIR}/worn" --table "${WORKDIR}/table"
                RESULT_VARIABLE rc OUTPUT_VARIABLE report ERROR_VARIABLE err)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "night_report.py thresholds failed (${rc}):\n${report}\n${err}")
endif()

# The fixture's worn and table distributions are built to separate, so the
# suggestion must land on its interesting branch rather than its error path.
if(NOT report MATCHES "the distributions separate")
    message(FATAL_ERROR
        "thresholds did not separate wrist from table on a fixture built to:\n${report}")
endif()
if(NOT report MATCHES "kMicroMovementFloor")
    message(FATAL_ERROR "thresholds named no constants:\n${report}")
endif()

# -- diary -------------------------------------------------------------------
execute_process(COMMAND "${PYTHON}" "${SCRIPT}" diary "${WORKDIR}/worn"
                        --diary "${WORKDIR}/diary.csv"
                RESULT_VARIABLE rc OUTPUT_VARIABLE report ERROR_VARIABLE err)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "night_report.py diary failed (${rc}):\n${report}\n${err}")
endif()

# The fixture's diary dates are derived from the night timestamps, so all
# three must pair. Asserting on the count rather than on the heading, because
# the heading prints whether or not anything matched -- which is how a fixture
# whose dates had drifted a year passed this test once already.
if(NOT report MATCHES "SLEEP ONSET vs lights-out \\(n=3\\)")
    message(FATAL_ERROR
        "diary matched fewer than the 3 nights in the fixture:\n${report}")
endif()
if(NOT report MATCHES "FINAL WAKE vs diary wake \\(n=3\\)")
    message(FATAL_ERROR "diary matched no final wakes:\n${report}")
endif()

# The errors have to be minutes, not most of a day. The diary's date is the
# evening, so waking is the next calendar day -- get that wrong and every
# final-wake error is ~24 hours out, which is the same offset every night and
# so leaves the spread looking perfectly healthy.
if(NOT report MATCHES "mean signed error [-+][0-9]?[0-9]\\.[0-9] min")
    message(FATAL_ERROR
        "an error is bigger than two digits of minutes; the diary's wake time "
        "is probably being read on the wrong day:\n${report}")
endif()
if(report MATCHES "with no diary entry")
    message(FATAL_ERROR "a fixture night went unmatched:\n${report}")
endif()
# Three nights is short of the ten the ledger asks for, and the script has to
# say so rather than quoting an accuracy figure off three.
if(NOT report MATCHES "of the 10 nights the ledger asks for")
    message(FATAL_ERROR
        "diary quoted an accuracy number off three nights:\n${report}")
endif()

message(STATUS "night report round trip OK")

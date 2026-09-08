# Drives the real report generator over the committed fixture run and checks
# the numbers the fixture put in come back out.
#
# It is a round trip rather than a unit test because what it protects is the
# join between the writer and the reader: a renamed column, a reordered field
# or a response word spelled differently would leave every arithmetic test
# green and the report empty.

foreach(required ANALYSE FIXTURES WORK)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is not set")
    endif()
endforeach()

set(RUN "${FIXTURES}/20260908T101500")
if(NOT EXISTS "${RUN}/trials.csv")
    message(FATAL_ERROR "the fixture run is missing; run Tests/fixtures/make_fixture.py")
endif()

file(MAKE_DIRECTORY "${WORK}")
set(REPORT "${WORK}/REPORT.md")

execute_process(
    COMMAND "${ANALYSE}" "${RUN}"
    OUTPUT_FILE "${REPORT}"
    ERROR_VARIABLE stderr
    RESULT_VARIABLE status
)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "hapticlab-analyse failed (${status}): ${stderr}")
endif()

file(READ "${REPORT}" report)

# What the fixture was built to produce. Each one is a different path through
# the tool, named so a failure says which.
set(expectations
    "20260908T101500"           "the run id is missing from the report"
    "wrist, unmasked"           "the condition is missing"
    "**3 patterns.**"           "the distinguishable set is not the three the clean triple earns"
    "dropped for want of trials" "the undersampled pair was not reported as undersampled"
    "not reported (6 of 12 trials)" "the thin pair's shortfall is not stated"
    "6 more needed"             "the report does not say how many more trials the pair needs"
    "during-playback"           "the discarded presses are missing"
    "n=1"                       "the report does not state the single-wrist limitation"
    "Information transfer"      "the information transfer is missing"
    "P01 / P02"                 "the pairwise table is missing"
    "P04 TWOTICK"               "a pattern is missing from the per-pattern table"
)

list(LENGTH expectations count)
math(EXPR last "${count} - 1")
foreach(i RANGE 0 ${last} 2)
    list(GET expectations ${i} needle)
    math(EXPR j "${i} + 1")
    list(GET expectations ${j} why)
    string(FIND "${report}" "${needle}" at)
    if(at EQUAL -1)
        message(FATAL_ERROR "${why}: no \"${needle}\" in ${REPORT}")
    endif()
endforeach()

# The miss and the no-answer are counted separately, because they are different
# results and one column would hide it.
string(REGEX MATCH "of which ([0-9]+) were answered with the miss key and ([0-9]+) closed"
       matched "${report}")
if(NOT matched)
    message(FATAL_ERROR "the miss and no-answer counts are not both reported")
endif()
if(NOT CMAKE_MATCH_1 EQUAL 2)
    message(FATAL_ERROR "expected 2 misses, the report says ${CMAKE_MATCH_1}")
endif()
if(NOT CMAKE_MATCH_2 EQUAL 1)
    message(FATAL_ERROR "expected 1 closed window, the report says ${CMAKE_MATCH_2}")
endif()

# A control and the data it controls are separate reports, so pooling them has
# to be refused rather than averaged.
file(MAKE_DIRECTORY "${WORK}/control")
file(READ "${RUN}/session.json" control_session)
string(REPLACE "\"contact\": \"wrist\"" "\"contact\": \"table\"" control_session
       "${control_session}")
file(WRITE "${WORK}/control/session.json" "${control_session}")
foreach(leaf trials.csv events.csv patterns.csv)
    configure_file("${RUN}/${leaf}" "${WORK}/control/${leaf}" COPYONLY)
endforeach()

execute_process(
    COMMAND "${ANALYSE}" "${RUN}" "${WORK}/control"
    OUTPUT_QUIET
    ERROR_VARIABLE pooled_error
    RESULT_VARIABLE pooled_status
)
if(pooled_status EQUAL 0)
    message(FATAL_ERROR "a no-contact control was pooled with a wrist run instead of refused")
endif()
string(FIND "${pooled_error}" "refusing to pool" at)
if(at EQUAL -1)
    message(FATAL_ERROR "the refusal does not say why: ${pooled_error}")
endif()

# And a control on its own has to say whether it came out above chance, which
# is the whole point of running it.
execute_process(
    COMMAND "${ANALYSE}" "${WORK}/control"
    OUTPUT_FILE "${WORK}/CONTROL.md"
    RESULT_VARIABLE control_status
)
if(NOT control_status EQUAL 0)
    message(FATAL_ERROR "the control run alone did not analyse")
endif()
file(READ "${WORK}/CONTROL.md" control_report)
string(FIND "${control_report}" "Above chance with the watch off the wrist" at)
if(at EQUAL -1)
    message(FATAL_ERROR "a control that is plainly above chance was not called out")
endif()

message(STATUS "report round trip: ${REPORT}")

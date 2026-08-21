# Helper macros to convert spaces into question marks and back again
e := 
sp := $(e) $(e)
qs = $(subst ?,$(sp),$1)
sq = $(subst $(sp),?,$1)

# Get name of this Makefile (avoid getting word 0 and a starting space)
makefile_name := $(wordlist 1,1000,$(MAKEFILE_LIST))

# Get path of this Makefile
makefile_path := $(call qs,$(dir $(call sq,$(abspath $(call sq,$(makefile_name))))))

# Get path where the Application is
application_path := $(call qs,$(abspath $(call sq,$(makefile_path)../..)))


.PHONY: clean assets all

ifneq ($(words $(makefile_path))$(words $(MAKEFILE_LIST)),11)
all clean assets:
$(error Spaces not allowed in path)
else

define replace_car
$(subst $(3),$(2),$(1))
endef

#Change symbol \ to /
SDK := $(call replace_car,$(UNA_SDK),/,\)
#Add path seach source files
VPATH := $(SDK)\
		 ../../

#If you want to add a source file from UNA-SDK, just write the path starting with the internal UNA-SDK folder
# ---------------------------------------------------------------------------
# What a simulator run of this app is, and is not
#
# **The simulator resolves no sensor drivers for a service, so a run of this app
# resolves 0 of 37 types.** Measured: linking the whole simulator sensor layer --
# `KernelMessageDispatcher.cpp`, `SensorManager.cpp`, `InstanceSensorLayer.cpp`
# and all eleven simulated drivers, exactly as `Docs/Tutorials/Sensors` does --
# changed nothing. Every `RequestDefault` still went unanswered and the log still
# filled with `App.DualAppComm::sendMessage ... Queue is full`. So those sources
# are not linked here: dead code that implies a capability is worse than an
# honest absence.
#
# This is the same class of limitation as SleepLab's ledger row T5, where the
# simulator turned out not to deliver `COMMAND_APP_NOTIF_GUI_RUN` to a service at
# all. See `SensorLab/Docs/FINDINGS.md`.
#
# What a simulator run therefore *does* exercise, and does exercise properly:
# the settings reader, the manifest, the 37-type sweep's own code path, the claim
# store and its promotion rules, the run log's append discipline, the profile
# writer, the roster burst and the screen. It ran end to end and wrote a
# `profile-unknown.json` with 40 log rows. That is most of the app.
#
# What it says about a sensor is **nothing**, and nothing in `Docs/LEDGER.md` is
# ever sourced from it. Even if the drivers did resolve, the simulator's
# `SampleRateAdapter` thins delivery on a half-period boundary in quantised
# bands, which the hardware demonstrably does not do -- ledger row S3 measured
# ~48 Hz delivered against 25 Hz requested, nearly double and in the opposite
# direction.
#
# ---------------------------------------------------------------------------
# **This list is enumerated, and the ARM build globs.**
#
# That disagreement is SleepLab's ledger row P14 and it has bitten twice: a new
# file under `Libs/Sources/` packs into a `.uapp` and then fails to link the
# simulator, because only one of the two builds went looking for it. Add every
# new source here in the same commit, and run all three builds before believing a
# branch -- `Tools/docker-build.sh app`, `tests` and `sim`.
ADDITIONAL_SOURCES_UNA :=\
../../Libs/Sources/Catalogue.cpp\
../../Libs/Sources/Service.cpp\
../../Libs/Sources/Settings.cpp\
../../Libs/Sources/Evidence/ClaimStore.cpp\
../../Libs/Sources/Probes/SensorBus.cpp\
../../Libs/Sources/Profile/Decimal.cpp\
../../Libs/Sources/Profile/Manifest.cpp\
../../Libs/Sources/Profile/ProfileWriter.cpp\
../../Libs/Sources/Profile/RawLog.cpp\
../../Libs/Sources/Profile/RunLog.cpp\
../../Libs/Sources/Stats/StreamStats.cpp\
Libs/Source/JSON/JsonStreamReader.cpp\
Libs/Source/Simulator/Kernel/Mock/FileSystem.cpp\
Libs/Source/JSON/JsonStreamWriter.cpp\
Libs/Source/Simulator/OS/OS.cpp\
Libs/Source/UnaLogger/Logger.cpp\
Libs/Source/Wrappers/StdLibWrappers.c\
Libs/Source/Kernel/KernelBuilder.cpp\
Libs/Source/Port/TouchGFX/TouchGFXCommandProcessor.cpp\
Libs/Source/Simulator/App/DualAppComm.cpp\
Libs/Source/Simulator/App/AppCore.cpp\
Libs/Source/Simulator/App/AppMessageCore.cpp\
Libs/Source/Simulator/App/MessageManager.cpp\
Libs/Source/Simulator/Kernel/Kernel.cpp\
Libs/Source/Simulator/Kernel/Mock/System.cpp
ADDITIONAL_SOURCES := $(SDK)/ThirdParty/coreJSON/source/core_json.c
ADDITIONAL_INCLUDE_PATHS := $(SDK)/Libs/Header\
$(SDK)/ThirdParty/coreJSON/source/include\
../../Libs/Header
ADDITIONAL_LIBRARY_PATHS :=
ADDITIONAL_LIBRARIES :=

export ADDITIONAL_SOURCES ADDITIONAL_INCLUDE_PATHS ADDITIONAL_LIBRARY_PATHS ADDITIONAL_LIBRARIES ADDITIONAL_SOURCES_UNA SDK VPATH

all: $(filter assets,$(MAKECMDGOALS))
all assets: $(filter clean,$(MAKECMDGOALS))
all clean assets:
	@$(MAKE) -r -f una/Makefile -s $(MFLAGS) $@ -C "$(application_path)"
endif
cmake_minimum_required(VERSION 3.25)
project(MultiPandOS LANGUAGES C ASM)

set(XT_PRG_PREFIX riscv64-unknown-elf-)
set(URISCV_SRC /usr/local/share/uriscv)
set(URISCV_INC /usr/include)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_C_COMPILER ${XT_PRG_PREFIX}gcc)
set(CMAKE_ASM_COMPILER ${XT_PRG_PREFIX}gcc)
set(CMAKE_C_LINKER ${XT_PRG_PREFIX}ld)

add_link_options(-nostdlib -T ${URISCV_SRC}/uriscvcore.ldscript -march=rv32imfd)
add_compile_options(-ffreestanding -static -nostartfiles -nostdlib -I${URISCV_INC} -ggdb -Wall -O0 -std=gnu99 -march=rv32imafd -mabi=ilp32d)
include_directories(/usr/include)

# Which phase to build. Pass -DPHASE=PHASE1, -DPHASE=PHASE2 or -DPHASE=PHASE3
# to cmake. The default is PHASE3 (the full Support-level kernel).
set(PHASE "PHASE3" CACHE STRING "Which phase to build (PHASE1, PHASE2, PHASE3)")
set_property(CACHE PHASE PROPERTY STRINGS PHASE1 PHASE2 PHASE3)

if(PHASE STREQUAL "PHASE1")
    set(PANDOS_SOURCES
        ./klog.c
        phase1/pcb.c phase1/asl.c phase1/p1test.c)
elseif(PHASE STREQUAL "PHASE2")
    set(PANDOS_SOURCES
        ./klog.c
        phase1/pcb.c phase1/asl.c
        phase2/init.c phase2/exceptions.c phase2/interrupts.c
        phase2/scheduler.c phase2/syscalls.c phase2/p2test.c)
elseif(PHASE STREQUAL "PHASE3")
    set(PANDOS_SOURCES
        ./klog.c
        phase1/pcb.c phase1/asl.c
        phase2/init.c phase2/exceptions.c phase2/interrupts.c
        phase2/scheduler.c phase2/syscalls.c
        phase3/vmSupport.c phase3/sysSupport.c phase3/initProc.c)
else()
    message(FATAL_ERROR "PHASE must be PHASE1, PHASE2, or PHASE3 (got '${PHASE}')")
endif()

add_executable(MultiPandOS
    ${PANDOS_SOURCES}
    ${URISCV_SRC}/crtso.S ${URISCV_SRC}/liburiscv.S)

add_custom_target(
	MultiPandOSuRISCV ALL
	COMMAND uriscv-elf2uriscv -k ${PROJECT_BINARY_DIR}/MultiPandOS
	BYPRODUCTS MultiPandOS.core.uriscv MultiPandOS.stab.uriscv
	DEPENDS MultiPandOS
)
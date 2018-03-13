# valve controller makefile
# equivalent to:
# g++ -O3 -o valve_controller valve_controller.cpp vo_alias.cc netutils.cc pthread_event.cc aioUsbApi.c configuration.cpp maccompat.cc utils.cc rs232.c flow_controller.cpp -lusb-1.0 -lrt

CC = g++
OUTPUTNAME = ~/executables/valve_controller
COMMON = ~/git/source_code/common
INCLUDE = -I ${COMMON}
LIBS = -lusb-1.0 -lrt -lpthread
CFLAGS_COMMON = -O3 -Wall
#CFLAGS_COMMON = -g -Wall

#OUTDIR = ../../bin

OBJS_COMMON = valve_controller.o ${COMMON}/netutils.o ${COMMON}/pthread_event.o ${COMMON}/aioUsbApi.o configuration.o ${COMMON}/maccompat.o ${COMMON}/utils.o ${COMMON}/rs232.o flow_controller.o
OBJS_BEHAVIOR = vo_alias_behavior.o
OBJS_PHYSIOLOGY = vo_alias_physiology.o
DEFS_BEHAVIOR = -D BEHAVIOR
DEFS_PHYSIOLOGY = -D PHYSIOLOGY

ifneq (,$(filter physiology,${MAKECMDGOALS}))
	CFLAGS = ${CFLAGS_COMMON} ${DEFS_PHYSIOLOGY}
	OBJS = ${OBJS_COMMON} ${OBJS_PHYSIOLOGY}
else ifneq (,$(filter behavior,${MAKECMDGOALS}))
	CFLAGS = ${CFLAGS_COMMON} ${DEFS_BEHAVIOR}
	OBJS = ${OBJS_COMMON} ${OBJS_BEHAVIOR}
endif

default:
	@echo ERROR: please specify the target to make '(make physiology or make behavior)'

physiology: clean ${OUTPUTNAME}

behavior: clean ${OUTPUTNAME} 

${OUTPUTNAME}: ${OBJS}
	@echo [*] Linking...
	@${CC} -g -o ${OUTPUTNAME} ${OBJS} ${LIBS} ${COMMON_LIBS} 
#ifneq (${D}, d)
#	strip --strip-unneeded ${OUTPUTNAME}
#endif
#	mv ${OUTPUTNAME} ${OUTDIR}

%.o: %.cpp
	@echo [*] Compiling $<
	${CC} -o $@ ${CFLAGS} ${INCLUDE} -c $*.cpp

%.o: %.cc
	@echo [*] Compiling $<
	@${CC} -o $@ ${CFLAGS} ${INCLUDE} -c $*.cc

%.o: %.c
	@echo [*] Compiling $<
	@${CC} -o $@ ${CFLAGS} ${INCLUDE} -c $*.c
	
clean_obj:
	rm -f ${OBJS}
	@echo "all cleaned up!"

clean:
#	rm -f ${OUTDIR}/${OUTPUTNAME} ${OBJS}	@echo "all cleaned up!"
	@rm -f ${OUTPUTNAME} ${OBJS}
	@echo "all cleaned up!"


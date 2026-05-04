bin_PROGRAMS = m6809-run exec09-monitor
bin_SCRIPTS = wpc-run

m6809_run_SOURCES = \
	6809.c main.c monitor.c machine.c eon.c wpc.c miscsbc.c \
	symtab.c command.c fileio.c wpclib.c imux.c \
	ioexpand.c mmu.c timer.c serial.c disk.c \
	6809.h config.h eon.h machine.h monitor.h wpclib.h \
	command.h symtab.h imux.h ioexpand.h mmu.h timer.h serial.h

m6809_run_LDADD = $(READLINE_LIBS)

exec09_monitor_SOURCES = \
	hostmon_main.c hostmon.c hostmon.h

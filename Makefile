## $XTermId: Makefile.in,v 1.137 2006/11/30 00:03:48 tom Exp $
##
## $XFree86: xc/programs/xterm/Makefile.in,v 3.56 2006/06/19 00:36:50 dickey Exp $ ##
##
## Copyright 2002-2005,2006 by Thomas E. Dickey
##
##                         All Rights Reserved
##
## Permission to use, copy, modify, and distribute this software and its
## documentation for any purpose and without fee is hereby granted,
## provided that the above copyright notice appear in all copies and that
## both that copyright notice and this permission notice appear in
## supporting documentation, and that the name of the above listed
## copyright holder(s) not be used in advertising or publicity pertaining
## to distribution of the software without specific, written prior
## permission.
##
## THE ABOVE LISTED COPYRIGHT HOLDER(S) DISCLAIM ALL WARRANTIES WITH REGARD
## TO THIS SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
## AND FITNESS, IN NO EVENT SHALL THE ABOVE LISTED COPYRIGHT HOLDER(S) BE
## LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
## WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
## ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
## OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

SHELL		= /bin/sh

#### Start of system configuration section. ####

srcdir		= .


x		= 
o		= .o

CC		= tcc
CPP		= tcc -E
AWK		= nawk
LINK		= $(CC) $(CFLAGS)

LN_S		= ln -s
RM              = rm -f
LINT		= 

INSTALL		= /usr/bin/install -c
INSTALL_PROGRAM	= ${INSTALL}
INSTALL_SCRIPT	= ${INSTALL}
INSTALL_DATA	= ${INSTALL} -m 644
transform	= s,x,x,

EXTRA_CFLAGS	= 
EXTRA_CPPFLAGS	= 
EXTRA_LOADFLAGS	= 

CPPFLAGS	= -I. -I$(srcdir) -DHAVE_CONFIG_H  -I/usr/local/include -D_THREAD_SAFE -I/usr/local/include/freetype2 -I/usr/local/include/libpng16  -DNARROWPROTO=1 -DFUNCPROTO=15 -DOSMAJORVERSION=13 -DOSMINORVERSION=4  -D_BSD_TYPES -D__BSD_VISIBLE -D_POSIX_C_SOURCE=200112L -D_XOPEN_SOURCE=600 $(EXTRA_CPPFLAGS)
CFLAGS		=  $(EXTRA_CFLAGS)
LDFLAGS		=  
LIBS		=  -L/usr/local/lib -lXft -lutil -lXaw -lXmu -lXext -lXt  -lSM -lICE -lX11  -ltermcap   -lfontconfig

prefix		= /usr/local
exec_prefix	= ${prefix}

manext		= 1
bindir		= ${exec_prefix}/bin
libdir		= ${exec_prefix}/lib
mandir		= ${prefix}/man/man$(manext)
appsdir		= '$(exec_prefix)/lib/X11/app-defaults'

#### End of system configuration section. ####

DESTDIR		=
BINDIR		= $(DESTDIR)$(bindir)
LIBDIR		= $(DESTDIR)$(libdir)
MANDIR		= $(DESTDIR)$(mandir)
APPSDIR		= $(DESTDIR)$(appsdir)

INSTALL_DIRS    = $(BINDIR) $(APPSDIR) $(MANDIR)

CLASS		= XTerm
EXTRAHDR	=  charclass.h precompose.h wcwidth.h
EXTRASRC	=  charclass.c precompose.c wcwidth.c
EXTRAOBJ	=  charclass.o precompose.o wcwidth.o

          SRCS1 = button.c charproc.c charsets.c cursor.c \
	  	  data.c doublechr.c fontutils.c input.c \
		  $(MAINSRC) menu.c misc.c print.c ptydata.c \
		  screen.c scrollbar.c tabs.c util.c xstrings.c \
		  VTPrsTbl.c $(EXTRASRC)
          OBJS1 = button$o charproc$o charsets$o cursor$o \
	  	  data$o doublechr$o fontutils$o input$o \
		  main$o menu$o misc$o print$o ptydata$o \
		  screen$o scrollbar$o tabs$o util$o xstrings$o \
		  VTPrsTbl$o $(EXTRAOBJ)
          SRCS2 = resize.c xstrings.c
          OBJS2 = resize$o xstrings$o
           SRCS = $(SRCS1) $(SRCS2)
           OBJS = $(OBJS1) $(OBJS2)
           HDRS = VTparse.h data.h error.h main.h menu.h proto.h \
                  ptyx.h version.h xstrings.h xterm.h $(EXTRAHDR)
       PROGRAMS = xterm$x resize$x

all :	$(PROGRAMS)

.SUFFIXES : .i .def .hin

.c$o :
# compiling
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $(srcdir)/$*.c

.c.i :
# compiling
	$(CPP) -C $(CPPFLAGS) $*.c >$@

.def.hin :
	grep '^CASE_' $< | $(AWK) '{printf "#define %s %d\n", $$1, n++}' >$@

main$o : main.h
misc$o : version.h

$(OBJS1) : xterm.h ptyx.h xtermcfg.h
main$o resize$o screen$o : xterm_io.h

xterm$x : $(OBJS1)
	$(LINK) $(LDFLAGS) -o $@ $(OBJS1) $(LIBS) $(EXTRA_LOADFLAGS)

resize$x : $(OBJS2)
	$(SHELL) $(srcdir)/plink.sh $(LINK) $(LDFLAGS) -o $@ $(OBJS2) $(LIBS)

256colres.h :
	-rm -f $@
	perl $(srcdir)/256colres.pl > $@

88colres.h :
	-rm -f $@
	perl $(srcdir)/88colres.pl > $@

charproc$o : main.h 

actual_xterm  = `echo xterm|    sed '$(transform)'`
actual_resize = `echo resize|   sed '$(transform)'`
actual_uxterm = `echo uxterm|   sed '$(transform)'`

binary_xterm  = $(actual_xterm)$x
binary_resize = $(actual_resize)$x
binary_uxterm = $(actual_uxterm)

install \
install-bin \
install-full :: xterm$x resize$x $(BINDIR)
	$(SHELL) $(srcdir)/sinstall.sh  "$(INSTALL_PROGRAM)" xterm$x   $(BINDIR)/$(binary_xterm)
#	$(INSTALL_PROGRAM) xterm$x $(BINDIR)/$(binary_xterm)
	$(INSTALL_PROGRAM) -m  755 resize$x $(BINDIR)/$(binary_resize)
	@$(SHELL) -c 'echo "... installing $(BINDIR)/$(binary_uxterm)"; \
		if test "$(binary_xterm)" != "xterm"; then \
			name="$(binary_xterm)"; \
			sed -e "s,=xterm,=$$name," $(srcdir)/uxterm >uxterm.tmp; \
			$(INSTALL_SCRIPT) -m  755 uxterm.tmp $(BINDIR)/$(binary_uxterm); \
			rm -f uxterm.tmp; \
		else \
			$(INSTALL_SCRIPT) -m  755 $(srcdir)/uxterm $(BINDIR)/$(binary_uxterm); \
		fi'
	@-$(SHELL) -c "(test NONE != NONE && cd $(BINDIR) && rm -f NONE) || exit 0"
	@-$(SHELL) -c "(test NONE != NONE && cd $(BINDIR) && $(LN_S) $(binary_xterm) NONE) || exit 0"
	@-$(SHELL) -c "(test NONE != NONE && cd $(BINDIR) && echo '... created symbolic link:' && ls -l $(binary_xterm) NONE) || exit 0"

install \
install-man \
install-full :: $(MANDIR)
	$(SHELL) $(srcdir)/minstall.sh "$(INSTALL_DATA)" $(srcdir)/xterm.man    $(MANDIR)/$(actual_xterm).$(manext)  $(appsdir)
	$(SHELL) $(srcdir)/minstall.sh "$(INSTALL_DATA)" $(srcdir)/resize.man   $(MANDIR)/$(actual_resize).$(manext) $(appsdir)
	@-$(SHELL) -c "(test NONE != NONE && cd $(MANDIR) && rm -f NONE.$(manext)) || exit 0"
	@-$(SHELL) -c "(test NONE != NONE && cd $(MANDIR) && $(LN_S) $(actual_xterm).$(manext) NONE.$(manext)) || exit 0"
	@-$(SHELL) -c "(test NONE != NONE && cd $(MANDIR) && echo '... created symbolic link:' && ls -l $(actual_xterm).$(manext) NONE.$(manext)) || exit 0"

install \
install-app \
install-full :: $(APPSDIR)
	@echo installing $(APPSDIR)/$(CLASS)
	@sed -e s/XTerm/$(CLASS)/ $(srcdir)/XTerm.ad >XTerm.tmp
	@$(INSTALL_DATA) XTerm.tmp $(APPSDIR)/$(CLASS)
	@echo installing $(APPSDIR)/$(CLASS)-color
	@sed -e s/XTerm/$(CLASS)/ $(srcdir)/XTerm-col.ad >XTerm.tmp
	@$(INSTALL_DATA) XTerm.tmp $(APPSDIR)/$(CLASS)-color
	@echo installing $(APPSDIR)/UXTerm
	@sed -e s/XTerm/$(CLASS)/ $(srcdir)/UXTerm.ad >XTerm.tmp
	@$(INSTALL_DATA) XTerm.tmp $(APPSDIR)/UXTerm
	@rm -f XTerm.tmp

install ::
	@echo 'Completed installation of executables and documentation.'
	@echo 'Use "make install-ti" to install terminfo description.'

TERMINFO_DIR = 
SET_TERMINFO = 

install-full \
install-ti :: $(TERMINFO_DIR)
	$(SET_TERMINFO) $(SHELL) $(srcdir)/run-tic.sh $(srcdir)/terminfo
	@echo 'Completed installation of terminfo description.'

install-full \
install-tc ::
	@test -f /etc/termcap && echo 'You must install the termcap entry manually by editing /etc/termcap'

installdirs : $(INSTALL_DIRS)

uninstall \
uninstall-bin \
uninstall-full ::
	-$(RM) $(BINDIR)/$(binary_xterm)
	-$(RM) $(BINDIR)/$(binary_resize)
	-$(RM) $(BINDIR)/$(binary_uxterm)
	@-$(SHELL) -c "test NONE != NONE && cd $(BINDIR) && rm -f NONE"

uninstall \
uninstall-man \
uninstall-full ::
	-$(RM) $(MANDIR)/$(actual_xterm).$(manext)
	-$(RM) $(MANDIR)/$(actual_resize).$(manext)
	@-$(SHELL) -c "test NONE != NONE && cd $(MANDIR) && rm -f NONE.$(manext)"

uninstall \
uninstall-app \
uninstall-full ::
	-$(RM) $(APPSDIR)/$(CLASS)
	-$(RM) $(APPSDIR)/$(CLASS)-color
	-$(RM) $(APPSDIR)/UXTerm

mostlyclean :
	-$(RM) *$o *.[is] XtermLog.* .pure core *~ *.bak *.BAK *.out *.tmp

clean : mostlyclean
	-$(RM) $(PROGRAMS)

distclean : clean
	-$(RM) Makefile config.status config.cache config.log xtermcfg.h

realclean : distclean
	-$(RM) tags TAGS ctlseqs.ps ctlseqs.txt

maintainer-clean : realclean
	-$(RM) 256colres.h 88colres.h

ctlseqs.html : ctlseqs.ms
	GROFF_NO_SGR=stupid $(SHELL) -c "tbl ctlseqs.ms | groff -Thtml -ms" >$@

ctlseqs.txt : ctlseqs.ms
	GROFF_NO_SGR=stupid $(SHELL) -c "tbl ctlseqs.ms | nroff -Tascii -ms | col -bx" >$@

ctlseqs.ps : ctlseqs.ms
	tbl ctlseqs.ms | groff -ms >$@

lint :
	$(LINT) $(CPPFLAGS) $(SRCS1)
	$(LINT) $(CPPFLAGS) $(SRCS2)

tags :
	ctags $(SRCS) $(HDRS)

TAGS :
	etags $(SRCS) $(HDRS)

$(TERMINFO_DIR) $(INSTALL_DIRS) :
	$(SHELL) ${srcdir}/mkdirs.sh $@

ALWAYS :

depend : $(TABLES)
	makedepend -- $(CPPFLAGS) -- $(SRCS)

# DO NOT DELETE THIS LINE -- make depend depends on it.

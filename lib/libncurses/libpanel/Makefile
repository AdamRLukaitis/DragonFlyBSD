ZPANEL=	yes
LIB=    private_panel${LIB_SUFFIX}

SRCS=	ncurses_def.h \
	p_above.c \
	p_below.c \
	p_bottom.c \
	p_delete.c \
	p_hidden.c \
	p_hide.c \
	p_move.c \
	p_new.c \
	p_replace.c \
	p_show.c \
	p_top.c \
	p_update.c \
	p_user.c \
	p_win.c \
	panel.c

${SRCS:M*.c}: curses.h

INCS=	panel.h

CLEANFILES=	ncurses_def.h curses.h curses.h.new

.include <bsd.lib.mk>

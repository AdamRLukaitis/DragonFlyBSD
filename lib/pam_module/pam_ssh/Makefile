# $DragonFly: src/lib/pam_module/pam_ssh/Makefile,v 1.3 2008/04/07 01:20:18 pavalos Exp $

LIB=	pam_ssh
SRCS=	pam_ssh.c
MAN=	pam_ssh.8

LDADD=	-lprivate_ssh -lcrypto -lcrypt
DPADD=	${LIBSSH} ${LIBCRYPTO} ${LIBCRYPT}

WARNS?=	4
SSHDIR=		${.CURDIR}/../../../crypto/openssh
CFLAGS+=	-I${SSHDIR}
LDFLAGS+=	-rpath /usr/lib/priv -L ${_SHLIBDIRPREFIX}/usr/lib/priv

.include <bsd.lib.mk>

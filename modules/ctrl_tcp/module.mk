#
# module.mk
#
# Copyright (C) 2017 José Luis Millán <jmillan@aliax.net>
#

MOD		:= ctrl_tcp
$(MOD)_SRCS	+= ctrl_tcp.c netstring.c ./netstring-c/netstring.c

include mk/mod.mk

# Copyright 2019-2020 Gentoo Authors
# Distributed under the terms of the GNU General Public License v2

EAPI=7

inherit acct-user

DESCRIPTION="user for RNG daemons"
ACCT_USER_ID=210
ACCT_USER_GROUPS=( rngd )

acct-user_add_deps

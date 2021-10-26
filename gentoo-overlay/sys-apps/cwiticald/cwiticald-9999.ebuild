# Copyright 2019-2021 Gentoo Authors
# Distributed under the terms of the GNU General Public License v2

EAPI=5

inherit eutils libtool git-r3

DESCRIPTION="EGD-compatible daemon for TrueRNG devices"
HOMEPAGE="https://github.com/rodan/cwiticald/"
SRC_URI=""
EGIT_REPO_URI="git://github.com/rodan/cwiticald.git"
EGIT_BRANCH="master"
S="${WORKDIR}/${P}/src"

LICENSE="GPL-3"
SLOT="0"
KEYWORDS="~amd64 ~x86"
IUSE=""

DEPEND="x11-misc/makedepend"
RDEPEND="
	acct-user/rngd
	acct-group/rngd
	>=dev-libs/libevent-2.0.0
"

src_install() {
	exeinto "usr/sbin"
	doexe cwiticald
}

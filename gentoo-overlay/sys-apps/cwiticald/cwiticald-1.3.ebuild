# Copyright 2019-2021 Gentoo Authors
# Distributed under the terms of the GNU General Public License v2

EAPI=5

inherit eutils

DESCRIPTION="EGD-compatible daemon for TrueRNG devices"
HOMEPAGE="https://github.com/rodan/cwiticald/"
SRC_URI="https://github.com/rodan/${PN}/archive/v${PV}.tar.gz -> ${P}.tar.gz"
S="${WORKDIR}/${P}/src"

LICENSE="GPL-3"
SLOT="0"
KEYWORDS="amd64 x86"
IUSE=""

DEPEND="x11-misc/makedepend"
RDEPEND="
	acct-user/rngd
	acct-group/rngd
	>=dev-libs/libevent-2.0.0
"

src_install() {
	doman "${S}/../doc/cwiticald.1"
	exeinto "usr/sbin"
	doexe cwiticald
}

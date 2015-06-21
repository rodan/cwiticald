#!/usr/bin/perl -w



use IO::Socket;

# entropy client

unless ($ARGV[0]) {
    print "Usage: sucker.pl <daemon>\n";
    print "  daemon: host:portnum or /path/to/unix/socket\n";
    print " every 30 seconds, suck as much entropy out of the daemon as we\n";
    print " can. Report the entropy rate that we achieve.\n";
    exit(0);
}

$daemon = shift;

if ($daemon =~ /:/) {
    $s = new IO::Socket::INET('PeerAddr' => $daemon);
} else {
    $s = new IO::Socket::UNIX('Peer' => $daemon);
}
die "couldn't contact daemon: $!" unless $s;

sub count_entropy {
    my $msg = pack("C", 0x00);
    $s->syswrite($msg, length($msg));
    my $nread = $s->sysread($buf, 4);
    die "didn't read all 4 bytes in one shot" if ($nread != 4);
    my $count = unpack("N",$buf);
    return $count; # bits
}

sub read_entropy {
    my $bytes = shift;
    $bytes = 255 if $bytes > 255;
    my $msg = pack("CC", 0x01, $bytes);
    $s->syswrite($msg, length($msg));
    my $nread = $s->sysread($buf, 1);
    die unless $nread == 1;
    my $count = unpack("C",$buf);
    if ($count) {
        $nread = $s->sysread($buf, $count);
        die "didn't get all the entropy" unless $nread == $count;
    }
    return $count;
}

sub suck {
    my $count = 0;
    while(1) {
	my $got;
	$got = read_entropy(255);
	$count += $got;
	last if $got == 0;
    }
    return $count;
}

die "couldn't get anything from the daemon, weird\n" unless count_entropy();

# empty the pool
print "cleaning the pool..\n";
suck();
print "done\n";
$start = time;
$prev = $start;
$bytes = 0;
print "sleeping first time..\n";
sleep(30);
print "done\n";

while (1) {
    my($got,$secs);
    print "sucking..\n";
    $got = suck();
    print "done\n";
    $secs = time - $prev;
    $totalsecs = time - $start;
    $prev = time;
    $bytes += $got;
    if (!$secs) {
	$rate = "[lots]";
    } else {
	$rate = 8*$got / $secs;
    }
    if (!$totalsecs) {
	$totalrate = "[lots]";
    } else {
	$totalrate = 8*$bytes / $totalsecs;
    }

    print `date`;
    print "got $got bytes in $secs secs for $rate bps\n";
    print "  cumulative: $bytes in $totalsecs for $totalrate bps\n";

    print "sleeping..\n";
    sleep(30);
    print "done\n";

}

# on my linux box at home, I get about 30 bps of entropy out of 21 sources
# solaris reports 22 sources available, gets about 50bps at a load of 0.3
#
# the gnupg-0.9.9 'make check' sequence requires 58320 bytes = 466450 bits.
#  (on my solaris box, that means the tests would take 2.6 hours)

# solaris 5.8 (22 sources available) gets about 50bps



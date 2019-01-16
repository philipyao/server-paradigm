#! /usr/bin/perl
#
# to test:
# 1) run this script with either "accept" or "select-accept" as the argument
#    (the script listens to 127.0.0.1:12345)
# 2) telnet localhost 12345
# 3) if you see "accept failed", there is the thundering herd problem
#
#
use strict;
use warnings;
 
use IO::Socket::INET;

my $mode = $ARGV[0] || '';
if ($mode !~ /^(accept|select-accept)$/) {
    die "Usage: $0 <accept|select-accept>\n";
}
 
my $listener = IO::Socket::INET->new(
    Listen => 5,
    LocalPort => 12345,
    LocalAddr => '127.0.0.1',
    Proto => 'tcp',
    ReuseAddr => 1,
) or die "failed to listen to port 127.0.0.1:12345:$!";

if ($mode eq 'select-accept') {
    $listener->blocking(0)
        or die "failed to set listening socket to non-blocking mode:$!";
}
 
my $pid = fork;
die "fork failed:$!"
    unless defined $pid;
 
while (1) {
    if ($mode eq 'select-accept') {
        while (1) {
            my $rfds = '';
            vec($rfds, fileno($listener), 1) = 1;
            if (select($rfds, undef, undef, undef) >= 1) {
                last;
            }
        }
    }
    my $conn = $listener->accept;
    if ($conn) {
        warn "connected!";
        $conn->close;
    } else {
        warn "accept failed:$!";
    }
}

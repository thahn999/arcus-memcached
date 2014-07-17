#!/usr/bin/perl

use strict;
use Test::More;
use FindBin qw($Bin);
use lib "$Bin/../lib";
use MemcachedTest;

my $server = new_memcached_engine("squall", '-M -m 1');
my $sock = $server->sock;

my $value = "B" x 8192;
my $vallen = length($value);

my $resp = "STORED\r\n";
my $key = 0;

while($resp eq "STORED\r\n") {
    print $sock "set dash$key 0 0 $vallen\r\n$value\r\n";
    if (($key % 1000) eq 0) {
        print "set dash$key\n";
    }
    $key++;
    $resp = scalar <$sock>;
}

my $max_stored = $key - 1;

plan tests => $max_stored + 1;

print $sock "set dash$key 0 0 $vallen\r\n$value\r\n";
print "set dash$key\n";
is(scalar <$sock>, "SERVER_ERROR out of memory storing object\r\n",
   "failed to add another one.");

for($key = 0; $key < $max_stored; $key++) {
    mem_get_is $sock, "dash$key", $value, "Failed at dash$key";
}

undef $server;

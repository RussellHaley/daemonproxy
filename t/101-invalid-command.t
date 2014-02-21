#! /usr/bin/env perl

use strict;
use warnings;
use Test::More;
use lib 't/lib';
use Test::DaemonProxy;

my $dp= Test::DaemonProxy->new;

$dp->run('--stdin');

$dp->send("typo");
$dp->response_like(qr/^error:.*typo/);

$dp->send("echo\t-marker-");
$dp->response_like( qr/^-marker-/, 'can still send/receive' );

$dp->send("echo\t-marker-");
$dp->response_like( qr/^-marker-/, 'can still send/receive' );

$dp->send("terminate");
$dp->exit_is(6);

done_testing;
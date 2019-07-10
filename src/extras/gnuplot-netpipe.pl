#!/usr/bin/env perl

use strict;

sub run_gnuplot {
    my ($str) = @_;
    open(G, "|gnuplot") || die "Can't open gnuplot";
    print "================================================================
$str\n";
    print G $str;
    close(G);
}

#############################################################################
# Plot netpipe bandwidths
#############################################################################

my $g = "set terminal pdf 
set output 'netpipe-bandwidths.pdf'
set title 'NetPIPE Bandwidth'
set ylabel 'Bandwidth (Mbps)'
set xlabel 'Message size (bytes)'
set key inside left top vertical Right noreverse enhanced autotitles columnhead nobox
set style data linespoints

set logscale y
set logscale x

plot ";
my $first = 1;
foreach my $a (@ARGV) {
    $g .= ", "
        if (!$first);
    $g .= "'$a' using 1:2 title '$a'";
    $first = 0;
}

run_gnuplot($g);

#############################################################################
# Plot netpipe latencies
#############################################################################

$g = "set terminal pdf 
set output 'netpipe-latencies.pdf'
set title 'NetPIPE Latencies'
set ylabel 'Latency (us)'
set xlabel 'Message size (bytes)'
set key inside left top vertical Right noreverse enhanced autotitles columnhead nobox
set style data linespoints

set logscale y
set logscale x

plot ";
$first = 1;
foreach my $a (@ARGV) {
    $g .= ", "
        if (!$first);
    $g .= "'$a' using 1:3 title '$a'";
    $first = 0;
}

run_gnuplot($g);

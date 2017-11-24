#!/usr/bin/perl

use strict;
use warnings;

use LWP::UserAgent;

#
# THIS SCRIPT IS USELESS AS ONE CAN JUST DO:
#
# curl -v --request POST --data-urlencode "json=$(cat aggregate_output.json)" http://localhost:8000/result/add/json/
#

# You need to enter the real URL and have the server running
my $codespeed_url = 'http://localhost:8000/';

sub add_data {
	my ($data) = @_;

	my $req = LWP::UserAgent->new;
	my $response = $req->post($codespeed_url . 'result/add/json/', Content => {'json' => $data});

	unless ($response->is_success) {
		die $response->status_line;
	}

	print "Server (" . $codespeed_url . ") response: " . $response->decoded_content . "\n";
}

my $json;
{
  local $/; #Enable 'slurp' mode
  $json = <>;
}

# print "JSON read:\n";
# print $json;

add_data($json);

#!/usr/bin/perl

use strict;
use warnings;

use JSON;
use LWP::UserAgent;
use URI::Escape;

# You need to enter the real URL and have the server running
my $codespeed_url = 'http://localhost:8000/';

sub add_data {
	my ($data) = @_;

	my $req = LWP::UserAgent->new;

	my $response = $req->post($codespeed_url . 'result/add/json/', 'json' => uri_escape($data));

	print "URL:\n";
	print $codespeed_url . 'result/add/json/' ."\n";

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

my $json_data = decode_json $json;

my $json_sent = encode_json $json_data;

print "JSON sent begin <<<<\n";
print $json_sent;
print "\n>>>>JSON sent end\n";

add_data($json_sent);

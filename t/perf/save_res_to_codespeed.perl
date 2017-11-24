#!/usr/bin/perl

use strict;
use warnings;

use JSON;
use LWP::UserAgent;
use URI::Escape;

# You need to enter the real URL and have the server running
my $codespeed_url = 'http://localhost:80/';

sub add_data() {
	my ($data) = @_;

	my $req = LWP::UserAgent->new;

	my $response = $req->get($codespeed_url . 'result/add/json/' . uri_escape($data));

	unless ($response->is_success) {
		die $response->status_line;
	}

	print "Server (" . $codespeed_url . ") response: " . $response->decoded_content . "\n";
}

if __name__ == "__main__":
    data = {'json': json.dumps(sample_data)}
    add(data)

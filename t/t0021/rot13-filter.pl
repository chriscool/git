#
# Example implementation for the Git filter protocol version 2
# See Documentation/gitattributes.txt, section "Filter Protocol"
#
# The first argument defines a debug log file that the script write to.
# All remaining arguments define a list of supported protocol
# capabilities ("clean", "smudge", etc).
#
# This implementation supports special test cases:
# (1) If data with the pathname "clean-write-fail.r" is processed with
#     a "clean" operation then the write operation will die.
# (2) If data with the pathname "smudge-write-fail.r" is processed with
#     a "smudge" operation then the write operation will die.
# (3) If data with the pathname "error.r" is processed with any
#     operation then the filter signals that it cannot or does not want
#     to process the file.
# (4) If data with the pathname "abort.r" is processed with any
#     operation then the filter signals that it cannot or does not want
#     to process the file and any file after that is processed with the
#     same command.
# (5) If data with a pathname that is a key in the DELAY hash is
#     requested (e.g. "test-delay10.a") then the filter responds with
#     a "delay" status and sets the "requested" field in the DELAY hash.
#     The filter will signal the availability of this object after
#     "count" (field in DELAY hash) "list_available_blobs" commands.
# (6) If data with the pathname "missing-delay.a" is processed that the
#     filter will drop the path from the "list_available_blobs" response.
# (7) If data with the pathname "invalid-delay.a" is processed that the
#     filter will add the path "unfiltered" which was not delayed before
#     to the "list_available_blobs" response.
#

use strict;
use warnings;
use IO::File;

my $MAX_PACKET_CONTENT_SIZE = 65516;
my $log_file                = shift @ARGV;
my @capabilities            = @ARGV;

open my $debug, ">>", $log_file or die "cannot open log file: $!";

my %DELAY = (
	'test-delay10.a' => { "requested" => 0, "count" => 1 },
	'test-delay11.a' => { "requested" => 0, "count" => 1 },
	'test-delay20.a' => { "requested" => 0, "count" => 2 },
	'test-delay10.b' => { "requested" => 0, "count" => 1 },
	'missing-delay.a' => { "requested" => 0, "count" => 1 },
	'invalid-delay.a' => { "requested" => 0, "count" => 1 },
);

sub rot13 {
	my $str = shift;
	$str =~ y/A-Za-z/N-ZA-Mn-za-m/;
	return $str;
}

sub packet_compare_lists {
	my ($expect, @result) = @_;
	my $ix;
	if (scalar @$expect != scalar @result) {
		return undef;
	}
	for ($ix = 0; $ix < $#result; $ix++) {
		if ($expect->[$ix] ne $result[$ix]) {
			return undef;
		}
	}
	return 1;
}

sub packet_bin_read {
	my $buffer;
	my $bytes_read = read STDIN, $buffer, 4;
	if ( $bytes_read == 0 ) {
		# EOF - Git stopped talking to us!
		return ( -1, "" );
	} elsif ( $bytes_read != 4 ) {
		die "invalid packet: '$buffer'";
	}
	my $pkt_size = hex($buffer);
	if ( $pkt_size == 0 ) {
		return ( 1, "" );
	} elsif ( $pkt_size > 4 ) {
		my $content_size = $pkt_size - 4;
		$bytes_read = read STDIN, $buffer, $content_size;
		if ( $bytes_read != $content_size ) {
			die "invalid packet ($content_size bytes expected; $bytes_read bytes read)";
		}
		return ( 0, $buffer );
	} else {
		die "invalid packet size: $pkt_size";
	}
}

sub remove_final_lf_or_die {
	my $buf = shift;
	unless ( $buf =~ s/\n$// ) {
		die "A non-binary line MUST be terminated by an LF.\n"
		    . "Received: '$buf'";
	}
	return $buf;
}

sub packet_txt_read {
	my ( $res, $buf ) = packet_bin_read();
	unless ( $res == -1 or $buf eq '' ) {
		$buf = remove_final_lf_or_die($buf);
	}
	return ( $res, $buf );
}

sub packet_required_key_val_read {
	my ( $key ) = @_;
	my ( $res, $buf ) = packet_txt_read();
	unless ( $res == -1 or ( $buf =~ s/^$key=// and $buf ne '' ) ) {
		die "bad $key: '$buf'";
	}
	return ( $res, $buf );
}

sub packet_bin_write {
	my $buf = shift;
	print STDOUT sprintf( "%04x", length($buf) + 4 );
	print STDOUT $buf;
	STDOUT->flush();
}

sub packet_txt_write {
	packet_bin_write( $_[0] . "\n" );
}

sub packet_flush {
	print STDOUT sprintf( "%04x", 0 );
	STDOUT->flush();
}

sub packet_initialize {
	my ($name, $version) = @_;

	packet_compare_lists([0, $name . "-client"], packet_txt_read()) ||
		die "bad initialize";
	packet_compare_lists([0, "version=" . $version], packet_txt_read()) ||
		die "bad version";
	packet_compare_lists([1, ""], packet_bin_read()) ||
		die "bad version end";

	packet_txt_write( $name . "-server" );
	packet_txt_write( "version=" . $version );
	packet_flush();
}

print $debug "START\n";
$debug->flush();

packet_initialize("git-filter", 2);

packet_compare_lists([0, "capability=clean"], packet_txt_read()) ||
	die "bad capability";
packet_compare_lists([0, "capability=smudge"], packet_txt_read()) ||
	die "bad capability";
packet_compare_lists([0, "capability=delay"], packet_txt_read()) ||
	die "bad capability";
packet_compare_lists([1, ""], packet_bin_read()) ||
	die "bad capability end";

foreach (@capabilities) {
	packet_txt_write( "capability=" . $_ );
}
packet_flush();
print $debug "init handshake complete\n";
$debug->flush();

while (1) {
	my ( $res, $command ) = packet_required_key_val_read("command");
	if ( $res == -1 ) {
		print $debug "STOP\n";
		exit();
	}
	print $debug "IN: $command";
	$debug->flush();

	if ( $command eq "list_available_blobs" ) {
		# Flush
		packet_compare_lists([1, ""], packet_bin_read()) ||
			die "bad list_available_blobs end";

		foreach my $pathname ( sort keys %DELAY ) {
			if ( $DELAY{$pathname}{"requested"} >= 1 ) {
				$DELAY{$pathname}{"count"} = $DELAY{$pathname}{"count"} - 1;
				if ( $pathname eq "invalid-delay.a" ) {
					# Send Git a pathname that was not delayed earlier
					packet_txt_write("pathname=unfiltered");
				}
				if ( $pathname eq "missing-delay.a" ) {
					# Do not signal Git that this file is available
				} elsif ( $DELAY{$pathname}{"count"} == 0 ) {
					print $debug " $pathname";
					packet_txt_write("pathname=$pathname");
				}
			}
		}

		packet_flush();

		print $debug " [OK]\n";
		$debug->flush();
		packet_txt_write("status=success");
		packet_flush();
	} else {
		my ( $res, $pathname ) = packet_required_key_val_read("pathname");
		if ( $res == -1 ) {
			die "unexpected EOF while expecting pathname";
		}
		print $debug " $pathname";
		$debug->flush();

		# Read until flush
		my ( $done, $buffer ) = packet_txt_read();
		while ( $buffer ne '' ) {
			if ( $buffer eq "can-delay=1" ) {
				if ( exists $DELAY{$pathname} and $DELAY{$pathname}{"requested"} == 0 ) {
					$DELAY{$pathname}{"requested"} = 1;
				}
			} else {
				die "Unknown message '$buffer'";
			}

			( $done, $buffer ) = packet_txt_read();
		}
		if ( $done == -1 ) {
			die "unexpected EOF after pathname '$pathname'";
		}

		my $input = "";
		{
			binmode(STDIN);
			my $buffer;
			my $done = 0;
			while ( !$done ) {
				( $done, $buffer ) = packet_bin_read();
				$input .= $buffer;
			}
			if ( $done == -1 ) {
				die "unexpected EOF while reading input for '$pathname'";
			}			
			print $debug " " . length($input) . " [OK] -- ";
			$debug->flush();
		}

		my $output;
		if ( exists $DELAY{$pathname} and exists $DELAY{$pathname}{"output"} ) {
			$output = $DELAY{$pathname}{"output"}
		} elsif ( $pathname eq "error.r" or $pathname eq "abort.r" ) {
			$output = "";
		} elsif ( $command eq "clean" and grep( /^clean$/, @capabilities ) ) {
			$output = rot13($input);
		} elsif ( $command eq "smudge" and grep( /^smudge$/, @capabilities ) ) {
			$output = rot13($input);
		} else {
			die "bad command '$command'";
		}

		if ( $pathname eq "error.r" ) {
			print $debug "[ERROR]\n";
			$debug->flush();
			packet_txt_write("status=error");
			packet_flush();
		} elsif ( $pathname eq "abort.r" ) {
			print $debug "[ABORT]\n";
			$debug->flush();
			packet_txt_write("status=abort");
			packet_flush();
		} elsif ( $command eq "smudge" and
			exists $DELAY{$pathname} and
			$DELAY{$pathname}{"requested"} == 1 ) {
			print $debug "[DELAYED]\n";
			$debug->flush();
			packet_txt_write("status=delayed");
			packet_flush();
			$DELAY{$pathname}{"requested"} = 2;
			$DELAY{$pathname}{"output"} = $output;
		} else {
			packet_txt_write("status=success");
			packet_flush();

			if ( $pathname eq "${command}-write-fail.r" ) {
				print $debug "[WRITE FAIL]\n";
				$debug->flush();
				die "${command} write error";
			}

			print $debug "OUT: " . length($output) . " ";
			$debug->flush();

			while ( length($output) > 0 ) {
				my $packet = substr( $output, 0, $MAX_PACKET_CONTENT_SIZE );
				packet_bin_write($packet);
				# dots represent the number of packets
				print $debug ".";
				if ( length($output) > $MAX_PACKET_CONTENT_SIZE ) {
					$output = substr( $output, $MAX_PACKET_CONTENT_SIZE );
				} else {
					$output = "";
				}
			}
			packet_flush();
			print $debug " [OK]\n";
			$debug->flush();
			packet_flush();
		}
	}
}

#
# Example implementation for the Git filter protocol version 2
# See Documentation/gitattributes.txt, section "Filter Protocol"
#
# The script takes the list of supported protocol capabilities as
# arguments ("clean", "smudge", etc).
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
#

use strict;
use warnings;
use IO::File;

my $MAX_PACKET_CONTENT_SIZE = 65516;
my @capabilities            = @ARGV;

open my $debug, ">>", "rot13-filter.log" or die "cannot open log file: $!";

sub rot13 {
	my $str = shift;
	$str =~ y/A-Za-z/N-ZA-Mn-za-m/;
	return $str;
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

sub packet_txt_read {
	my ( $res, $buf ) = packet_bin_read();
	unless ( $res == -1 || $buf =~ s/\n$// ) {
		die "A non-binary line MUST be terminated by an LF.\n"
		    . "Received: '$buf'";
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

	( packet_txt_read() eq ( 0, $name . "-client" ) )       || die "bad initialize";
	( packet_txt_read() eq ( 0, "version=" . $version ) )   || die "bad version";
	( packet_bin_read() eq ( 1, "" ) )                      || die "bad version end";

	packet_txt_write( $name . "-server" );
	packet_txt_write( "version=" . $version );
	packet_flush();
}

sub packet_read_capabilities {
	my @cap;
	while (1) {
		my ( $res, $buf ) = packet_bin_read();
		return ( $res, @cap ) if ( $res != 0 );
		unless ( $buf =~ s/\n$// ) {
			die "A non-binary line MUST be terminated by an LF.\n"
			    . "Received: '$buf'";
		}
		die "bad capability buf: '$buf'" unless ( $buf =~ s/capability=// );
		push @cap, $buf;
	}
}

sub packet_read_and_check_capabilities {
	my @local_caps = @_;
	my @remote_res_caps = packet_read_capabilities();
	my $res = shift @remote_res_caps;
	my %remote_caps = map { $_ => 1 } @remote_res_caps;
	foreach (@local_caps) {
        	die "'$_' capability not available" unless (exists($remote_caps{$_}));
	}
	return $res;
}

sub packet_write_capabilities {
	packet_txt_write( "capability=" . $_ ) foreach (@_);
	packet_flush();
}

print $debug "START\n";
$debug->flush();

packet_initialize("git-filter", 2);

packet_read_and_check_capabilities("clean", "smudge");
packet_write_capabilities(@capabilities);

print $debug "init handshake complete\n";
$debug->flush();

while (1) {
	my ($res, $command) = packet_txt_read();
	if ( $res == -1 ) {
		print $debug "STOP\n";
		exit();
	}
	$command =~ s/^command=//;
	print $debug "IN: $command";
	$debug->flush();

	my ($pathname) = packet_txt_read() =~ /^pathname=(.+)$/;
	print $debug " $pathname";
	$debug->flush();

	if ( $pathname eq "" ) {
		die "bad pathname '$pathname'";
	}

	# Flush
	packet_bin_read();

	my $input = "";
	{
		binmode(STDIN);
		my $buffer;
		my $done = 0;
		while ( !$done ) {
			( $done, $buffer ) = packet_bin_read();
			$input .= $buffer;
		}
		print $debug " " . length($input) . " [OK] -- ";
		$debug->flush();
	}

	my $output;
	if ( $pathname eq "error.r" or $pathname eq "abort.r" ) {
		$output = "";
	} elsif ( $command eq "clean" and grep( /^clean$/, @capabilities ) ) {
		$output = rot13($input);
	} elsif ( $command eq "smudge" and grep( /^smudge$/, @capabilities ) ) {
		$output = rot13($input);
	} else {
		die "bad command '$command'";
	}

	print $debug "OUT: " . length($output) . " ";
	$debug->flush();

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
	} else {
		packet_txt_write("status=success");
		packet_flush();

		if ( $pathname eq "${command}-write-fail.r" ) {
			print $debug "[WRITE FAIL]\n";
			$debug->flush();
			die "${command} write error";
		}

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

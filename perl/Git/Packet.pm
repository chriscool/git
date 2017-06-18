package Git::Packet;
use 5.008;
use strict;
use warnings;
BEGIN {
	require Exporter;
	if ($] < 5.008003) {
		*import = \&Exporter::import;
	} else {
		# Exporter 5.57 which supports this invocation was
		# released with perl 5.8.3
		Exporter->import('import');
	}
}

our @EXPORT = qw(
			packet_bin_read
			packet_txt_read
			packet_bin_write
			packet_txt_write
			packet_flush
			packet_initialize
			packet_read_capabilities
			packet_write_capabilities
			packet_read_and_check_capabilities
		);
our @EXPORT_OK = @EXPORT;

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

sub compare_lists {
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

sub packet_initialize {
	my ($name, $version) = @_;

	compare_lists([0, $name . "-client"], packet_txt_read()) ||
		die "bad initialize";
	compare_lists([0, "version=" . $version], packet_txt_read()) ||
		die "bad version";
	compare_lists([1, ""], packet_bin_read()) ||
		die "bad version end";

	packet_txt_write( $name . "-server" );
	packet_txt_write( "version=" . $version );
	packet_flush();
}

sub packet_read_capabilities {
	my @cap;
	while (1) {
		my ( $res, $buf ) = packet_bin_read();
		return ( $res, @cap ) if ( $res != 0 );
		$buf = remove_final_lf_or_die($buf);
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

1;

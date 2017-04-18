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

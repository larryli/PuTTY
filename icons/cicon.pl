#!/usr/bin/perl 

# Given a list of input PNGs, create a C source file containing a
# const array of XPMs, named by a given C identifier.

$id = shift @ARGV;
$k = 0;
@xpms = ();
foreach $f (@ARGV) {
  # XPM format is generated directly by ImageMagick, so that's easy
  # enough. We just have to adjust the declaration line so that it
  # has the right name, linkage and storage class.
  @lines = ();
  open XPM, "convert $f xpm:- |";
  push @lines, $_ while <XPM>;
  close XPM;
  die "XPM from $f in unexpected format\n" unless $lines[1] =~ /^static.*\{$/;
  $lines[1] = "static const char *const ${id}_$k"."[] = {\n";
  $k++;
  push @xpms, @lines, "\n";
}

# Now output.
foreach $line (@xpms) { print $line; }
print "const char *const *const ${id}[] = {\n";
for ($i = 0; $i < $k; $i++) { print "    ${id}_$i,\n"; }
print "};\n";
print "const int n_${id} = $k;\n";

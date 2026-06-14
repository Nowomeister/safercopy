Short: Reliable file copy VERIFY/UPDATE/BUFFER\
Author: Nowee (with Claude)\
Uploader: Nowee\
Type: util/cli\
Version: 1.42\
Date: 2026.06.11\
Requires: AmigaOS 2.04+ (V37), 68000+\
Architecture: m68k-amigaos\

SaferCopy 1.42 - A reliable replacement for AmigaDOS Copy 47.7


![A successful copy](/images/safercopy.png)


BACKGROUND

Copy 47.7 shipped with AmigaOS 3.2.3 silently ignores the VERIFY, DATES
and BUF/BUFFER keywords documented in the AmigaDOS 3.2.3 manual.
Hence this version.

FEATURES
- Lets you copy only the missing, uncomplete or older files with the
  "update" option.
- Partial write detection: checks Write() return value on every call.
  Silently truncated files are caught, logged, and the destination is
  replaced.
- Locale : fully localised via locale.library. Machine translated
  catalogs are included for 18 languages. Reverts to English if
  locale.library is absent or no catalog is found.
- Path handling: destination directories are created recursively
  (equivalent of mkdir -p).
- Error buffering, errors gets printed both immediately AND are
  summarised at the end. No need to watch the terminal for ages.
- Lets you rename filenames truncated by the filesystem, for example
  upon copying from PFS3 107 chars to PFS3 30 chars (facepalms).
  It will compare the files to properly rename them.
- Show progress percentage for large copies

TESTED (MD5, etc) on Amiga 4000, 3000T, WinUAE, over a tremendous amount
of files. And again. Aaand again. And again. Man, it's exhausting.

It works for me, but I can't garantee it won't burn down your house to
the ground so please make a copy of it first... Uhh wait.. :D
It comes with no garantee whatsoever.

Thanks to Kolla for the testing.

It's been fixed.. or not.. with the help of Claude Opus 4.8


USAGE

*** Please use stack 32768 ***

BUF/BUFFER : actually uses the specified buffer size (default 512KB).\

DATES : actually calls SetFileDate() when specified alone, without
requiring CLONE.

VERIFY : actually re-reads the destination after every write and
compares byte-for-byte with the source. Corrupted files are replaced
immediately.

PROGRESS : show a live percentage while copying, for files larger than
2 MB (updates in place on one line). Handy for big disk images over slow
media. Suppressed by QUIET.

UPDATE : skips files where destination exists with identical size and
datestamp. Re-copies if destination is smaller (previous interrupted
copy) or older than source. Reports "[dest incomplete: X/Y bytes]" for
visibility.\

NDATE : with UPDATE, compare size only (ignore datestamp). Useful when
a previous backup was done without DATES/CLONE and destination files
have the wrong date.\

VERBOSE : shows skipped (up-to-date) files. By default only active
copies and errors are displayed.\

MAXERR/K/N : abort after N errors. Useful for unattended large copies
over unreliable media. Default 0 = no limit.\

FORCE : strips write-protection from destination before copying.\

CHECK : audit mode - no files are copied. For each source file,
compares size against destination and reports Missing or SizeDiff.\

Combine with NDATE for a fast size-only scan. Useful before a resume\
to see what actually needs attention.

NAMELEN/K/N : reports (and skips) any file or directory whose name is
longer than N characters. PFS3 volumes keep the FFS default of 30
characters unless long filenames were configured at format time, so
longer names written through more tolerant paths will not fit on a
backup volume. Combine with CHECK for a copy-free inventory.

Example: SaferCopy FROM DH0: TO Backup: ALL CHECK NAMELEN=30

RENAME : repair a backup whose long names were previously truncated, after
the destination volume has been reconfigured to accept long names (PFS3
setfsname). It is a pure renamer - NO files are copied, it has nothing to do
with UPDATE. For every source name longer than NAMELEN, the already-present
truncated copy on the destination is RENAMED to its full name (a metadata
operation, no data transfer). This avoids re-copying everything (which would
also leave the short versions behind as orphans). Requires NAMELEN. Combine
with CHECK for a dry run (prints "WouldRen ..." without touching anything).
Never overwrites an existing full name, never deletes.

Here NAMELEN is the EXACT truncation size used when the backup was made: 30
for FFS or a default PFS3 partition (set it to whatever limit the destination
volume had at the time). The pairing is on the exact first-NAMELEN-character
prefix - if the real truncation used a different length, the entry is reported
as "missing" rather than renamed (RENAME never guesses). If you don't know
the length, try a few CHECK runs with different NAMELEN values and see which
one turns "missing" into "WouldRen".

Short names (<= NAMELEN) were never truncated and are left untouched. Entries
that are not on the destination at all (never backed up, or the loser of a
collision where two long names share the first N characters) are NOT errors:
they are counted as "missing" in the final report. Copy those with a normal
UPDATE afterwards. Run RENAME at the same FROM/TO granularity as the original
backup (typically a whole volume).

By default RENAME prints only the final summary (renamed / skipped / missing
/ errors): on a real Amiga, one console line per operation would, over tens
of thousands of fast metadata renames, make the console the bottleneck and
look like a hang. Add VERBOSE to see each individual rename and each missing
entry; for a CHECK dry run of a large tree, redirect the output to a file
(... >RAM:rename.log).

Example (preview): SaferCopy FROM DH0: TO Backup: ALL RENAME NAMELEN=30 CHECK
Example (do it):   SaferCopy FROM DH0: TO Backup: ALL RENAME NAMELEN=30



PROGRESS : show progress percentage for large copies\

Example: SaferCopy FROM Work: TO Backup: ALL CHECK NDATE\

TEMPLATE
FROM/M,TO/A,ALL/S,QUIET/S,BUF=BUFFER/K/N,CLONE/S,DATES/S,
NOPRO/S,VERIFY/S,NOREQ/S,UPDATE/S,FORCE/S,MAXERR/K/N,NDATE/S,VERBOSE/S,
CHECK/S, NAMELEN/K/N, RENAME/S, PROGRESS/S

Type "SaferCopy ?" for an interactive argument prompt.


EXAMPLES

Full backup with verification, preserve dates, 1MB buffer
SaferCopy FROM Work: TO DH1:Backup/ ALL CLONE VERIFY BUF=1048576 NOREQ

Incremental update, abort after 50 errors
SaferCopy FROM Work: TO DH1:Backup/ ALL UPDATE VERIFY BUF=1048576 MAXERR=50

USB to HD, big buffer
SaferCopy FROM USB0:data/ TO DH1:data/ ALL CLONE VERIFY BUF=2097152 NOREQ

Catch up after a backup done without DATES (ignore datestamp differences)
SaferCopy FROM sys: TO Multimedia:SYSBACKUP/ ALL UPDATE NDATE CLONE
  VERIFY NOREQ

Rename truncated files. NAMELEN is the truncation size, ie 30 char
for FFS or a default PFS3 partition, to be used with RENAME. 
SaferCopy FROM <src> TO <dst> ALL RENAME NAMELEN=30 CHECK



LANGUAGES

The catalogs are LLM translated. Please report in case of impediments...

Catalogs are included for:
  catala, czech, dansk, deutsch, english, espanol, euskara, francais,
  greek, italiano, nederlands, norsk, polski, portugues, roman,
  russian, slovensko, srpski, turkce.

Install: copy catalogs/$LANGUAGE/SaferCopy.catalog to
LOCALE:Catalogs/$LANGUAGE/SaferCopy.catalog


COMPILATION

GCC : m68k-amigaos-gcc -O2 -m68000 -noixemul -resident -Wall -o SaferCopy
SaferCopy.c

SASC: sc SaferCopy.c LINK RESIDENT MATH=SOFT NOSTKCHK OPT IDIR=Include:
IDIR=NDK3.2:Include

The -resident / RESIDENT options produce a pure (re-entrant) binary that
can be made resident with the Shell RESIDENT command.


SOURCES

Available on GitHub, https://github.com/Nowomeister/safercopy/


TODO:
Test on OS 2.04, 3.0, 3.1.


HISTORY


1.4.2 - Added NAMELEN/K/N: report (and with RENAME, match) names longer
      than N characters. Added RENAME/S: repair a previously-truncated
      backup by renaming the short destination names back to their full
      source names (metadata only, no re-copy) after the volume has been
      reconfigured for long names (PFS3 setfsname); CHECK gives a dry-run
      preview. Added PROGRESS/S: live percentage while copying files
      larger than 2 MB. Claude fixed C89 mistakes for SAS/C (declarations
      after statements, // comments, a long-long percentage calc). Cool
      compilation tip from a Gunther. Y hallo thar!
1.4.1 - Let's not talk about it, would you?\
1.4 - Fixed a stack smash in the error logger: error messages for paths
      longer than ~350 characters overflowed a fixed 384-byte buffer
      (NOSTKCHK + no MMU = silent corruption, delayed lockup on long
      unattended copies). Error formatting is now bounded and uses
      static buffers (less stack at maximum recursion depth). The
      AnchorPath is now zeroed between the two MatchFirst passes as
      required by the dos.library autodoc (stale AChain state could
      leak memory progressively). NOREQ no longer leaves the Shell
      process with requesters permanently disabled after exit
      (pr_WindowPtr is restored even when the original value was NULL).
      Corrupt directory entries (unterminated, empty, or
      separator-containing names returned by the filesystem, as seen
      on a damaged PFS3 volume) are now detected, reported and
      skipped instead of being silently propagated to the
      destination. Optional pure/residentable build (GCC -resident
      "pure" make target, SAS/C RESIDENT + cres.o); the default binary
      stays non-resident until the pure variant is validated on real
      hardware.\
1.3 - Fixed lockup on directory names containing AmigaDOS pattern
      special characters (& ~ # ? ( ) [ ] | %). Added CHECK/S audit
      mode (size comparison, no copy).\
1.2 - Added locale catalog support (18 languages), $VER string,
      minimum OS version check (V37 / AmigaOS 2.04+).\
1.1 - Added NDATE, VERBOSE, recursive mkdir, fixed sys: path handling,
      fixed Shell window not closing after exit.\
1.0 - Initial release.\
1.1 - Added NDATE, VERBOSE, recursive mkdir, fixed sys: path handling,
      fixed Shell window not closing after exit.\
1.0 - Initial release (2011).\

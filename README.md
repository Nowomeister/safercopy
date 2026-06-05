Short:        Reliable file copy, fixes Copy 47.7 VERIFY/DATES/BUF bugs
Author:       Nowee (with Claude)
Uploader:     Nowee
Type:         util/shell
Version:      1.2
Requires:     AmigaOS 2.04+ (V37), 68000+, ~1MB free RAM per BUF size
Architecture: m68k-amigaos

---

SaferCopy 1.1 - A reliable replacement for AmigaDOS Copy 47.7

BACKGROUND

  Copy 47.7 shipped with AmigaOS 3.2.3 silently ignores the VERIFY,
  DATES and BUF/BUFFER keywords documented in the AmigaDOS 3.2.3 manual.
  On USB stacks (Poseidon) and FAT95, this causes random incomplete file
  copies with no error reported - particularly destructive during large
  backups involving millions of files.

FIXES vs COPY 47.7

  - BUF/BUFFER  : actually uses the specified buffer size (default 512KB).
                  Critical for USB performance: reduces DOS calls from
                  700/sec to ~1/sec for a 500MB file.

  - DATES       : actually calls SetFileDate() when specified alone,
                  without requiring CLONE.

  - VERIFY      : actually re-reads the destination after every write and
                  compares byte-for-byte with the source. Corrupted files
                  are deleted immediately.

  - Partial write detection: checks Write() return value on every call.
                  Silently truncated files (common on FAT95/Poseidon) are
                  caught, logged, and the destination is deleted.

  - Path handling: destination directories are created recursively
                  (equivalent of mkdir -p). Assign paths like sys: are
                  handled correctly without spurious slashes.

EXTRA FEATURES

  UPDATE        : skips files where destination exists with identical
                  size and datestamp. Re-copies if destination is smaller
                  (previous interrupted copy) or older than source.
                  Reports "[dest incomplete: X/Y bytes]" for visibility.

  NDATE         : with UPDATE, compare size only (ignore datestamp).
                  Useful when a previous backup was done without DATES/CLONE
                  and destination files have the wrong date.

  VERBOSE       : show skipped (up-to-date) files. By default only active
                  copies and errors are displayed.

  MAXERR/K/N    : abort after N errors. Useful for unattended large copies
                  over unreliable media. Default 0 = no limit.

  Error buffering: errors print immediately AND are summarised at the end.
                  No need to watch the terminal for hours.

  FORCE         : strips write-protection from destination before copying.

  Locale        : fully localised via locale.library. Catalogs included for
                  18 languages. Falls back to English if locale.library is
                  absent or no catalog is found.

TEMPLATE

  FROM/M,TO/A,ALL/S,QUIET/S,BUF=BUFFER/K/N,CLONE/S,DATES/S,
  NOPRO/S,VERIFY/S,NOREQ/S,UPDATE/S,FORCE/S,MAXERR/K/N,NDATE/S,VERBOSE/S

  Type "SaferCopy ?" for an interactive argument prompt (AmigaDOS standard).

EXAMPLES

  - Full backup with verification, preserve dates, 1MB buffer
  SaferCopy FROM Work: TO DH1:Backup/ ALL CLONE VERIFY BUF=1048576 NOREQ

  - Incremental update, abort after 50 errors
  SaferCopy FROM Work: TO DH1:Backup/ ALL UPDATE VERIFY BUF=1048576 MAXERR=50

  - USB to HD, big buffer
  SaferCopy FROM USB0:data/ TO DH1:data/ ALL CLONE VERIFY BUF=2097152 NOREQ

  - Catch up after a backup done without DATES (ignore datestamp differences)
  SaferCopy FROM sys: TO Multimedia:SYSBACKUP/ ALL UPDATE NDATE CLONE VERIFY NOREQ

LANGUAGES

  Catalogs included for: english, francais, deutsch, espanol, nederlands,
  dansk, norsk, polski, czech, slovensko, portugues, catala, euskara,
  turkce, srpski, russian, greek, roman.

  Install: copy <lang>/SaferCopy.catalog to LOCALE:Catalogs/<lang>/

COMPILATION

  GCC : m68k-amigaos-gcc -O2 -m68000 -noixemul -Wall -o SaferCopy SaferCopy.c
  SASC: sc SaferCopy.c LINK MATH=SOFT NOSTKCHK OPT IDIR=Include: IDIR=NDK3.2:Include

  Catalogs: python3 catalogs/build_catalog.py  (requires Python 3, no catcomp needed)

SOURCE

  Included in the archive.

TODO:

	Test on OS 2.04, 3.0, 3.1.

HISTORY

  1.2 - Added locale catalog support (18 languages), $VER string,
        minimum OS version check (V37 / AmigaOS 2.04+).

  1.1 - Added NDATE, VERBOSE, recursive mkdir,
        fixed sys: path handling, fixed Shell window not closing after exit.

  1.0 - Initial release.

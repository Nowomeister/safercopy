/*
 * SaferCopy.c -- Remplacement de Copy pour AmigaOS 3.2.3
 *
 * Bugs corriges vs Copy 47.7 :
 *   [1] BUF/BUFFER ignore en silence         -> buffer configurable, defaut 512 KB
 *   [2] DATES ignore en silence              -> SetFileDate() appele si DATES ou CLONE
 *   [3] VERIFY ignore en silence             -> relecture + comparaison octet par octet
 *   [4] Write() partiel non detecte          -> verification nWritten == nRead, abort+delete
 *   [5] NOREQ via pr_WindowPtr = -1          -> pas de requeteurs systeme
 *   [6] Fichier dest corrompu laisse intouche -> DeleteFile() si echec
 *
 * Template AmigaDOS 3.2.3 (du manuel) :
 *   FROM/M,TO/A,ALL/S,QUIET/S,BUF=BUFFER/K/N,CLONE/S,DATES/S,
 *   NOPRO/S,VERIFY/S,NOREQ/S,UPDATE/S,FORCE/S
 *
 *   UPDATE : ne copie que si dest absent, taille differente (copie incomplete
 *            precedente), ou src plus recent que dest
 *   FORCE  : copie meme si dest est protege en ecriture
 *
 * Compilation SAS/C 6.x :
 *   sc SaferCopy.c LINK MATH=SOFT NOSTKCHK OPT STRMERGE
 *      IDIR=Include: IDIR=NDK3.2:Include
 *
 * Compilation GCC cross (m68k-amigaos-gcc 6.x) :
 *   m68k-amigaos-gcc -O2 -m68000 -noixemul -Wall -o SaferCopy SaferCopy.c
 *
 * Notes de portabilite :
 *   - printf/fprintf/sprintf (stdio libnix) a la place des macros
 *     varargs AmigaOS Printf/PutStr/SNPrintf.
 *     Raison : Printf du NDK utilise un tableau LONG[] en interne ;
 *     GCC 6 se plaint du melange pointeur signé/non signé.
 *   - Buffers de chemins internes en char[]. Cast (STRPTR) uniquement
 *     aux appels AmigaOS qui exigent STRPTR.
 *   - SNPrintf absent du NDK 3.x -> sprintf.
 *   - INLINE absent sous GCC avant inclusion des headers -> __inline__.
 *     NOTE : ne pas ecrire "asterisque slash" dans un commentaire C,
 *     cela termine le bloc commentaire prematurement.
 *     Ne pas mettre d apostrophes dans les commentaires bloc C : le
 *     compilateur les interprete comme debut de litteral de caractere.
 */

/* --- Includes ---------------------------------------------------- */
#include <proto/dos.h>
#include <proto/exec.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/dosasl.h>
#include <stdio.h>     /* printf, fprintf, sprintf, fputs, fflush */
#include <stdlib.h>    /* exit */
#include <string.h>    /* memcmp, strncpy, memset                 */

/* --- Portabilite inline ------------------------------------------ */
/*
 * SASC definit INLINE via ses headers.
 * GCC : __inline__ fonctionne avant et apres inclusion NDK.
 */
#ifndef INLINE
#  define INLINE __inline__
#endif

/* --------------------------------------------------------------*/
/*  Template                                                            */
/* --------------------------------------------------------------*/

#define TEMPLATE \
    "FROM/M,TO/A,ALL/S,QUIET/S,BUF=BUFFER/K/N," \
    "CLONE/S,DATES/S,NOPRO/S,VERIFY/S,NOREQ/S,UPDATE/S,FORCE/S," \
    "MAXERR/K/N"

enum {
    A_FROM=0, A_TO, A_ALL, A_QUIET, A_BUF,
    A_CLONE, A_DATES, A_NOPRO, A_VERIFY, A_NOREQ, A_UPDATE, A_FORCE,
    A_MAXERR,
    A_COUNT
};

/* --------------------------------------------------------------*/
/*  Constantes                                                          */
/* --------------------------------------------------------------*/

/*
 * 512 KB par defaut.
 * Sur USB 1.1 Amiga (700 KB/s reel) : 1 appel Read/Write par seconde
 * au lieu de 700 avec le 1 KB par defaut du DOS.
 * Passer a 1 MB ou 2 MB si la RAM le permet : BUF=2097152
 */
#define BUF_DEFAULT  (512UL * 1024UL)
#define BUF_MIN      (4096UL)
#define BUF_MAX      (8UL * 1024UL * 1024UL)

#define MAXPATH      512

/* --------------------------------------------------------------*/
/*  Macros de sortie                                                    */
/*                                                                      */
/*  On utilise printf/fprintf (libnix) plutot que Printf/PutStr (NDK). */
/*  En contexte CLI, stdout == Output() et stderr == Error().          */
/* --------------------------------------------------------------*/

#define OUT(fmt, ...)  printf(fmt,  ##__VA_ARGS__)
#define ERR(fmt, ...)  fprintf(stderr, fmt, ##__VA_ARGS__)

/* --------------------------------------------------------------*/
/*  Etat global                                                         */
/* --------------------------------------------------------------*/

/*
 * Buffer d'erreurs : accumule les messages pendant la copie,
 * les affiche en bloc a la fin. Les gens n'ont pas a surveiller
 * un terminal pendant des heures pour des millions de fichiers.
 *
 * 24 KB : ~150 erreurs de 160 chars chacune. Si ca deborde,
 * on compte les messages perdus et on les signale a la fin.
 */
#define ERR_BUF_BYTES  24576    /* 24 KB */

static char  g_errBuf[ERR_BUF_BYTES];
static ULONG g_errBufUsed  = 0;
static LONG  g_errTrunc    = 0;   /* messages non loges : buffer plein */

static struct {
    UBYTE  *buf;        /* buffer copie  (UBYTE* pour Read/Write)   */
    UBYTE  *vbuf;       /* buffer verify (UBYTE* pour Read)         */
    ULONG   bufSize;
    BOOL    all;
    BOOL    quiet;
    BOOL    clone;      /* CLONE = dates + protection + commentaire */
    BOOL    dates;      /* DATES seul (sous-ensemble de CLONE)      */
    BOOL    nopro;      /* ne PAS copier la protection              */
    BOOL    verify;     /* relire et comparer apres ecriture        */
    BOOL    update;     /* sauter si dest existe et est a jour      */
    BOOL    force;      /* virer la protection dest si necessaire   */
    LONG    nCopied;
    LONG    nSkipped;
    LONG    nErrors;
    LONG    maxErr;     /* 0 = illimite ; >0 = abandon si nErrors >= maxErr */
    BOOL    abort;      /* mis a 1 quand maxErr est depasse          */
    LONG    retCode;
    APTR    oldWinPtr;  /* pr_WindowPtr original pour NOREQ         */
} G;

/* --------------------------------------------------------------*/
/*  Wrappers inline : cast (STRPTR) en un seul endroit                 */
/*  Evite de polluer tout le code avec des casts explicites.           */
/* --------------------------------------------------------------*/

static INLINE BPTR xOpen(const char *p, LONG m)
    { return Open((STRPTR)p, m); }

static INLINE BPTR xLock(const char *p, LONG m)
    { return Lock((STRPTR)p, m); }

static INLINE BOOL xDel(const char *p)
    { return DeleteFile((STRPTR)p); }

static INLINE BOOL xExDir(const char *p)
    { BPTR l = CreateDir((STRPTR)p);
      if (l) { UnLock(l); return TRUE; }
      return FALSE; }

static INLINE BOOL xDate(const char *p, struct DateStamp *ds)
    { return SetFileDate((STRPTR)p, ds); }

static INLINE BOOL xProt(const char *p, ULONG f)
    { return SetProtection((STRPTR)p, f); }

static INLINE BOOL xComm(const char *p, const char *c)
    { return SetComment((STRPTR)p, (STRPTR)c); }

static INLINE const char *xPart(const char *p)
    { return (const char *)FilePart((STRPTR)p); }

static INLINE BOOL xAddPart(char *o, const char *l, ULONG n)
    { return AddPart((STRPTR)o, (STRPTR)l, n); }

/* --------------------------------------------------------------*/
/*  Prototypes internes                                                 */
/* --------------------------------------------------------------*/

static BOOL  CopyFile   (const char *src, const char *dst);
static BOOL  VerifyFiles(const char *src, const char *dst);
static void  CopyDir    (const char *src, const char *dst);
static BOOL  NeedsCopy  (const char *src, const char *dst);
static BOOL  EnsureDir  (const char *path);
static BOOL  IsDir      (const char *path, struct FileInfoBlock *fib_out);
static void  JoinPath   (char *out, LONG outlen,
                         const char *dir, const char *leaf);
static void  Fail       (const char *msg, const char *arg);
static void  PrintErrors(void);
static void  CheckAbort (void);
static void  Die        (LONG code);

/* --------------------------------------------------------------*/
/*  main                                                                */
/* --------------------------------------------------------------*/

int main(void)
{
    struct RDArgs  *rda;
    LONG            args[A_COUNT];
    STRPTR         *fromList;
    const char     *toPath;
    BOOL            toIsDir;
    LONG            i;

    memset(args, 0, sizeof(args));
    memset(&G,   0, sizeof(G));
    G.retCode = RETURN_OK;

    /* --- ReadArgs -------------------------------------------------- */
    rda = ReadArgs((STRPTR)TEMPLATE, args, NULL);
    if (!rda)
    {
        PrintFault(IoErr(), (STRPTR)"SaferCopy");
        return RETURN_FAIL;
    }

    fromList = (STRPTR *) args[A_FROM];
    toPath   = (const char *) args[A_TO];
    G.all    = (BOOL) args[A_ALL];
    G.quiet  = (BOOL) args[A_QUIET];
    G.clone  = (BOOL) args[A_CLONE];
    G.dates  = (BOOL) args[A_DATES] || G.clone;
    G.nopro  = (BOOL) args[A_NOPRO];
    G.verify = (BOOL) args[A_VERIFY];
    G.update = (BOOL) args[A_UPDATE];
    G.force  = (BOOL) args[A_FORCE];

    /*
     * MAXERR : 0 = pas de limite (defaut).
     * MAXERR=10 -> abandonne apres 10 erreurs.
     */
    G.maxErr = args[A_MAXERR] ? *(LONG *)args[A_MAXERR] : 0;

    /*
     * BUF : ReadArgs retourne un pointeur sur LONG pour les args /N.
     * Exemple : BUF=1048576 -> args[A_BUF] = adresse d un LONG = 1048576.
     */
    G.bufSize = args[A_BUF]
                ? (ULONG)(*(LONG *)args[A_BUF])
                : BUF_DEFAULT;
    if (G.bufSize < BUF_MIN) G.bufSize = BUF_MIN;
    if (G.bufSize > BUF_MAX) G.bufSize = BUF_MAX;

    /*
     * NOREQ : pr_WindowPtr = (APTR)-1 coupe tous les requeteurs.
     * Plus fiable que SetReqHandler() qui ne couvre pas tout.
     */
    if (args[A_NOREQ])
    {
        struct Process *me = (struct Process *)FindTask(NULL);
        G.oldWinPtr      = me->pr_WindowPtr;
        me->pr_WindowPtr = (APTR)-1L;
    }

    /* --- Allocation buffers ---------------------------------------- */
    G.buf = (UBYTE *)AllocVec(G.bufSize, MEMF_ANY);
    if (G.verify)
        G.vbuf = (UBYTE *)AllocVec(G.bufSize, MEMF_ANY);

    if (!G.buf || (G.verify && !G.vbuf))
    {
        ERR("SaferCopy: impossible d allouer %lu octets de buffer\n",
            G.verify
            ? (unsigned long)(G.bufSize * 2)
            : (unsigned long)(G.bufSize));
        FreeArgs(rda);
        Die(RETURN_FAIL);
    }

    if (!G.quiet) {
        OUT("SaferCopy: buffer %lu Ko%s%s%s",
            (unsigned long)(G.bufSize / 1024),
            G.verify ? ", VERIFY" : "",
            G.dates  ? ", DATES"  : "",
            G.update ? ", UPDATE" : "");
        if (G.maxErr > 0)
            OUT(", MAXERR=%ld", (long)G.maxErr);
        OUT("\n");
    }

    /* --- Verification arguments ------------------------------------ */
    if (!fromList || !fromList[0])
    {
        ERR("SaferCopy: FROM est obligatoire\n");
        FreeArgs(rda);
        Die(RETURN_FAIL);
    }

    /* --- Destination ---------------------------------------------- */
    toIsDir = IsDir(toPath, NULL);

    if (fromList[0] && fromList[1] && !toIsDir)
    {
        if (!EnsureDir(toPath))
        {
            Fail("SaferCopy: impossible de creer le repertoire destination",
                 toPath);
            FreeArgs(rda);
            Die(RETURN_FAIL);
        }
        toIsDir = TRUE;
    }

    /* --- Boucle principale ---------------------------------------- */
    for (i = 0; fromList[i] && !G.abort; i++)
    {
        const char *src = (const char *)fromList[i];
        char        dst[MAXPATH];

        if (IsDir(src, NULL))
        {
            if (!G.all)
            {
                if (!G.quiet)
                    OUT("SaferCopy: '%s' est un repertoire - utilisez ALL\n",
                        src);
                G.nErrors++;
                G.retCode = RETURN_WARN;
                continue;
            }
            if (toIsDir)
                JoinPath(dst, MAXPATH, toPath, xPart(src));
            else
            {
                strncpy(dst, toPath, (size_t)(MAXPATH - 1));
                dst[MAXPATH - 1] = '\0';
            }
            if (!EnsureDir(dst))
            {
                Fail("SaferCopy: impossible de creer", dst);
                G.nErrors++;
                G.retCode = RETURN_WARN;
                CheckAbort();
                continue;
            }
            CopyDir(src, dst);
        }
        else
        {
            if (toIsDir)
                JoinPath(dst, MAXPATH, toPath, xPart(src));
            else
            {
                strncpy(dst, toPath, (size_t)(MAXPATH - 1));
                dst[MAXPATH - 1] = '\0';
            }
            if (!CopyFile(src, dst))
            {
                G.nErrors++;
                G.retCode = RETURN_WARN;
                CheckAbort();
            }
        }
    }

    /* --- Rapport final -------------------------------------------- */
    PrintErrors();   /* toujours, meme en mode QUIET */

    if (!G.quiet || G.nErrors > 0)
        OUT("SaferCopy: %ld copie(s)  %ld ignoree(s)  %ld erreur(s)%s\n",
            (long)G.nCopied, (long)G.nSkipped, (long)G.nErrors,
            G.abort ? "  [ABANDON: MAXERR atteint]" : "");

    FreeArgs(rda);
    Die(G.retCode);
    return G.retCode;
}

/* --------------------------------------------------------------*/
/*  CopyDir                                                             */
/* --------------------------------------------------------------*/

static void CopyDir(const char *src, const char *dst)
{
    struct AnchorPath *ap;
    char               pat[MAXPATH + 8];
    LONG               rc;

    ap = (struct AnchorPath *)
         AllocVec(sizeof(struct AnchorPath) + MAXPATH,
                  MEMF_CLEAR | MEMF_ANY);
    if (!ap)
    {
        ERR("SaferCopy: memoire insuffisante (AnchorPath)\n");
        G.nErrors++;
        return;
    }
    ap->ap_Strlen = MAXPATH;

    /* SNPrintf absent du NDK 3.x -> sprintf */
    sprintf(pat, "%s/#?", src);

    /* Passe 1 : sous-repertoires (recurse en premier) */
    rc = MatchFirst((STRPTR)pat, ap);
    while (rc == 0 && !G.abort)
    {
        if (ap->ap_Info.fib_DirEntryType > 0)
        {
            char subdst[MAXPATH];
            JoinPath(subdst, MAXPATH, dst, ap->ap_Info.fib_FileName);
            if (!EnsureDir(subdst))
            {
                Fail("SaferCopy: impossible de creer", subdst);
                G.nErrors++;
                CheckAbort();
            }
            else
            {
                CopyDir((const char *)ap->ap_Buf, subdst);
            }
        }
        rc = MatchNext(ap);
    }
    MatchEnd(ap);

    /* Passe 2 : fichiers */
    rc = MatchFirst((STRPTR)pat, ap);
    while (rc == 0 && !G.abort)
    {
        if (ap->ap_Info.fib_DirEntryType < 0)
        {
            char dstfile[MAXPATH];
            JoinPath(dstfile, MAXPATH, dst, ap->ap_Info.fib_FileName);
            if (!CopyFile((const char *)ap->ap_Buf, dstfile))
            {
                G.nErrors++;
                G.retCode = RETURN_WARN;
                CheckAbort();
            }
        }
        rc = MatchNext(ap);
    }
    MatchEnd(ap);

    FreeVec(ap);
}

/* --------------------------------------------------------------*/
/*  CopyFile                                                     */
/* --------------------------------------------------------------*/

static BOOL CopyFile(const char *src, const char *dst)
{
    BPTR   srcFH = 0, dstFH = 0;
    LONG   nRead, nWritten;
    BOOL   ok = FALSE;
    struct FileInfoBlock *fib;
    BPTR   lock;

    fib = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL);
    if (!fib)
    {
        ERR("SaferCopy: AllocDosObject echoue\n");
        return FALSE;
    }

    /* --- Infos source : date, protection, commentaire --------- */
    lock = xLock(src, ACCESS_READ);
    if (!lock)
    {
        Fail("SaferCopy: source inaccessible", src);
        FreeDosObject(DOS_FIB, fib);
        return FALSE;
    }
    if (!Examine(lock, fib))
    {
        Fail("SaferCopy: Examine echoue", src);
        UnLock(lock);
        FreeDosObject(DOS_FIB, fib);
        return FALSE;
    }
    UnLock(lock);

    /* --- UPDATE : faut-il copier ? ---------------------------- */
    if (G.update && !NeedsCopy(src, dst))
    {
        if (!G.quiet)
            OUT("  Ignore  %s (a jour)\n", src);
        G.nSkipped++;
        FreeDosObject(DOS_FIB, fib);
        return TRUE;
    }

    if (!G.quiet)
    {
        OUT("Copie   %s -> %s\n", src, dst);

        /* UPDATE : si la destination existe avec une taille differente,
         * c'est probablement un echec de copie precedent. Le signaler. */
        if (G.update)
        {
            BPTR dl = xLock(dst, ACCESS_READ);
            if (dl)
            {
                struct FileInfoBlock *df =
                    (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL);
                if (df)
                {
                    if (Examine(dl, df) && df->fib_Size != fib->fib_Size)
                        OUT("  [dest incomplete : %ld/%ld octets]\n",
                            (long)df->fib_Size, (long)fib->fib_Size);
                    FreeDosObject(DOS_FIB, df);
                }
                UnLock(dl);
            }
        }
    }

    /* --- FORCE : retirer protection en ecriture --------------- */
    if (G.force)
        xProt(dst, 0);

    /* --- Ouverture -------------------------------------------- */
    srcFH = xOpen(src, MODE_OLDFILE);
    if (!srcFH)
    {
        Fail("SaferCopy: impossible d ouvrir", src);
        goto done;
    }
    dstFH = xOpen(dst, MODE_NEWFILE);
    if (!dstFH)
    {
        Fail("SaferCopy: impossible de creer", dst);
        goto done;
    }

    /* --- Boucle de copie par blocs ---------------------------- */
    ok = TRUE;
    while ((nRead = Read(srcFH, G.buf, G.bufSize)) > 0)
    {
        nWritten = Write(dstFH, G.buf, nRead);
        if (nWritten != nRead)
        {
            /*
             * Write() partiel sur FAT95/Poseidon : arrive sans IoErr()
             * coherent. On abandonne et on supprime le fichier corrompu.
             */
            ERR("SaferCopy: ECRITURE INCOMPLETE sur %s "
                "(voulu %ld, ecrit %ld) - fichier supprime!\n",
                dst, (long)nRead, (long)nWritten);
            ok = FALSE;
            break;
        }
    }
    if (nRead < 0)
    {
        Fail("SaferCopy: erreur de lecture", src);
        ok = FALSE;
    }

    /*
     * Close() AVANT verify, flush
     * Sans Close(), ecrituree physique impossible pour la cache USB.
     */
    Close(dstFH); dstFH = 0;
    Close(srcFH); srcFH = 0;

    if (!ok)
    {
        xDel(dst);
        goto done;
    }

    /* --- VERIFY : relire dst et comparer octet par octet ------ */
    if (G.verify)
    {
        if (!G.quiet)
        {
            OUT("  Verify  %s ...", dst);
            fflush(stdout);
        }

        if (!VerifyFiles(src, dst))
        {
            if (!G.quiet) OUT(" ECHEC!\n");
            Fail("SaferCopy: verification echouee - fichier supprime", dst);
            xDel(dst);
            ok = FALSE;
            goto done;
        }
        if (!G.quiet) OUT(" OK\n");
    }

    /* --- DATES / CLONE --------------------------------------- */
    if (G.dates)
    {
        /*
         * Bug Copy 47.7 confirme : SetFileDate n est jamais appele
         * quand DATES est specifie sans CLONE.
         */
        if (!xDate(dst, &fib->fib_Date))
            Fail("SaferCopy: avertissement SetFileDate echoue", dst);
    }
    if (G.clone && !G.nopro)
        xProt(dst, fib->fib_Protection);

    if (G.clone && fib->fib_Comment[0])
        xComm(dst, fib->fib_Comment);

    G.nCopied++;

done:
    if (srcFH) Close(srcFH);
    if (dstFH) Close(dstFH);
    FreeDosObject(DOS_FIB, fib);
    return ok;
}

/* --------------------------------------------------------------*/
/*    VerifyFiles -- relecture src+dst, comparaison bloc/bloc    */
/* --------------------------------------------------------------*/

static BOOL VerifyFiles(const char *src, const char *dst)
{
    BPTR srcFH = 0, dstFH = 0;
    LONG rSrc, rDst;
    BOOL ok = FALSE;

    srcFH = xOpen(src, MODE_OLDFILE);
    dstFH = xOpen(dst, MODE_OLDFILE);
    if (!srcFH || !dstFH) goto done;

    ok = TRUE;
    for (;;)
    {
        rSrc = Read(srcFH, G.buf,  G.bufSize);
        rDst = Read(dstFH, G.vbuf, G.bufSize);

        if (rSrc < 0 || rDst < 0)
        {
            ERR("\nSaferCopy: verify - erreur de lecture\n");
            ok = FALSE; break;
        }
        if (rSrc != rDst)
        {
            ERR("\nSaferCopy: verify - tailles differentes (%ld vs %ld)\n",
                (long)rSrc, (long)rDst);
            ok = FALSE; break;
        }
        if (rSrc == 0) break;   /* EOF propre */

        if (memcmp(G.buf, G.vbuf, (size_t)rSrc) != 0)
        {
            ERR("\nSaferCopy: verify - contenu different!\n");
            ok = FALSE; break;
        }
    }

done:
    if (srcFH) Close(srcFH);
    if (dstFH) Close(dstFH);
    return ok;
}

/* --------------------------------------------------------------*/
/*  NeedsCopy -- UPDATE : TRUE = copie necessaire                */
/* --------------------------------------------------------------*/

static BOOL NeedsCopy(const char *src, const char *dst)
{
    struct FileInfoBlock *fibSrc;
    struct FileInfoBlock *fibDst;
    BPTR lockSrc = 0, lockDst = 0;
    BOOL needed = TRUE;

    fibSrc = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL);
    fibDst = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL);
    if (!fibSrc || !fibDst) goto done;

    lockSrc = xLock(src, ACCESS_READ);
    lockDst = xLock(dst, ACCESS_READ);
    if (!lockSrc || !lockDst) goto done;   /* dst absent -> copie */

    if (!Examine(lockSrc, fibSrc)) goto done;
    if (!Examine(lockDst, fibDst)) goto done;

    if (fibSrc->fib_Size != fibDst->fib_Size) goto done;

    {
        struct DateStamp *ds = &fibSrc->fib_Date;
        struct DateStamp *dd = &fibDst->fib_Date;
        if (ds->ds_Days   != dd->ds_Days   ||
            ds->ds_Minute != dd->ds_Minute ||
            ds->ds_Tick   != dd->ds_Tick)
            goto done;
    }

    needed = FALSE;   /* meme taille et meme date : rien a faire */

done:
    if (lockSrc) UnLock(lockSrc);
    if (lockDst) UnLock(lockDst);
    if (fibSrc)  FreeDosObject(DOS_FIB, fibSrc);
    if (fibDst)  FreeDosObject(DOS_FIB, fibDst);
    return needed;
}

/* --------------------------------------------------------------*/
/*  Utilitaires                                                  */
/* --------------------------------------------------------------*/

static BOOL IsDir(const char *path, struct FileInfoBlock *fib_out)
{
    struct FileInfoBlock *fib;
    BPTR lock;
    BOOL isDir = FALSE;

    lock = xLock(path, ACCESS_READ);
    if (!lock) return FALSE;

    fib = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL);
    if (fib)
    {
        if (Examine(lock, fib))
        {
            isDir = (fib->fib_DirEntryType > 0);
            if (fib_out) *fib_out = *fib;
        }
        FreeDosObject(DOS_FIB, fib);
    }
    UnLock(lock);
    return isDir;
}

static BOOL EnsureDir(const char *path)
{
    if (IsDir(path, NULL)) return TRUE;
    return xExDir(path);
}

static void JoinPath(char *out, LONG outlen,
                     const char *dir, const char *leaf)
{
    strncpy(out, dir, (size_t)(outlen - 1));
    out[outlen - 1] = '\0';
    xAddPart(out, leaf, (ULONG)outlen);
}

/*
 * Fail : construit le message d'erreur et l'ajoute au buffer.
 * N'affiche RIEN immediatement : les messages arrivent en bloc
 * a la fin via PrintErrors().
 */
static void Fail(const char *msg, const char *arg)
{
    char line[384];
    char fault[96];
    LONG err = IoErr();
    int  used, avail;

    if (arg)
        sprintf(line, "%s: %s", msg, arg);
    else
        sprintf(line, "%s", msg);

    if (err && Fault(err, NULL, (STRPTR)fault, (LONG)sizeof(fault))) {
        LONG cur = (LONG)strlen(line);
        /* sprintf sur line+cur, en s'assurant de ne pas deborder */
        sprintf(line + cur, " (%s)", fault);
    }

    /* Affichage immediat : l'utilisateur voit l'erreur en temps reel */
    ERR("%s\n", line);

    /* Ajout au buffer d'erreurs (pour le recapitulatif final) ----- */
    used  = (int)strlen(line);
    avail = (int)(ERR_BUF_BYTES - g_errBufUsed);
    if (used + 2 <= avail)
    {
        memcpy(g_errBuf + g_errBufUsed, line, (size_t)used);
        g_errBuf[g_errBufUsed + (ULONG)used]     = '\n';
        g_errBuf[g_errBufUsed + (ULONG)used + 1] = '\0';
        g_errBufUsed += (ULONG)used + 1;
    }
    else
    {
        g_errTrunc++;
    }
}

/*
 * PrintErrors : vide le buffer d'erreurs sur stderr.
 * Appele une seule fois, juste avant le rapport final.
 */
static void PrintErrors(void)
{
    if (g_errBufUsed == 0 && g_errTrunc == 0) return;

    ERR("\n--- Recapitulatif : %ld erreur(s) ---\n", (long)G.nErrors);
    if (g_errBufUsed > 0)
        ERR("%s", g_errBuf);
    if (g_errTrunc > 0)
        ERR("  [%ld message(s) tronques : buffer d erreurs plein]\n",
            (long)g_errTrunc);
}

/*
 * CheckAbort : verifie si on a atteint MAXERR.
 * Si oui, logue un message final et arme G.abort.
 * La boucle principale et CopyDir testent G.abort apres chaque fichier.
 */
static void CheckAbort(void)
{
    if (G.maxErr > 0 && G.nErrors >= G.maxErr && !G.abort)
    {
        G.abort   = TRUE;
        G.retCode = RETURN_ERROR;
        /* Ce message va dans le buffer, imprime avec les autres a la fin. */
        {
            char msg[128];
            sprintf(msg, "SaferCopy: MAXERR=%ld atteint - abandon", (long)G.maxErr);
            Fail(msg, NULL);
        }
    }
}

static void Die(LONG code)
{
    if (G.oldWinPtr)
    {
        struct Process *me = (struct Process *)FindTask(NULL);
        me->pr_WindowPtr = G.oldWinPtr;
    }
    if (G.buf)  FreeVec(G.buf);
    if (G.vbuf) FreeVec(G.vbuf);
    exit(code);
}

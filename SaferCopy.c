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
 *   sc SaferCopy.c LINK RESIDENT MATH=SOFT NOSTKCHK OPT STRMERGE
 *      IDIR=Include: IDIR=NDK3.2:Include
 *   RESIDENT : code reentrant (data via A4, startup cres.o) ->
 *   binaire pur, utilisable avec la commande RESIDENT du Shell.
 *
 * Compilation GCC cross (m68k-amigaos-gcc 6.x) :
 *   m68k-amigaos-gcc -O2 -m68000 -noixemul -Wall
 *      -o SaferCopy SaferCopy.c
 *   Variante PURE (residentable) : ajouter -resident (compilation
 *   ET edition de liens). Data base-relative copiee a chaque
 *   invocation, les ~26 Ko de statiques sont dupliques par
 *   instance. ATTENTION : le -resident de bebbo gcc est fragile ;
 *   valider la variante pure sur du vrai materiel (verify de
 *   masse) avant de la distribuer. Voir SaferCopy_GCC.make,
 *   cibles "pure" et "both".
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

/* --- Version ----------------------------------------------------- */
/*
 * $VER: SaferCopy 1.4.2 (2026.06.14) Nowee with Claude
 * Parsed by Version, VersionWB, and the Aminet indexer.
 */
const char * const version = "$VER: SaferCopy 1.4.2 (2026.06.14) Nowee with Claude";

/* --- Includes ---------------------------------------------------- */
#include <proto/dos.h>
#include <proto/exec.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/dosasl.h>
#include <stdio.h>     /* printf, fprintf, sprintf, fputs, fflush */
#include <stdlib.h>    /* exit                                    */
#include <string.h>    /* memcmp, strncpy, memset                 */
#include <libraries/locale.h>   /* struct Catalog                */
#include <proto/locale.h>       /* OpenCatalogA, GetCatalogStr   */
#include "SaferCopy_cat.h"      /* MSG_* IDs                     */

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

/* OS minimum : AmigaOS 2.0 (exec V36 = kick 2.0, DOS V36).
 * On exige V37 (2.04) pour ReadArgs, MatchFirst/Next, etc.
 * V37 est le minimum documenté pour l usage de ces APIs. */
#define MIN_OSVER  37

/* ------------------------------------------------------------------ */
#define TEMPLATE \
    "FROM/M,TO/A,ALL/S,QUIET/S,BUF=BUFFER/K/N," \
    "CLONE/S,DATES/S,NOPRO/S,VERIFY/S,NOREQ/S,UPDATE/S,FORCE/S," \
    "MAXERR/K/N,NDATE/S,VERBOSE/S,CHECK/S,NAMELEN/K/N,RENAME/S," \
    "PROGRESS/S"

enum {
    A_FROM=0, A_TO, A_ALL, A_QUIET, A_BUF,
    A_CLONE,
	A_DATES,
	A_NOPRO,
	A_VERIFY,
	A_NOREQ,
	A_UPDATE,
	A_FORCE,
    A_MAXERR,
    A_NDATE,
    A_VERBOSE,
    A_CHECK,
    A_NAMELEN,
    A_RENAME,
	A_PROGRESS,
    A_COUNT,
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

/* ------------------------------------------------------------------ */
/*  Locale support                                                      */
/*                                                                      */
/*  CS(id, def) : retourne la chaine traduite du catalog si disponible, */
/*  sinon la chaine anglaise de secours (def).                          */
/*  Fallback transparent si locale.library absent ou catalog non trouve.*/
/* ------------------------------------------------------------------ */
struct LocaleBase *LocaleBase = NULL;
static struct Catalog *g_catalog = NULL;

#define CS(id,def) \
    (g_catalog ? (const char *)GetCatalogStr(g_catalog,(id),(STRPTR)(def)) : (def))

static void locale_open(void)
{
    LocaleBase = (struct LocaleBase *)OpenLibrary((STRPTR)"locale.library", 38);
    if (LocaleBase)
        g_catalog = OpenCatalogA(NULL, (STRPTR)"SaferCopy.catalog", NULL);
    /* Pas d erreur si absent : on utilise les defaults anglais */
}

static void locale_close(void)
{
    if (g_catalog) { CloseCatalog(g_catalog);                      g_catalog  = NULL; }
    if (LocaleBase){ CloseLibrary((struct Library *)LocaleBase);   LocaleBase = NULL; }
}

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
    BOOL    ndate;      /* UPDATE compare taille seulement (pas date) */
    BOOL    verbose;    /* afficher les fichiers ignores (UPDATE a jour) */
    BOOL    check;      /* CHECK : audit taille seulement, pas de copie  */
	BOOL	progress;	/* PROGRESS: montre la completion d'un ficher */
    LONG    nCopied;
    LONG    nSkipped;
    LONG    nErrors;
    LONG    maxErr;     /* 0 = illimite ; >0 = abandon si nErrors >= maxErr */
    LONG    nameMax;    /* NAMELEN : 0 = off ; >0 = signaler et sauter
                           tout nom de plus de nameMax caracteres.
                           Audit des volumes PFS3 configures avec la
                           limite FFS par defaut (30) : NAMELEN=30
                           avec CHECK = inventaire sans copie.       */
    BOOL    rename;     /* RENAME : ne RIEN copier. Pour chaque nom
                           source plus long que nameMax, renommer la
                           version tronquee deja presente sur la
                           cible vers le nom complet (operation de
                           metadonnees, zero transfert). Sert a
                           reparer un backup tronque apres avoir
                           releve la limite du volume (setfsname).
                           Combine avec CHECK = simulation.          */
    LONG    nRenamed;
    LONG    nMissing;   /* RENAME : entrees absentes de la cible.
                           PAS une erreur (RENAME ne copie pas) : ces
                           fichiers sont juste a recopier par un UPDATE
                           ulterieur. Inclut les perdants de collision
                           (deux noms longs -> meme nom court). Detail
                           ligne par ligne en VERBOSE seulement.       */
    BOOL    abort;      /* mis a 1 quand maxErr est depasse          */
    LONG    retCode;
    APTR    oldWinPtr;  /* pr_WindowPtr original pour NOREQ         */
    BOOL    noreqSet;   /* TRUE si on a modifie pr_WindowPtr.
                           NE PAS tester oldWinPtr != NULL : pour un
                           CLI la valeur originale EST NULL, et les
                           commandes tournent sur le processus du
                           Shell -> sans restauration le Shell garde
                           pr_WindowPtr = -1 (plus aucun requester). */
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
static void  EscapeAmigaPattern(char *out, ULONG outlen, const char *in);
static BOOL  NeedsCopy  (const char *src, const char *dst);
static BOOL  EnsureDir  (const char *path);
static BOOL  EnsurePath (const char *path);
static BOOL  IsDir      (const char *path, struct FileInfoBlock *fib_out);

static BOOL  JoinPathSafe(char *out, LONG outlen, const char *dir,
                         const char *leaf, const char *srcPath);
static void  Fail       (const char *msg, const char *arg);
static void  ShowProgress(ULONG done, ULONG total);
static void  FailNameLen(const char *path, LONG n);
static void  RenameDir  (const char *dst, const char *leaf,
                         const char *srcPath, char *fullBuf,
                         char *truncBuf, char *descendBuf);
static void  RenameFile (const char *dst, const char *leaf,
                         const char *srcPath, char *fullBuf,
                         char *truncBuf);
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
    /* --- Verification version OS ---------------------------------- */
    /*
     * SysBase est garanti non-NULL a ce stade (libnix startup).
     * On verifie exec en premier, puis dos.
     * Pas besoin d ouvrir d autres librairies : on n utilise que
     * exec et dos dans tout SaferCopy.
     */
    {
        extern struct ExecBase *SysBase;
        extern struct DosLibrary *DOSBase;
        if (((struct Library *)SysBase)->lib_Version < MIN_OSVER ||
            ((struct Library *)DOSBase)->lib_Version < MIN_OSVER)
        {
            fprintf(stderr,
                    "SaferCopy requires AmigaOS 2.04+ (V%d)\n", MIN_OSVER);
            return RETURN_FAIL;
        }
    }

    locale_open();   /* avant ReadArgs : les messages d erreur sont deja traduits */

    rda = ReadArgs((STRPTR)TEMPLATE, args, NULL);
    if (!rda)
    {
        PrintFault(IoErr(), (STRPTR)"SaferCopy");
        locale_close();
        return RETURN_FAIL;
    }

    fromList   = (STRPTR *) args[A_FROM];
    toPath     = (const char *) args[A_TO];
    G.all      = (BOOL) args[A_ALL];
    G.quiet    = (BOOL) args[A_QUIET];
    G.clone    = (BOOL) args[A_CLONE];
    G.dates    = (BOOL) args[A_DATES] || G.clone;
    G.nopro    = (BOOL) args[A_NOPRO];
    G.verify   = (BOOL) args[A_VERIFY];
    G.update   = (BOOL) args[A_UPDATE];
    G.force    = (BOOL) args[A_FORCE];
    G.ndate    = (BOOL) args[A_NDATE];
    G.verbose  = (BOOL) args[A_VERBOSE];
    G.check    = (BOOL) args[A_CHECK];
	G.progress = (BOOL) args[A_PROGRESS];

    /*
     * MAXERR : 0 = pas de limite (defaut).
     * MAXERR=10 -> abandonne apres 10 erreurs.
     */
    G.maxErr = args[A_MAXERR] ? *(LONG *)args[A_MAXERR] : 0;

    /* NAMELEN : 0 = off. NAMELEN=30 -> signale tout nom plus long. */
    G.nameMax = args[A_NAMELEN] ? *(LONG *)args[A_NAMELEN] : 0;
    G.rename  = (BOOL) args[A_RENAME];

    if (G.rename && G.nameMax <= 0)
    {
        ERR(CS(MSG_RENAME_NEEDS, "SaferCopy: RENAME requires NAMELEN=<n>\n"));
        FreeArgs(rda);
        Die(RETURN_FAIL);
    }

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
        G.noreqSet       = TRUE;
        me->pr_WindowPtr = (APTR)-1L;
    }

    /* --- Allocation buffers ---------------------------------------- */
    G.buf = (UBYTE *)AllocVec(G.bufSize, MEMF_ANY);
    if (G.verify)
        G.vbuf = (UBYTE *)AllocVec(G.bufSize, MEMF_ANY);

    if (!G.buf || (G.verify && !G.vbuf))
    {
        ERR(CS(MSG_NO_MEM_BUF, "SaferCopy: cannot allocate %lu bytes for buffer\n"),
            G.verify
            ? (unsigned long)(G.bufSize * 2)
            : (unsigned long)(G.bufSize));
        FreeArgs(rda);
        Die(RETURN_FAIL);
    }

    if (!G.quiet) {
        OUT("SaferCopy: buffer %lu KB%s%s%s%s%s%s",
            (unsigned long)(G.bufSize / 1024),
            G.verify  ? CS(MSG_OPT_VERIFY,  ", VERIFY")  : "",
            G.dates   ? CS(MSG_OPT_DATES,   ", DATES")   : "",
            G.update  ? CS(MSG_OPT_UPDATE,  ", UPDATE")  : "",
            G.verbose ? CS(MSG_OPT_VERBOSE, ", VERBOSE") : "",
            G.check   ? CS(MSG_OPT_CHECK,   ", CHECK")   : "",
            G.rename  ? CS(MSG_OPT_RENAME,  ", RENAME")  : "");
        if (G.nameMax > 0)
            OUT(CS(MSG_OPT_NAMELEN, ", NAMELEN=%ld"), (long)G.nameMax);
        if (G.maxErr > 0)
            OUT(CS(MSG_OPT_MAXERR, ", MAXERR=%ld"), (long)G.maxErr);
        OUT("\n");
    }

    /* --- Verification arguments ------------------------------------ */
    if (!fromList || !fromList[0])
    {
        ERR(CS(MSG_FROM_REQUIRED, "SaferCopy: FROM is required\n"));
        FreeArgs(rda);
        Die(RETURN_FAIL);
    }

    /* --- Destination ---------------------------------------------- */
    toIsDir = IsDir(toPath, NULL);

    if (fromList[0] && fromList[1] && !toIsDir)
    {
        if (!EnsurePath(toPath))
        {
            Fail(CS(MSG_NO_MKDIR_DEST, "SaferCopy: cannot create destination directory"),
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
        char        tdst[MAXPATH];   /* RENAME : chemin tronque       */

        if (IsDir(src, NULL))
        {
            if (!G.all)
            {
                if (!G.quiet)
                    OUT(CS(MSG_IS_DIR_USE_ALL, "SaferCopy: '%s' is a directory - use ALL\n"),
                        src);
                G.nErrors++;
                G.retCode = RETURN_WARN;
                continue;
            }
            if (toIsDir)
            {
                /* RENAME ne tronque pas le chemin de tete : on garde le
                 * nom complet et on laisse CopyDir resoudre par niveau. */
                if (G.rename)
                    JoinPathSafe(dst, MAXPATH, toPath, xPart(src), NULL);
                else if (!JoinPathSafe(dst, MAXPATH, toPath, xPart(src), src))
                {
                    G.nErrors++;
                    G.retCode = RETURN_WARN;
                    CheckAbort();
                    continue;
                }
            }
            else
            {
                strncpy(dst, toPath, (size_t)(MAXPATH - 1));
                dst[MAXPATH - 1] = '\0';
            }
            /* RENAME ne cree aucun repertoire : la cible existe deja. */
            if (!G.rename && !EnsurePath(dst))
            {
                Fail(CS(MSG_NO_MKDIR, "SaferCopy: cannot create"), dst);
                G.nErrors++;
                G.retCode = RETURN_WARN;
                CheckAbort();
                continue;
            }
            CopyDir(src, dst);
        }
        else if (G.rename)
        {
            /* Fichier isole en mode RENAME : la cible doit etre un dir. */
            if (!toIsDir)
            {
                Fail(CS(MSG_RENAME_NEEDS_DIR,
                        "SaferCopy: RENAME of a single file needs a directory TO"),
                     src);
                G.nErrors++;
                G.retCode = RETURN_WARN;
                CheckAbort();
                continue;
            }
            RenameFile(toPath, xPart(src), src, dst, tdst);
        }
        else
        {
            if (toIsDir)
            {
                if (!JoinPathSafe(dst, MAXPATH, toPath, xPart(src), src))
                {
                    G.nErrors++;
                    G.retCode = RETURN_WARN;
                    CheckAbort();
                    continue;
                }
            }
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

    if (G.rename)
    {
        if (!G.quiet || G.nErrors > 0)
            OUT(CS(MSG_RENAME_REPORT,
                   "SaferCopy: %ld renamed  %ld skipped  %ld missing  %ld error(s)%s\n"),
                (long)G.nRenamed, (long)G.nSkipped, (long)G.nMissing, (long)G.nErrors,
                G.abort ? CS(MSG_ABORT_TAG, "  [ABORT: MAXERR reached]") : "");
    }
    else if (!G.quiet || G.nErrors > 0)
        OUT(CS(MSG_FINAL_REPORT, "SaferCopy: %ld copied  %ld skipped  %ld error(s)%s\n"),
            (long)G.nCopied, (long)G.nSkipped, (long)G.nErrors,
            G.abort ? CS(MSG_ABORT_TAG, "  [ABORT: MAXERR reached]") : "");

    FreeArgs(rda);
    Die(G.retCode);
    return G.retCode;
}

/* --------------------------------------------------------------*/
/*  EscapeAmigaPattern                                                  */
/*                                                                      */
/*  Echappe les caracteres speciaux AmigaDOS ParsePattern dans un       */
/*  chemin afin qu il soit interprete litteralement par MatchFirst.     */
/*                                                                      */
/*  Caracteres speciaux (prefixes par apostrophe) :                     */
/*    # ? ( ) | ~ [ ] % ' &                                             */
/*  Separateurs de chemin non echappes : / :                            */
/*                                                                      */
/*  Exemple :                                                           */
/*    "Games/Quick&Silva"  ->  "Games/Quick'&Silva"                     */
/*    "Work:foo(bar)"      ->  "Work:foo'(bar')"                        */
/* --------------------------------------------------------------*/

static void EscapeAmigaPattern(char *out, ULONG outlen, const char *in)
{
    /* Tous les metacaracteres ParsePattern sauf / et : (separateurs). */
    static const char specials[] = "#?()|~[]%'&";
    ULONG o = 0;
    const char *p;
    const char *s;

    for (p = in; *p && o + 2 < outlen; p++)
    {
        for (s = specials; *s; s++)
        {
            if (*p == *s)
            {
                out[o++] = '\'';   /* echappement AmigaDOS */
                break;
            }
        }
        if (o < outlen - 1)
            out[o++] = *p;
    }
    out[o] = '\0';
}

/* --------------------------------------------------------------*/
/*  ValidFibName                                                        */
/*                                                                      */
/*  Validation defensive du nom retourne par MatchFirst/MatchNext.     */
/*  Une entree de repertoire corrompue (vu en vrai sur PFS3 :          */
/*  "filename too long" + "corrupt directory entry" detectes par       */
/*  PFSDoctor) peut produire un fib_FileName non termine, vide, ou     */
/*  contenant des separateurs. Copier un tel nom PROPAGE la            */
/*  corruption : le handler accepte de recreer le nom a destination    */
/*  sans le valider. On refuse, on signale, on saute.                  */
/*  fib_FileName fait 108 octets ; les FS Amiga acceptent au plus      */
/*  107 caracteres (30 pour FFS).                                      */
/* --------------------------------------------------------------*/

static BOOL ValidFibName(const char *name)
{
    LONG i;

    for (i = 0; i < 108; i++)
    {
        char c = name[i];
        if (c == '\0')
            return (BOOL)(i > 0);   /* nom vide = invalide          */
        if (c == '/' || c == ':')
            return FALSE;           /* separateur dans un nom       */
    }
    return FALSE;                   /* 108 octets sans zero final   */
}

/* --------------------------------------------------------------*/
/*  RENAME : reparer un backup tronque                                 */
/*                                                                      */
/*  Contexte : un backup a ete fait sur un volume PFS3 limite a 30      */
/*  caracteres (defaut FFS), les noms longs ont donc ete tronques      */
/*  (par un outil de copie quelconque). Le volume a depuis ete         */
/*  reconfigure pour les noms longs (setfsname). Le contenu est deja la,*/
/*  sous le nom court : on RENOMME vers le nom complet. Aucun octet     */
/*  n est recopie. Recopier aurait tout retransfere ET laisse les       */
/*  versions courtes en orphelins.                                      */
/*                                                                      */
/*  Politique de securite :                                            */
/*    - jamais d ecrasement : on ne renomme que si le nom complet      */
/*      n existe pas deja sur la cible.                                 */
/*    - jamais de suppression.                                         */
/*    - CHECK + RENAME = simulation : on affiche, on ne touche rien.   */
/*  Collisions : deux noms longs partageant les nameMax premiers       */
/*  caracteres pointent vers le meme nom court. Le premier renomme le   */
/*  consomme ; le second est alors signale Missing (a recopier par un   */
/*  UPDATE ulterieur).                                                  */
/* --------------------------------------------------------------*/

/* Construit truncBuf = dst / (nameMax premiers caracteres de leaf). */
static void BuildTrunc(char *truncBuf, const char *dst, const char *leaf)
{
    char t[108];
    memcpy(t, leaf, (size_t)G.nameMax);
    t[G.nameMax] = '\0';
    JoinPathSafe(truncBuf, MAXPATH, dst, t, NULL);
}

/*
 * RenameDir : resout un sous-repertoire en mode RENAME et indique
 * dans descendBuf le repertoire destination ou recurser (vide si
 * rien a explorer).
 */
static void RenameDir(const char *dst, const char *leaf,
                      const char *srcPath, char *fullBuf,
                      char *truncBuf, char *descendBuf)
{
    LONG nl = (LONG)strlen(leaf);
    BPTR l;

    JoinPathSafe(fullBuf, MAXPATH, dst, leaf, NULL);
    descendBuf[0] = '\0';

    /* Nom non tronque : repertoire normal, on descend s il existe.
     * Absent = sous-arbre pas sur la cible : ce n est PAS une erreur en
     * mode RENAME (on ne copie pas), juste un manque informatif. */
    if (nl <= G.nameMax)
    {
        l = xLock(fullBuf, ACCESS_READ);
        if (l) { UnLock(l); strcpy(descendBuf, fullBuf); }
        else
        {
            if (G.verbose)
                OUT(CS(MSG_CHECK_MISSING, "  Missing  %s\n"), srcPath);
            G.nMissing++;
        }
        return;
    }

    /* Nom long : deja au nom complet sur la cible ? */
    l = xLock(fullBuf, ACCESS_READ);
    if (l) { UnLock(l); strcpy(descendBuf, fullBuf); return; }

    /* Sinon, chercher la version tronquee. */
    BuildTrunc(truncBuf, dst, leaf);
    l = xLock(truncBuf, ACCESS_READ);
    if (!l)
    {
        /* VERBOSE : afficher AUSSI le chemin tronque cherche, pour
         * diagnostiquer une non-correspondance de prefixe. */
        if (G.verbose)
            OUT(CS(MSG_RENAME_NOTRUNC, "  Missing  %s  (looked for: %s)\n"),
                srcPath, truncBuf);
        G.nMissing++;
        return;
    }
    UnLock(l);

    if (G.check)   /* simulation : on descend dans le tronque pour le rapport */
    {
        OUT(CS(MSG_WOULD_RENAME, "  WouldRen %s -> %s\n"), truncBuf, fullBuf);
        strcpy(descendBuf, truncBuf);
        return;
    }

    if (Rename((STRPTR)truncBuf, (STRPTR)fullBuf))
    {
        /* VERBOSE seulement : a 100 000 renommages, une ligne chacun
         * noierait la console (goulot d affichage, l Amiga parait fige).
         * Par defaut on ne sort que le bilan final. */
        if (G.verbose)
            OUT(CS(MSG_RENAMED, "  Rename  %s -> %s\n"), truncBuf, fullBuf);
        G.nRenamed++;
        strcpy(descendBuf, fullBuf);   /* descendre dans le nom corrige */
    }
    else
    {
        Fail(CS(MSG_RENAME_FAIL, "SaferCopy: rename failed"), fullBuf);
        G.nErrors++; G.retCode = RETURN_WARN; CheckAbort();
        strcpy(descendBuf, truncBuf);  /* renommage rate : continuer quand meme */
    }
}

/*
 * RenameFile : promeut un fichier tronque vers son nom complet.
 * Les noms <= nameMax n ont jamais ete tronques : rien a faire.
 */
static void RenameFile(const char *dst, const char *leaf,
                       const char *srcPath, char *fullBuf, char *truncBuf)
{
    LONG nl = (LONG)strlen(leaf);
    BPTR l;

    if (nl <= G.nameMax) return;

    JoinPathSafe(fullBuf, MAXPATH, dst, leaf, NULL);

    l = xLock(fullBuf, ACCESS_READ);
    if (l) { UnLock(l); G.nSkipped++; return; }   /* deja au nom complet */

    BuildTrunc(truncBuf, dst, leaf);
    l = xLock(truncBuf, ACCESS_READ);
    if (!l)
    {
        /* Ni nom long ni nom court : pas sur la cible (ou perdant de
         * collision). Pas une erreur : a recopier par un UPDATE.
         * VERBOSE montre le chemin tronque cherche (diagnostic prefixe). */
        if (G.verbose)
            OUT(CS(MSG_RENAME_NOTRUNC, "  Missing  %s  (looked for: %s)\n"),
                srcPath, truncBuf);
        G.nMissing++;
        return;
    }
    UnLock(l);

    if (G.check)
    {
        OUT(CS(MSG_WOULD_RENAME, "  WouldRen %s -> %s\n"), truncBuf, fullBuf);
        return;
    }

    if (Rename((STRPTR)truncBuf, (STRPTR)fullBuf))
    {
        /* VERBOSE seulement : a 100 000 renommages, une ligne chacun
         * noierait la console (goulot d affichage, l Amiga parait fige).
         * Par defaut on ne sort que le bilan final. */
        if (G.verbose)
            OUT(CS(MSG_RENAMED, "  Rename  %s -> %s\n"), truncBuf, fullBuf);
        G.nRenamed++;
    }
    else
    {
        Fail(CS(MSG_RENAME_FAIL, "SaferCopy: rename failed"), fullBuf);
        G.nErrors++; G.retCode = RETURN_WARN; CheckAbort();
    }
}

/* --------------------------------------------------------------*/
/*  CopyDir                                                             */
/* --------------------------------------------------------------*/

/*
 * Buffers de travail de CopyDir.
 *
 * CRITIQUE : CopyDir est recursive. Mettre ces buffers (~2 Ko) sur la
 * PILE a chaque niveau faisait deborder la pile sur les arbres profonds.
 * Sur 68k il n y a PAS de page de garde MMU : un debordement de pile
 * n envoie pas un Guru propre, il ecrase silencieusement les structures
 * d exec (liste des taches, etc.) -> lockup TOTAL, reset obligatoire.
 *
 * On alloue donc AnchorPath + buffers en un seul bloc sur le TAS.
 * La pile ne porte plus que des pointeurs (~100 octets / niveau) :
 * 32 Ko de pile encaissent maintenant 100+ niveaux de profondeur.
 *
 * Disposition du bloc :
 *   [ AnchorPath ][ MAXPATH : espace ap_Buf ][ DirBufs ]
 * L espace MAXPATH doit suivre immediatement AnchorPath car MatchFirst
 * y ecrit le chemin resolu (ap_Strlen octets).
 */
struct DirBufs {
    char pat[MAXPATH * 2 + 8];   /* x2 : pire cas echappement      */
    char subdst[MAXPATH];        /* passe 1 : sous-repertoire dest  */
    char dstfile[MAXPATH];       /* passe 2 : fichier dest          */
    char truncdst[MAXPATH];      /* RENAME : chemin tronque cible   */
    char descend[MAXPATH];       /* RENAME : dir ou recurser        */
};

static void CopyDir(const char *src, const char *dst)
{
    struct AnchorPath *ap;
    struct DirBufs    *db;
    UBYTE             *blk;
    LONG               rc;

    blk = (UBYTE *)AllocVec(sizeof(struct AnchorPath) + MAXPATH
                            + sizeof(struct DirBufs),
                            MEMF_CLEAR | MEMF_ANY);
    if (!blk)
    {
        ERR(CS(MSG_NO_ANCHORPATH, "SaferCopy: out of memory (AnchorPath)\n"));
        G.nErrors++;
        return;
    }
    ap = (struct AnchorPath *)blk;
    db = (struct DirBufs *)(blk + sizeof(struct AnchorPath) + MAXPATH);
    ap->ap_Strlen = MAXPATH;

    /*
     * Construire le pattern avec EscapeAmigaPattern + AddPart.
     *
     * PROBLEME : les noms de repertoires peuvent contenir des caracteres
     * speciaux pour ParsePattern (ex: & | ~ # ? ( ) [ ] ' %).
     * Exemple : "TheAdventuresOfQuick&Silva" -> ParsePattern interprete
     * '&' comme un operateur ET -> lockup garanti.
     *
     * SOLUTION : echapper chaque caractere special avec une apostrophe
     * avant d y ajouter le pattern /#? .
     *
     * Les separateurs / et : ne sont PAS echappes (ce sont des chemins).
     */
    EscapeAmigaPattern(db->pat, (ULONG)sizeof(db->pat) - 4, src);
    xAddPart(db->pat, "#?", (ULONG)sizeof(db->pat));

    /* Passe 1 : sous-repertoires (recurse en premier) */
    rc = MatchFirst((STRPTR)db->pat, ap);
    while (rc == 0 && !G.abort)
    {
        if (ap->ap_Info.fib_DirEntryType > 0)
        {
            if (!ValidFibName((const char *)ap->ap_Info.fib_FileName))
            {
                Fail(CS(MSG_BAD_DIRENTRY,
                        "SaferCopy: corrupt directory entry - skipped"),
                     (const char *)ap->ap_Buf);
                G.nErrors++;
                G.retCode = RETURN_WARN;
                CheckAbort();
                rc = MatchNext(ap);
                continue;
            }
            if (G.rename)
            {
                RenameDir(dst, (const char *)ap->ap_Info.fib_FileName,
                          (const char *)ap->ap_Buf,
                          db->subdst, db->truncdst, db->descend);
                if (db->descend[0])
                    CopyDir((const char *)ap->ap_Buf, db->descend);
                rc = MatchNext(ap);
                continue;
            }
            if (!JoinPathSafe(db->subdst, MAXPATH, dst,
                             (const char *)ap->ap_Info.fib_FileName,
                             (const char *)ap->ap_Buf))
            {
                G.nErrors++;
                G.retCode = RETURN_WARN;
                CheckAbort();
                rc = MatchNext(ap);
                continue;
            }
            if (G.check)
            {
                /* Mode CHECK : ne pas creer le repertoire.
                 * On recurse quand meme : les fichiers manquants seront
                 * signales par le bloc CHECK de la passe 2. */
                CopyDir((const char *)ap->ap_Buf, db->subdst);
            }
            else if (!EnsureDir(db->subdst))
            {
                Fail(CS(MSG_NO_MKDIR, "SaferCopy: cannot create"), db->subdst);
                G.nErrors++;
                CheckAbort();
            }
            else
            {
                CopyDir((const char *)ap->ap_Buf, db->subdst);
            }
        }
        rc = MatchNext(ap);
    }
    MatchEnd(ap);

    /*
     * Reinitialiser l AnchorPath avant le second MatchFirst.
     * L autodoc dos.library exige une structure mise a zero ;
     * reutiliser l etat laisse par MatchEnd est indefini (AChain
     * residuels -> fuite memoire progressive, scan corrompu).
     * On efface aussi la zone ap_Buf (MAXPATH octets apres la
     * structure) ; db->pat, situe au-dela, n est pas touche.
     */
    memset(ap, 0, sizeof(struct AnchorPath) + MAXPATH);
    ap->ap_Strlen = MAXPATH;

    /* Passe 2 : fichiers */
    rc = MatchFirst((STRPTR)db->pat, ap);
    while (rc == 0 && !G.abort)
    {
        if (ap->ap_Info.fib_DirEntryType < 0)
        {
            if (!ValidFibName((const char *)ap->ap_Info.fib_FileName))
            {
                Fail(CS(MSG_BAD_DIRENTRY,
                        "SaferCopy: corrupt directory entry - skipped"),
                     (const char *)ap->ap_Buf);
                G.nErrors++;
                G.retCode = RETURN_WARN;
                CheckAbort();
                rc = MatchNext(ap);
                continue;
            }
            if (G.rename)
            {
                RenameFile(dst, (const char *)ap->ap_Info.fib_FileName,
                           (const char *)ap->ap_Buf,
                           db->dstfile, db->truncdst);
                rc = MatchNext(ap);
                continue;
            }
            if (!JoinPathSafe(db->dstfile, MAXPATH, dst,
                             (const char *)ap->ap_Info.fib_FileName,
                             (const char *)ap->ap_Buf))
            {
                G.nErrors++;
                G.retCode = RETURN_WARN;
                CheckAbort();
                rc = MatchNext(ap);
                continue;
            }

            if (G.check)
            {
                /* CHECK inline : MatchFirst a deja fourni fib_Size de la
                 * source -> zero re-lock source, un seul Lock destination.
                 * Evite de doubler les packets DOS/Poseidon par fichier. */
                BPTR dl = xLock(db->dstfile, ACCESS_READ);
                if (!dl)
                {
                    OUT(CS(MSG_CHECK_MISSING, "  Missing  %s\n"),
                        (char *)ap->ap_Buf);
                    G.nErrors++;
                    G.retCode = RETURN_WARN;
                    CheckAbort();
                }
                else
                {
                    struct FileInfoBlock *df =
                        (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL);
                    if (df && Examine(dl, df))
                    {
                        if (df->fib_Size == ap->ap_Info.fib_Size)
                        {
                            if (G.verbose)
                                OUT(CS(MSG_CHECK_OK, "  OK       %s\n"),
                                    (char *)ap->ap_Buf);
                            G.nSkipped++;
                        }
                        else
                        {
                            OUT(CS(MSG_CHECK_MISMATCH,
                                   "  SizeDiff %s  (src=%ld  dst=%ld)\n"),
                                (char *)ap->ap_Buf,
                                (long)ap->ap_Info.fib_Size,
                                (long)df->fib_Size);
                            G.nErrors++;
                            G.retCode = RETURN_WARN;
                            CheckAbort();
                        }
                    }
                    if (df) FreeDosObject(DOS_FIB, df);
                    UnLock(dl);
                }
            }
            else if (!CopyFile((const char *)ap->ap_Buf, db->dstfile))
            {
                G.nErrors++;
                G.retCode = RETURN_WARN;
                CheckAbort();
            }
        }
        rc = MatchNext(ap);
    }
    MatchEnd(ap);

    FreeVec(blk);
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
    ULONG  bytesCopied;     /* PROGRESS : octets ecrits jusqu ici */
    BOOL   showProgress;    /* PROGRESS actif pour ce fichier ?   */

    fib = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL);
    if (!fib)
    {
        ERR(CS(MSG_ALLOC_DOS_FAILED, "SaferCopy: AllocDosObject failed\n"));
        return FALSE;
    }

    /* --- Infos source : date, protection, commentaire --------- */
    lock = xLock(src, ACCESS_READ);
    if (!lock)
    {
        Fail(CS(MSG_SRC_INACCESSIBLE, "SaferCopy: source not accessible"), src);
        FreeDosObject(DOS_FIB, fib);
        return FALSE;
    }
    if (!Examine(lock, fib))
    {
        Fail(CS(MSG_EXAMINE_FAILED, "SaferCopy: Examine failed"), src);
        UnLock(lock);
        FreeDosObject(DOS_FIB, fib);
        return FALSE;
    }
    UnLock(lock);

    /* --- NAMELEN : filet de securite sur le nom DESTINATION ----- */
    /* Les chemins venant de CopyDir ou de la boucle principale     */
    /* sont deja passes par JoinPathLim (filtres ou tronques).      */
    /* Ne reste que le cas TO=nom explicite trop long.              */
    if (G.nameMax > 0)
    {
        LONG n = (LONG)strlen(xPart(dst));
        if (n > G.nameMax)
        {
            FailNameLen(dst, n);
            FreeDosObject(DOS_FIB, fib);
            return FALSE;
        }
    }

    /* --- CHECK : audit par taille uniquement, pas de copie ---- */
    if (G.check)
    {
        BPTR              dl = xLock(dst, ACCESS_READ);
        struct FileInfoBlock *df;
        BOOL              ok = FALSE;

        if (!dl)
        {
            OUT(CS(MSG_CHECK_MISSING, "  Missing  %s\n"), src);
            G.nErrors++;
            FreeDosObject(DOS_FIB, fib);
            return FALSE;
        }
        df = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL);
        if (df && Examine(dl, df))
        {
            if (df->fib_Size == fib->fib_Size)
            {
                if (G.verbose)
                    OUT(CS(MSG_CHECK_OK, "  OK       %s\n"), src);
                G.nSkipped++;
                ok = TRUE;
            }
            else
            {
                OUT(CS(MSG_CHECK_MISMATCH,
                       "  SizeDiff %s  (src=%ld  dst=%ld)\n"),
                    src, (long)fib->fib_Size, (long)df->fib_Size);
                G.nErrors++;
            }
        }
        if (df) FreeDosObject(DOS_FIB, df);
        UnLock(dl);
        FreeDosObject(DOS_FIB, fib);
        return ok;
    }

    /* --- UPDATE : faut-il copier ? ---------------------------- */
    if (G.update && !NeedsCopy(src, dst))
    {
        if (G.verbose)
            OUT(CS(MSG_IGNORED, "  Skip    %s (up to date)\n"), src);
        G.nSkipped++;
        FreeDosObject(DOS_FIB, fib);
        return TRUE;
    }

    if (!G.quiet)
    {
        OUT(CS(MSG_COPYING, "Copy    %s -> %s\n"), src, dst);

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
                        OUT(CS(MSG_DEST_INCOMPLETE, "  [dest incomplete: %ld/%ld bytes]\n"),
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
        Fail(CS(MSG_CANNOT_OPEN, "SaferCopy: cannot open"), src);
        goto done;
    }
    dstFH = xOpen(dst, MODE_NEWFILE);
    if (!dstFH)
    {
        Fail(CS(MSG_CANNOT_CREATE, "SaferCopy: cannot create"), dst);
        goto done;
    }

    /* --- Boucle de copie par blocs ---------------------------- */
    ok = TRUE;
    bytesCopied  = 0;
    showProgress = G.progress && !G.quiet &&
                   (fib->fib_Size > (2L * 1024L * 1024L));
    while ((nRead = Read(srcFH, G.buf, G.bufSize)) > 0)
    {
        nWritten = Write(dstFH, G.buf, nRead);
        if (nWritten != nRead)
        {
            /*
             * Write() partiel sur FAT95/Poseidon : arrive sans IoErr()
             * coherent. On abandonne et on supprime le fichier corrompu.
             */
            ERR(CS(MSG_WRITE_INCOMPLETE,
                   "SaferCopy: INCOMPLETE WRITE %s (wanted %ld, wrote %ld) - file deleted!\n"),
                dst, (long)nRead, (long)nWritten);
            ok = FALSE;
            break;
        }
        bytesCopied += (ULONG)nWritten;
        if (showProgress)
            ShowProgress(bytesCopied, (ULONG)fib->fib_Size);
    }
    if (showProgress)
        OUT("\n");   /* passage a la ligne une fois le fichier termine */
    if (nRead < 0)
    {
        Fail(CS(MSG_READ_ERROR, "SaferCopy: read error"), src);
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
            OUT(CS(MSG_VERIFYING, "  Verify  %s ..."), dst);
            fflush(stdout);
        }

        if (!VerifyFiles(src, dst))
        {
            if (!G.quiet) OUT(CS(MSG_VERIFY_FAIL, " FAILED!\n"));
            Fail(CS(MSG_VERIFY_FAILED_DEL, "SaferCopy: verify failed - file deleted"), dst);
            xDel(dst);
            ok = FALSE;
            goto done;
        }
        if (!G.quiet) OUT(CS(MSG_VERIFY_OK, " OK\n"));
    }

    /* --- DATES / CLONE --------------------------------------- */
    if (G.dates)
    {
        /*
         * Bug Copy 47.7 confirme : SetFileDate n est jamais appele
         * quand DATES est specifie sans CLONE.
         */
        if (!xDate(dst, &fib->fib_Date))
            Fail(CS(MSG_SETDATE_WARNING, "SaferCopy: warning - SetFileDate failed"), dst);
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
            ERR(CS(MSG_VERIFY_READ_ERR, "\nSaferCopy: verify - read error\n"));
            ok = FALSE; break;
        }
        if (rSrc != rDst)
        {
            ERR(CS(MSG_VERIFY_SIZE_DIFF, "\nSaferCopy: verify - size mismatch (%ld vs %ld)\n"),
                (long)rSrc, (long)rDst);
            ok = FALSE; break;
        }
        if (rSrc == 0) break;   /* EOF propre */

        if (memcmp(G.buf, G.vbuf, (size_t)rSrc) != 0)
        {
            ERR(CS(MSG_VERIFY_CONTENT, "\nSaferCopy: verify - content mismatch\n"));
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

    /* NDATE : comparer uniquement la taille (ignorer la date).
     * Utile quand le backup precedent a ete fait sans DATES/CLONE :
     * les fichiers destination ont une date differente de la source
     * mais le contenu est identique. */
    if (!G.ndate)
    {
        struct DateStamp *ds = &fibSrc->fib_Date;
        struct DateStamp *dd = &fibDst->fib_Date;
        if (ds->ds_Days   != dd->ds_Days   ||
            ds->ds_Minute != dd->ds_Minute ||
            ds->ds_Tick   != dd->ds_Tick)
            goto done;
    }

    needed = FALSE;   /* meme taille (et meme date si !NDATE) : rien a faire */

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

/*
 * JoinPathSafe :
 *   Construit un chemin destination de façon sûre.
 *   Si G.nameMax > 0 et que 'leaf' dépasse la limite :
 *     - Appelle FailNameLen(errorPath) si errorPath != NULL
 *     - Retourne FALSE
 *   Sinon construit le chemin et retourne le résultat de xAddPart.
 */
static BOOL JoinPathSafe(char *out, LONG outlen,
                         const char *dir, const char *leaf,
                         const char *errorPath)
{
    if (G.nameMax > 0)
    {
        LONG n = (LONG)strlen(leaf);
        if (n > G.nameMax)
        {
            if (errorPath)
                FailNameLen(errorPath, n);
            return FALSE;
        }
    }

    strncpy(out, dir, (size_t)(outlen - 1));
    out[outlen - 1] = '\0';
    return xAddPart(out, leaf, (ULONG)outlen);
}

/*
 * StrAppend : concatenation bornee (sc.lib et libnix C89 n ont pas
 * de snprintf). Tronque silencieusement si dst est plein.
 */
static void StrAppend(char *dst, ULONG dstlen, const char *src)
{
    ULONG used = (ULONG)strlen(dst);
    ULONG n;

    if (used + 1 >= dstlen) return;
    n = (ULONG)strlen(src);
    if (n > dstlen - 1 - used) n = dstlen - 1 - used;
    memcpy(dst + used, src, (size_t)n);
    dst[used + n] = '\0';
}

/*
 * Fail : construit le message d'erreur et l'ajoute au buffer.
 *
 * CRITIQUE : arg est un chemin pouvant faire MAXPATH octets et msg
 * une chaine de catalogue de longueur arbitraire. L ancien
 * sprintf("%s: %s") dans line[384] debordait sur la PILE des qu un
 * chemin en erreur depassait ~350 caracteres -> ecrasement silencieux
 * au point le plus profond de la recursion (NOSTKCHK, pas de MMU)
 * -> lockup differe. D ou : concatenation bornee, et buffers en
 * static (Fail n est jamais reentrant, le programme est mono-tache).
 */
static void Fail(const char *msg, const char *arg)
{
    static char line[MAXPATH + 192];
    static char fault[96];
    LONG err = IoErr();
    int  used, avail;

    line[0] = '\0';
    StrAppend(line, (ULONG)sizeof(line), msg);
    if (arg)
    {
        StrAppend(line, (ULONG)sizeof(line), ": ");
        StrAppend(line, (ULONG)sizeof(line), arg);
    }

    if (err && Fault(err, NULL, (STRPTR)fault, (LONG)sizeof(fault))) {
        StrAppend(line, (ULONG)sizeof(line), " (");
        StrAppend(line, (ULONG)sizeof(line), fault);
        StrAppend(line, (ULONG)sizeof(line), ")");
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
 * FailNameLen : signale un nom depassant la limite NAMELEN.
 * Le format vient du catalog ; buffer static, taille bornee
 * (le chemin complet est ajoute par Fail, lui-meme borne).
 */
static void FailNameLen(const char *path, LONG n)
{
    static char m[96];

    sprintf(m, CS(MSG_NAME_TOO_LONG, "SaferCopy: name too long (%ld > %ld)"),
            (long)n, (long)G.nameMax);
    Fail(m, path);
}

/*
 * PrintErrors : vide le buffer d'erreurs sur stderr.
 * Appele une seule fois, juste avant le rapport final.
 */
static void PrintErrors(void)
{
    if (g_errBufUsed == 0 && g_errTrunc == 0) return;

    ERR(CS(MSG_SUMMARY_HEADER, "\n--- Summary: %ld error(s) ---\n"), (long)G.nErrors);
    if (g_errBufUsed > 0)
        ERR("%s", g_errBuf);
    if (g_errTrunc > 0)
        ERR(CS(MSG_ERRBUF_TRUNCATED, "  [%ld message(s) truncated - error buffer full]\n"),
            (long)g_errTrunc);
}


static BOOL EnsurePath(const char *path)
{
    char buf[MAXPATH];
    LONG i, len;

    if (IsDir(path, NULL)) return TRUE;

    strncpy(buf, path, MAXPATH - 1);
    buf[MAXPATH - 1] = '\0';
    len = (LONG)strlen(buf);

    /* Retirer le slash final */
    if (len > 0 && buf[len - 1] == '/')
        buf[--len] = '\0';

    for (i = 1; i <= len; i++)
    {
        if (buf[i] == '/' || i == len)
        {
            char save   = buf[i];
            LONG endpos = (LONG)strlen(buf) - 1;
            buf[i] = '\0';

            /* Ne pas tenter de creer le volume lui-meme ("DH1:") */
            if (endpos >= 0 && buf[endpos] != ':')
            {
                if (!IsDir(buf, NULL))
                {
                    BPTR l = CreateDir((STRPTR)buf);
                    if (l) UnLock(l);
                    else if (i == len)
                    {
                        buf[i] = save;
                        return FALSE;   /* echec sur le composant final */
                    }
                    /* echec sur intermediaire = probablement deja existant,
                     * on continue et on verra au composant suivant */
                }
            }
            buf[i] = save;
        }
    }
    return IsDir(path, NULL);
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
        /* Ce message va dans le buffer, imprime avec les autres a la fin.
         * static + 192 octets : pas sur la pile (on peut etre au plus
         * profond de la recursion), marge pour les formats traduits. */
        {
            static char msg[192];
            sprintf(msg, CS(MSG_ABORT, "SaferCopy: MAXERR=%ld reached - aborting"), (long)G.maxErr);
            Fail(msg, NULL);
        }
    }
}

static void ShowProgress(ULONG done, ULONG total)
{
    int percent;

    if (total == 0) return;

    /*
     * Calcul du pourcentage en 32 bits, sans long long (support
     * douteux sous SAS/C 6). done * 100 deborde un ULONG des que
     * done > ~42 Mo : pour les gros fichiers on divise d abord.
     * Pour total < 100 (jamais atteint : PROGRESS gate a 2 Mo)
     * on garde done * 100 qui ne peut pas deborder.
     */
    if (total >= 100UL)
        percent = (int)(done / (total / 100UL));
    else
        percent = (int)((done * 100UL) / total);

    if (total >= (10UL * 1024UL * 1024UL)) {   /* >= 10 Mo */
        OUT("\r  %3d%%  (%lu.%lu / %lu.%lu MB)",
            percent,
            (unsigned long)(done / (1024*1024)),
            (unsigned long)((done % (1024*1024)) / (100*1024)),   /* 1 decimale */
            (unsigned long)(total / (1024*1024)),
            (unsigned long)((total % (1024*1024)) / (100*1024)));
    } else {
        OUT("\r  %3d%%  (%lu / %lu bytes)",
            percent,
            (unsigned long)done,
            (unsigned long)total);
    }

    fflush(stdout);
}

static void Die(LONG code)
{
    if (G.noreqSet)
    {
        struct Process *me = (struct Process *)FindTask(NULL);
        me->pr_WindowPtr = G.oldWinPtr;
    }
    if (G.buf)  FreeVec(G.buf);
    if (G.vbuf) FreeVec(G.vbuf);
    locale_close();
    fflush(stdout);
    fflush(stderr);
    exit(code);
}
/* $Id: main.c,v 1.69 2019/11/25 23:24:36 Tom.Shields Exp $ */

#include <err.h>
#include <fcntl.h> /* for open(), O_EXCL, etc. */
#include <signal.h>
#include <unistd.h> /* for _exit() */

#include "defs.h"

#include <sys/stat.h>
#include <sys/types.h>

typedef struct _my_tmpfiles {
	struct _my_tmpfiles *next;
	char *name;
} MY_TMPFILES;

static MY_TMPFILES *my_tmpfiles;

char dflag;
char dflag2;
char gflag;
char iflag;
char lflag;
static char oflag;
char rflag;
char sflag;
char tflag;
char vflag;

const char *symbol_prefix;
const char *myname = "yacc";

int lineno;
int outline;

static char default_file_prefix[] = "y";
static int explicit_file_name;

static char *file_prefix = default_file_prefix;

char *code_file_name;
char *input_file_name;
size_t input_file_name_len = 0;
char *defines_file_name;
char *externs_file_name;

static char *graph_file_name;
static char *output_file_name;
static char *verbose_file_name;

FILE *action_file;  /*  a temp file, used to save actions associated    */
                    /*  with rules until the parser is written          */
FILE *code_file;    /*  y.code.c (used when the -r option is specified) */
FILE *defines_file; /*  y.tab.h                                         */
FILE *externs_file; /*  y.tab.i                                         */
FILE *input_file;   /*  the input file                                  */
FILE *output_file;  /*  y.tab.c                                         */
FILE *text_file;    /*  a temp file, used to save text until all        */
                    /*  symbols have been defined                       */
FILE *union_file;   /*  a temp file, used to save the union             */
                    /*  definition until all symbol have been           */
                    /*  defined                                         */
FILE *verbose_file; /*  y.output                                        */
FILE *graph_file;   /*  y.dot                                           */

Value_t nitems;
Value_t nrules;
Value_t nsyms;
Value_t ntokens;
Value_t nvars;

Value_t start_symbol;
char **symbol_name;
char **symbol_pname;
Value_t *symbol_value;
Value_t *symbol_prec;
char *symbol_assoc;

int pure_parser;
int token_table;
int error_verbose;

Value_t *symbol_pval;
char **symbol_destructor;
char **symbol_type_tag;
int locations = 0; /* default to no position processing */
char *initial_action = NULL;
int backtrack = 0; /* default is no backtracking */

int exit_code;

Value_t *ritem;
Value_t *rlhs;
Value_t *rrhs;
Value_t *rprec;
Assoc_t *rassoc;
Value_t **derives;
char *nullable;

static int got_intr = 0;

void done(int k)
{
/*
 * Since fclose() is called via the signal handler, it might die.  Don't loop
 * if there is a problem closing a file.
 */
#define DO_CLOSE(fp)            \
	if (fp != NULL) {       \
		FILE *use = fp; \
		fp = NULL;      \
		fclose(use);    \
	}

	DO_CLOSE(input_file);
	DO_CLOSE(output_file);
	if (iflag)
		DO_CLOSE(externs_file);
	if (rflag)
		DO_CLOSE(code_file);

	DO_CLOSE(action_file);
	DO_CLOSE(defines_file);
	DO_CLOSE(graph_file);
	DO_CLOSE(text_file);
	DO_CLOSE(union_file);
	DO_CLOSE(verbose_file);

	if (got_intr)
		_exit(EXIT_FAILURE);

#ifdef NO_LEAKS
	if (rflag)
		free(code_file_name);

	if (dflag && !dflag2)
		free(defines_file_name);

	if (iflag)
		free(externs_file_name);

	if (oflag)
		free(output_file_name);

	if (vflag)
		free(verbose_file_name);

	if (gflag)
		free(graph_file_name);

	lr0_leaks();
	lalr_leaks();
	mkpar_leaks();
	mstring_leaks();
	output_leaks();
	reader_leaks();
#endif

	exit(k);
#undef DO_CLOSE
}

/*
 * Called by emalloc, easprintf, etc...
 */
__attribute__((__format__(printf, 2, 3)))
static void err_handler(int unused, const char *fmt, ...)
{
	(void)unused;
	va_list ap;
	va_start(ap, fmt);
	verr(2, fmt, ap);
	va_end(ap);
}

static void
onintr(int sig GCC_UNUSED)
{
	got_intr = 1;
	done(EXIT_FAILURE);
}

static void
set_signals(void)
{
#ifdef SIGINT
	if (signal(SIGINT, SIG_IGN) != SIG_IGN)
		signal(SIGINT, onintr);
#endif
#ifdef SIGTERM
	if (signal(SIGTERM, SIG_IGN) != SIG_IGN)
		signal(SIGTERM, onintr);
#endif
#ifdef SIGHUP
	if (signal(SIGHUP, SIG_IGN) != SIG_IGN)
		signal(SIGHUP, onintr);
#endif
}

static void
usage(void)
{
	static const char *msg[] = {
		"",
		"Options:",
		"    -b file_prefix        set filename prefix (default \"y.\")",
		"    -B                    create a backtracking parser",
		"    -d                    write definitions (" DEFINES_SUFFIX ")",
		"    -H defines_file       write definitions to defines_file",
		"    -i                    write interface (y.tab.i)",
		"    -g                    write a graphical description",
		"    -l                    suppress #line directives",
		"    -L                    enable position processing, e.g., \"%locations\"",
		"    -o output_file        (default \"" OUTPUT_SUFFIX "\")",
		"    -p symbol_prefix      set symbol prefix (default \"yy\")",
		"    -P                    create a reentrant parser, e.g., \"%pure-parser\"",
		"    -r                    produce separate code and table files (y.code.c)",
		"    -s                    suppress #define's for quoted names in %token lines",
		"    -t                    add debugging support",
		"    -v                    write description (y.output)",
		"    -V                    show version information and exit"
	};
	unsigned n;

	fflush(stdout);
	fprintf(stderr, "Usage: %s [options] filename\n", myname);
	for (n = 0; n < sizeof(msg) / sizeof(msg[0]); ++n)
		fprintf(stderr, "%s\n", msg[n]);

	exit(EXIT_FAILURE);
}

static void
setflag(int ch)
{
	switch (ch) {
	case 'B':
		backtrack = 1;
		break;
	case 'd':
		dflag = 1;
		dflag2 = 0;
		break;
	case 'g':
		gflag = 1;
		break;
	case 'i':
		iflag = 1;
		break;
	case 'l':
		lflag = 1;
		break;
	case 'L':
		locations = 1;
		break;
	case 'P':
		pure_parser = 1;
		break;
	case 'r':
		rflag = 1;
		break;
	case 's':
		sflag = 1;
		break;
	case 't':
		tflag = 1;
		break;
	case 'v':
		vflag = 1;
		break;
	case 'V':
		printf("%s - %s\n", myname, VERSION);
		exit(EXIT_SUCCESS);
	case 'y':
		/* noop for bison compatibility. byacc is already designed to be posix
		 * yacc compatible. */
		break;
	default:
		usage();
	}
}

static void
getargs(int argc, char *argv[])
{
	int i;
	int ch;

	if (argc > 0)
		myname = argv[0];

	while ((ch = getopt(argc, argv, "Bb:dgH:ilLo:Pp:rstVvy")) != -1) {
		switch (ch) {
		case 'b':
			file_prefix = optarg;
			break;
		case 'H':
			dflag = dflag2 = 1;
			defines_file_name = optarg;
			break;
		case 'o':
			output_file_name = optarg;
			explicit_file_name = 1;
			break;
		case 'p':
			symbol_prefix = optarg;
			break;
		default:
			setflag(ch);
			break;
		}
	}
	if ((i = optind) < argc) {
		/* getopt handles "--" specially, while we handle "-" specially */
		if (!strcmp(argv[i], "-")) {
			if ((i + 1) < argc)
				usage();
			input_file = stdin;
			return;
		}
	}

	if (i + 1 != argc)
		usage();
	input_file_name_len = strlen(argv[i]);
	input_file_name = estrdup(argv[i]);
}

#define CREATE_FILE_NAME(dest, suffix) \
	dest = alloc_file_name(len, suffix)

static char *
alloc_file_name(size_t len, const char *suffix)
{
	len += strlen(suffix) + 1;
	char *result = emalloc(len);
	strlcpy(result, file_prefix, len);
	strlcat(result, suffix, len);
	return result;
}

static char *
find_suffix(char *name, const char *suffix)
{
	size_t len = strlen(name);
	size_t slen = strlen(suffix);
	if (len >= slen) {
		name += len - slen;
		if (strcmp(name, suffix) == 0)
			return name;
	}
	return NULL;
}

static void
create_file_names(void)
{
	size_t len;
	const char *defines_suffix;
	const char *externs_suffix;
	char *suffix;

	suffix = NULL;
	defines_suffix = DEFINES_SUFFIX;
	externs_suffix = EXTERNS_SUFFIX;

	/* compute the file_prefix from the user provided output_file_name */
	if (output_file_name != 0) {
		if (!(suffix = find_suffix(output_file_name, OUTPUT_SUFFIX))
		    && (suffix = find_suffix(output_file_name, ".c"))) {
			defines_suffix = ".h";
			externs_suffix = ".i";
		}
	}

	if (suffix != NULL) {
		len = (size_t)(suffix - output_file_name);
		file_prefix = emalloc(len + 1);
		strncpy(file_prefix, output_file_name, len)[len] = 0;
	} else
		len = strlen(file_prefix);

	/* if "-o filename" was not given */
	if (output_file_name == 0) {
		oflag = 1;
		CREATE_FILE_NAME(output_file_name, OUTPUT_SUFFIX);
	}

	if (rflag) {
		CREATE_FILE_NAME(code_file_name, CODE_SUFFIX);
	} else
		code_file_name = output_file_name;

	if (dflag && !dflag2) {
		if (explicit_file_name) {
			char *xsuffix;
			defines_file_name = estrdup(output_file_name);
			/* does the output_file_name have a known suffix */
			xsuffix = strrchr(output_file_name, '.');
			if (xsuffix != 0 &&
			    (!strcmp(xsuffix, ".c") ||   /* good, old-fashioned C */
			     !strcmp(xsuffix, ".C") ||   /* C++, or C on Windows */
			     !strcmp(xsuffix, ".cc") ||  /* C++ */
			     !strcmp(xsuffix, ".cxx") || /* C++ */
			     !strcmp(xsuffix, ".cpp")))  /* C++ (Windows) */
			{
				strncpy(defines_file_name, output_file_name,
				        xsuffix - output_file_name + 1);
				defines_file_name[xsuffix - output_file_name + 1] = 'h';
				defines_file_name[xsuffix - output_file_name + 2] = 0;
			} else {
				fprintf(stderr, "%s: suffix of output file name %s"
				                " not recognized, no -d file generated.\n",
				        myname, output_file_name);
				dflag = 0;
				free(defines_file_name);
				defines_file_name = 0;
			}
		} else {
			CREATE_FILE_NAME(defines_file_name, defines_suffix);
		}
	}

	if (iflag) {
		CREATE_FILE_NAME(externs_file_name, externs_suffix);
	}

	if (vflag) {
		CREATE_FILE_NAME(verbose_file_name, VERBOSE_SUFFIX);
	}

	if (gflag) {
		CREATE_FILE_NAME(graph_file_name, GRAPH_SUFFIX);
	}

	if (suffix != NULL) {
		free(file_prefix);
	}
}

static void
close_tmpfiles(void)
{
	while (my_tmpfiles != 0) {
		MY_TMPFILES *next = my_tmpfiles->next;

		(void)chmod(my_tmpfiles->name, 0644);
		(void)unlink(my_tmpfiles->name);

		free(my_tmpfiles->name);
		free(my_tmpfiles);

		my_tmpfiles = next;
	}
}

/*
 * tmpfile() should be adequate, except that it may require special privileges
 * to use, e.g., MinGW and Windows 7 where it tries to use the root directory.
 */
static FILE *
open_tmpfile(const char *label)
{
#define MY_FMT "%s/%.*sXXXXXX"

	FILE *result = NULL;
	int fd;
	const char *tmpdir;
	char *name;
	const char *mark;

	if (((tmpdir = getenv("TMPDIR")) == 0 ||
	    access(tmpdir, W_OK) != 0) ||
	    ((tmpdir = getenv("TEMP")) == 0 ||
	     access(tmpdir, W_OK) != 0)) {
		tmpdir = "/tmp";
		if (access(tmpdir, W_OK) != 0)
			tmpdir = ".";
	}

	/* The size of the format is guaranteed to be longer than the result from
	 * printing empty strings with it; this calculation accounts for the
	 * string-lengths as well.
	 */
	name = emalloc(strlen(tmpdir) + sizeof(MY_FMT) + strlen(label));
	mode_t save_umask = umask(0177);

	if ((mark = strrchr(label, '_')) == 0)
		mark = label + strlen(label);

	sprintf(name, MY_FMT, tmpdir, (int)(mark - label), label);
	fd = mkstemp(name);
	if (fd >= 0
	    && (result = fdopen(fd, "w+")) != 0) {
		MY_TMPFILES *item;

		if (my_tmpfiles == 0) {
			atexit(close_tmpfiles);
		}

		item = emalloc(sizeof(*item));
		item->name = name;
		item->next = my_tmpfiles;
		my_tmpfiles = item;
	} else {
		free(name);
	}
	(void)umask(save_umask);

	if (result == 0)
		open_error(label);
	return result;
#undef MY_FMT
}

static void
open_files(void)
{
	create_file_names();

	if (input_file == 0) {
		input_file = fopen(input_file_name, "r");
		if (input_file == 0)
			open_error(input_file_name);
	}

	action_file = open_tmpfile("action_file");
	text_file = open_tmpfile("text_file");

	if (vflag) {
		verbose_file = fopen(verbose_file_name, "w");
		if (verbose_file == 0)
			open_error(verbose_file_name);
	}

	if (gflag) {
		graph_file = fopen(graph_file_name, "w");
		if (graph_file == 0)
			open_error(graph_file_name);
		fprintf(graph_file, "digraph %s {\n", file_prefix);
		fprintf(graph_file, "\tedge [fontsize=10];\n");
		fprintf(graph_file, "\tnode [shape=box,fontsize=10];\n");
		fprintf(graph_file, "\torientation=landscape;\n");
		fprintf(graph_file, "\trankdir=LR;\n");
		fprintf(graph_file, "\t/*\n");
		fprintf(graph_file, "\tmargin=0.2;\n");
		fprintf(graph_file, "\tpage=\"8.27,11.69\"; // for A4 printing\n");
		fprintf(graph_file, "\tratio=auto;\n");
		fprintf(graph_file, "\t*/\n");
	}

	if (dflag || dflag2) {
		defines_file = fopen(defines_file_name, "w");
		if (defines_file == 0)
			open_error(defines_file_name);
		union_file = open_tmpfile("union_file");
	}

	if (iflag) {
		externs_file = fopen(externs_file_name, "w");
		if (externs_file == 0)
			open_error(externs_file_name);
	}

	output_file = fopen(output_file_name, "w");
	if (output_file == 0)
		open_error(output_file_name);

	if (rflag) {
		code_file = fopen(code_file_name, "w");
		if (code_file == 0)
			open_error(code_file_name);
	} else
		code_file = output_file;
}

int main(int argc, char *argv[])
{
	SRexpect = -1;
	RRexpect = -1;
	exit_code = EXIT_SUCCESS;

	esetfunc(err_handler);

	set_signals();
	getargs(argc, argv);
	open_files();
	reader();
	lr0();
	lalr();
	make_parser();
	graph();
	finalize_closure();
	verbose();
	output();
	done(exit_code);
}

#ifndef MF_ERR_H
#define MF_ERR_H

/* Every fallible call returns mf_err. Results travel through `out` parameters.
   There is no errno-style global, and no error object allocation — callers that
   want detail pass an errbuf. */
typedef enum {
    MF_OK = 0,
    MF_ERR_ARGS,            /* malformed command line */
    MF_ERR_IO,              /* open/read/write failed */
    MF_ERR_PARSE,           /* malformed JSON */
    MF_ERR_UNKNOWN_KEY,     /* config key not recognised — never ignored */
    MF_ERR_TYPE,            /* config key present with the wrong JSON type */
    MF_ERR_RANGE            /* config value outside its permitted range */
} mf_err;

/* Every code above has a producer. There is deliberately no MF_ERR_INTERNAL:
   the conditions it used to carry — out of memory, a broken invariant — kill
   the process now instead of travelling as a value nobody can act on
   (see mf/panic.h). A code with no producer is a claim the code does not make. */

const char *mf_err_str(mf_err e);

#endif

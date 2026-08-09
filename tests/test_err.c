#include "harness.h"
#include "mf/err.h"

MF_TEST(err_every_code_has_a_message) {
    const mf_err all[] = {MF_OK,        MF_ERR_ARGS,  MF_ERR_IO,
                          MF_ERR_PARSE, MF_ERR_UNKNOWN_KEY, MF_ERR_TYPE,
                          MF_ERR_RANGE};
    for (size_t i = 0; i < sizeof all / sizeof *all; i++) {
        const char *s = mf_err_str(all[i]);
        MF_CHECK(s != NULL);
        MF_CHECK(s[0] != '\0');
    }
}

MF_TEST(err_unknown_code_is_not_null) {
    MF_EQ_STR(mf_err_str((mf_err)999), "unknown error");
}

void run_err_tests(void) {
    MF_RUN(err_every_code_has_a_message);
    MF_RUN(err_unknown_code_is_not_null);
}

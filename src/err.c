#include "mf/err.h"

const char *mf_err_str(mf_err e) {
    switch (e) {
        case MF_OK:                 return "ok";
        case MF_ERR_ARGS:           return "bad arguments";
        case MF_ERR_IO:             return "i/o error";
        case MF_ERR_PARSE:          return "parse error";
        case MF_ERR_UNKNOWN_KEY:    return "unknown config key";
        case MF_ERR_TYPE:           return "wrong type";
        case MF_ERR_RANGE:          return "value out of range";
        case MF_ERR_NOT_IMPLEMENTED:return "not implemented";
        case MF_ERR_INTERNAL:       return "internal error";
    }
    return "unknown error";
}

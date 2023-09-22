#include "putty.h"

#define CONF_OPTION(id, ...) { __VA_ARGS__ },
#define VALUE_TYPE(x) .value_type = CONF_TYPE_ ## x
#define SUBKEY_TYPE(x) .subkey_type = CONF_TYPE_ ## x

const ConfKeyInfo conf_key_info[] = {
    #include "conf.h"
};

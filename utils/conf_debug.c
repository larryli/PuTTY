#include "putty.h"

#define CONF_OPTION(id, ...) "CONF_" #id,

static const char *const conf_debug_identifiers[] = {
    #include "conf.h"
};

const char *conf_id(int key)
{
    size_t i = key;
    if (i < lenof(conf_debug_identifiers))
        return conf_debug_identifiers[i];
    return "CONF_!outofrange!";
}

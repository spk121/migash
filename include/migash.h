/* migash.h - the public API for the Miga Shell Library */
#ifndef MIGASH_H
#define MIGASH_H

/* Must come first */
#include "migash/config.h"

/* Primitive types */
#include "migash/string_t.h"
#include "migash/strlist.h"

/* Getopt for shell builtins */
#include "migash/getopt.h"
#include "migash/getopt_string.h"

/* Shell execution API */
#include "migash/exec_types_public.h"
#include "migash/frame.h"
#include "migash/exec.h"

#endif /* MIGASH_H */

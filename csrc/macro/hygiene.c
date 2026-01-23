/*
 * OmniLisp Macro Hygiene System
 *
 * Mark-based hygiene to prevent accidental variable capture.
 * Symbols introduced by macros are renamed to avoid conflicts
 * with user code.
 */

#include "macro.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ============== Reserved/Special Symbols ============== */

/*
 * Symbols that should not be renamed for hygiene.
 * These are core language forms that must maintain their identity.
 */
static const char* RESERVED_SYMBOLS[] = {
    /* Core special forms */
    "define", "lambda", "fn", "if", "quote", "set!", "begin", "do",
    "let", "let*", "letrec", "cond", "case", "and", "or", "not",
    "match", "when", "unless",

    /* Type annotations */
    ":", "->", "=>",

    /* Boolean and nil */
    "true", "false", "nil", "nothing",

    /* Module system */
    "import", "export", "module", "require",

    /* Macros */
    "syntax", "syntax-rules", "define-syntax",

    /* Common primitives that should be stable */
    "+", "-", "*", "/", "=", "<", ">", "<=", ">=",
    "cons", "car", "cdr", "list", "append",
    "print", "println", "error",

    NULL
};

// REVIEWED:NAIVE
/*
 * Check if a symbol is reserved and should not be renamed.
 */
static bool is_reserved_symbol(const char* name) {
    for (int i = 0; RESERVED_SYMBOLS[i] != NULL; i++) {
        if (strcmp(name, RESERVED_SYMBOLS[i]) == 0) {
            return true;
        }
    }
    return false;
}

/* ============== Hygiene Context ============== */

HygieneContext* omni_macro_hygiene_new(void) {
    HygieneContext* ctx = malloc(sizeof(HygieneContext));
    if (!ctx) return NULL;

    ctx->current_mark = 0;
    ctx->renames = NULL;
    return ctx;
}

void omni_macro_hygiene_free(HygieneContext* ctx) {
    if (!ctx) return;

    RenameEntry* entry = ctx->renames;
    while (entry) {
        RenameEntry* next = entry->next;
        free(entry->original);
        free(entry->renamed);
        free(entry);
        entry = next;
    }

    free(ctx);
}

/* ============== Mark Management ============== */

int omni_macro_next_mark(MacroExpander* exp) {
    if (!exp || !exp->hygiene) return 0;
    return ++exp->hygiene->current_mark;
}

/* ============== Rename Table ============== */

// REVIEWED:NAIVE
RenameEntry* omni_macro_lookup_rename(HygieneContext* ctx,
                                       const char* original,
                                       int mark) {
    if (!ctx || !original) return NULL;

    for (RenameEntry* entry = ctx->renames; entry; entry = entry->next) {
        if (entry->mark == mark && strcmp(entry->original, original) == 0) {
            return entry;
        }
    }
    return NULL;
}

void omni_macro_record_rename(HygieneContext* ctx,
                               const char* original,
                               const char* renamed,
                               int mark) {
    if (!ctx || !original || !renamed) return;

    RenameEntry* entry = malloc(sizeof(RenameEntry));
    entry->original = strdup(original);
    entry->renamed = strdup(renamed);
    entry->mark = mark;
    entry->next = ctx->renames;
    ctx->renames = entry;
}

/* ============== Gensym ============== */

OmniValue* omni_macro_gensym(MacroExpander* exp, const char* prefix) {
    if (!exp) return omni_nil;

    char buf[128];
    const char* p = prefix ? prefix : "g";
    snprintf(buf, sizeof(buf), "_%s_%d", p, exp->gensym_counter++);

    return omni_new_sym(buf);
}

/* ============== Hygiene Marking ============== */

/*
 * Apply a hygiene mark to a symbol.
 *
 * For symbols introduced by a macro (not pattern variables), we need to
 * rename them to avoid capturing user variables with the same name.
 *
 * The strategy:
 * 1. Reserved symbols (core forms, primitives) are never renamed
 * 2. For other symbols, check if already renamed at this mark
 * 3. If not, create a new renamed version
 */
OmniValue* omni_macro_apply_mark(MacroExpander* exp, OmniValue* sym, int mark) {
    if (!exp || !sym || !omni_is_sym(sym)) return sym;

    const char* name = sym->str_val;

    /* Never rename reserved symbols */
    if (is_reserved_symbol(name)) {
        return sym;
    }

    /* Never rename symbols starting with underscore (already hygienic) */
    if (name[0] == '_') {
        return sym;
    }

    /* Check for existing rename at this mark */
    RenameEntry* entry = omni_macro_lookup_rename(exp->hygiene, name, mark);
    if (entry) {
        return omni_new_sym(entry->renamed);
    }

    /* Create new renamed symbol */
    char buf[256];
    snprintf(buf, sizeof(buf), "%s_m%d", name, mark);

    /* Record the rename */
    omni_macro_record_rename(exp->hygiene, name, buf, mark);

    return omni_new_sym(buf);
}

/*
 * Resolve a potentially marked symbol to its original or renamed form.
 * This is used during code generation to properly resolve identifiers.
 */
const char* omni_macro_resolve_symbol(MacroExpander* exp, const char* name) {
    if (!exp || !name) return name;

    /* Check if this is a renamed symbol (contains _m followed by digits) */
    const char* marker = strstr(name, "_m");
    if (marker) {
        /* Verify it's followed by digits */
        const char* p = marker + 2;
        bool all_digits = true;
        while (*p) {
            if (*p < '0' || *p > '9') {
                all_digits = false;
                break;
            }
            p++;
        }

        if (all_digits && p > marker + 2) {
            /* This is a renamed symbol - extract original name */
            size_t orig_len = marker - name;
            char* original = malloc(orig_len + 1);
            memcpy(original, name, orig_len);
            original[orig_len] = '\0';

            /* Note: caller must free this */
            return original;
        }
    }

    return name;
}

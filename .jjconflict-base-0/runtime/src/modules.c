/*
 * modules.c - Module and import system for OmniLisp
 *
 * Implements module system for code organization and namespace management.
 *
 * API:
 *   - module: Create a new module
 *   - import: Import symbols from a module
 *   - export: Export symbols from a module
 *   - use: Use module (short form of import)
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "../include/omni.h"
#include "internal_types.h"

/* ============== Module Structure ============== */

/*
 * Module: A namespace for symbols
 *
 * A module contains:
 * - Name: Module identifier
 * - Exports: List of exported symbols
 * - Imports: List of imported modules
 * - Private: Private symbols (not exported)
 */

typedef struct ModuleExport {
    char* name;              /* Symbol name */
    Obj* value;              /* Symbol value */
    int is_public;           /* Export flag */
    struct ModuleExport* next;
} ModuleExport;

typedef struct ModuleImport {
    char* module_name;       /* Imported module name */
    char* prefix;            /* Optional prefix for namespacing */
    char** imported_symbols;  /* NULL for all, or list of specific symbols */
    int symbol_count;
    struct ModuleImport* next;
} ModuleImport;

typedef struct Module {
    char* name;              /* Module name */
    ModuleExport* exports;    /* Exported symbols */
    ModuleImport* imports;    /* Imported modules */
    struct Module* next;     /* Next in global registry */
} Module;

/* Global module registry */
static Module* g_module_registry = NULL;

/* Current module being defined */
static Module* g_current_module = NULL;

/* ============== Module Registry Functions ============== */

/*
 * Find or create a module by name
 */
static Module* get_or_create_module(const char* name) {
    if (!name) return NULL;

    /* Search for existing module */
    for (Module* m = g_module_registry; m; m = m->next) {
        if (strcmp(m->name, name) == 0) {
            return m;
        }
    }

    /* Create new module */
    Module* m = malloc(sizeof(Module));
    memset(m, 0, sizeof(Module));
    m->name = strdup(name);
    m->next = g_module_registry;
    g_module_registry = m;

    return m;
}

/*
 * Find a module by name
 */
static Module* find_module(const char* name) {
    for (Module* m = g_module_registry; m; m = m->next) {
        if (strcmp(m->name, name) == 0) {
            return m;
        }
    }
    return NULL;
}

/* ============== Module API ============== */

/*
 * prim_module_begin: Begin defining a new module
 *
 * Args: module_name (string or symbol)
 * Returns: The module object
 *
 * Example:
 *   (module-begin "MyModule")
 *   (define x 10)
 *   (export x)
 *   (module-end)
 */
Obj* prim_module_begin(Obj* name_obj) {
    const char* name = NULL;

    if (IS_BOXED(name_obj) && name_obj->tag == TAG_SYM) {
        name = (const char*)name_obj->ptr;
    } else if (IS_BOXED(name_obj) && name_obj->tag == TAG_STRING) {
        name = (const char*)name_obj->ptr;
    }

    if (!name) {
        fprintf(stderr, "module-begin: Invalid module name\n");
        return NULL;
    }

    /* Set current module */
    g_current_module = get_or_create_module(name);

    return mk_sym(g_current_module->name);
}

/*
 * prim_module_end: End current module definition
 *
 * Returns: The module object
 */
Obj* prim_module_end(void) {
    if (!g_current_module) {
        fprintf(stderr, "module-end: No active module\n");
        return NULL;
    }

    Module* m = g_current_module;
    g_current_module = NULL;

    return mk_sym(m->name);
}

/*
 * prim_export: Export a symbol from current module
 *
 * Args: symbol_name, value
 * Returns: The value
 *
 * Example:
 *   (export 'some-symbol value)
 */
Obj* prim_export(Obj* symbol_name, Obj* value) {
    if (!g_current_module) {
        fprintf(stderr, "export: No active module\n");
        return value;
    }

    const char* name = NULL;
    if (IS_BOXED(symbol_name) && symbol_name->tag == TAG_SYM) {
        name = (const char*)symbol_name->ptr;
    } else if (IS_BOXED(symbol_name) && symbol_name->tag == TAG_STRING) {
        name = (const char*)symbol_name->ptr;
    }

    if (!name) {
        fprintf(stderr, "export: Invalid symbol name\n");
        return value;
    }

    /* Add to exports */
    ModuleExport* export = malloc(sizeof(ModuleExport));
    export->name = strdup(name);
    export->value = value;
    export->is_public = 1;
    export->next = g_current_module->exports;
    g_current_module->exports = export;

    return value;
}

/*
 * prim_module_get: Get a module by name
 *
 * Args: module_name
 * Returns: The module object or NULL
 */
Obj* prim_module_get(Obj* name_obj) {
    const char* name = NULL;

    if (IS_BOXED(name_obj) && name_obj->tag == TAG_SYM) {
        name = (const char*)name_obj->ptr;
    } else if (IS_BOXED(name_obj) && name_obj->tag == TAG_STRING) {
        name = (const char*)name_obj->ptr;
    }

    Module* m = find_module(name);

    if (!m) {
        return NULL;
    }

    /* Return module as symbol for now */
    return mk_sym(m->name);
}

/* ============== Import API ============== */

/*
 * prim_import: Import symbols from a module
 *
 * Args: module_name, options (keyword args)
 * Options:
 *   - :prefix: Add prefix to imported symbols
 *   - :only: Import only specified symbols
 *   - :except: Import all except specified symbols
 *   - :rename: Rename imported symbols
 *
 * Returns: Boolean indicating success
 *
 * Example:
 *   (import "MyModule" :only [foo bar])
 *   (import "OtherModule" :prefix "other")
 */
Obj* prim_import(Obj* name_obj, Obj* options) {
    const char* name = NULL;

    if (IS_BOXED(name_obj) && name_obj->tag == TAG_SYM) {
        name = (const char*)name_obj->ptr;
    } else if (IS_BOXED(name_obj) && name_obj->tag == TAG_STRING) {
        name = (const char*)name_obj->ptr;
    }

    Module* m = find_module(name);
    if (!m) {
        fprintf(stderr, "import: Module '%s' not found\n", name ? name : "?");
        return mk_bool(0);
    }

    /* Parse options */
    char* prefix = NULL;
    Obj* only_list = NULL;
    Obj* except_list = NULL;

    if (options && IS_BOXED(options) && options->tag == TAG_PAIR) {
        Obj* opt_list = options;
        while (opt_list && IS_BOXED(opt_list) && opt_list->tag == TAG_PAIR) {
            Obj* opt = opt_list->a;
            Obj* rest = opt_list->b;

            if (IS_BOXED(opt) && opt->tag == TAG_PAIR) {
                Obj* key = opt->a;
                Obj* value = opt->b;

                if (IS_BOXED(key) && key->tag == TAG_SYM) {
                    const char* key_str = (const char*)key->ptr;

                    if (strcmp(key_str, "prefix") == 0) {
                        if (IS_BOXED(value) && value->tag == TAG_SYM) {
                            prefix = (char*)value->ptr;
                        }
                    } else if (strcmp(key_str, "only") == 0) {
                        only_list = value;
                    } else if (strcmp(key_str, "except") == 0) {
                        except_list = value;
                    }
                }
            }

            opt_list = rest;
        }
    }

    /* Add import to current module */
    if (g_current_module) {
        ModuleImport* import = malloc(sizeof(ModuleImport));
        import->module_name = strdup(name);
        import->prefix = prefix ? strdup(prefix) : NULL;
        import->imported_symbols = NULL;
        import->symbol_count = 0;
        import->next = g_current_module->imports;
        g_current_module->imports = import;

        return mk_bool(1);
    }

    return mk_bool(0);
}

/*
 * prim_use: Use module (convenience form of import)
 *
 * Args: module_name
 * Returns: Boolean indicating success
 *
 * Example:
 *   (use "MyModule")
 */
Obj* prim_use(Obj* name_obj) {
    return prim_import(name_obj, NULL);
}

/*
 * prim_require: Require a module (load and import if not loaded)
 *
 * Args: module_name
 * Returns: The module object
 *
 * Note: This would typically load the module from a file
 * if it's not already loaded. For now, it just looks up
 * existing modules.
 */
Obj* prim_require(Obj* name_obj) {
    Obj* module = prim_module_get(name_obj);

    if (!module) {
        /* Module not found - would need to load from file */
        const char* name = NULL;
        if (IS_BOXED(name_obj) && name_obj->tag == TAG_SYM) {
            name = (const char*)name_obj->ptr;
        } else if (IS_BOXED(name_obj) && name_obj->tag == TAG_STRING) {
            name = (const char*)name_obj->ptr;
        }

        fprintf(stderr, "require: Module '%s' not found and auto-loading not implemented\n",
                name ? name : "?");
    }

    return module;
}

/*
 * prim_module_exports: Get list of exported symbols from module
 *
 * Args: module_name
 * Returns: List of (symbol-name . value) pairs
 */
Obj* prim_module_exports(Obj* name_obj) {
    const char* name = NULL;

    if (IS_BOXED(name_obj) && name_obj->tag == TAG_SYM) {
        name = (const char*)name_obj->ptr;
    } else if (IS_BOXED(name_obj) && name_obj->tag == TAG_STRING) {
        name = (const char*)name_obj->ptr;
    }

    Module* m = find_module(name);
    if (!m) {
        return NULL;  /* Module not found */
    }

    /* Build list of exports */
    Obj* result_list = NULL;
    ModuleExport* exp = m->exports;

    while (exp) {
        Obj* pair = mk_pair(mk_sym(exp->name), exp->value);
        Obj* new_list = mk_pair(pair, result_list);
        result_list = new_list;
        exp = exp->next;
    }

    return result_list;
}

/*
 * prim_module_list: List all registered modules
 *
 * Returns: List of module names
 */
Obj* prim_module_list(void) {
    Obj* result_list = NULL;

    for (Module* m = g_module_registry; m; m = m->next) {
        Obj* new_list = mk_pair(mk_sym(m->name), result_list);
        result_list = new_list;
    }

    return result_list;
}

/* ============== Qualified Name Resolution ============== */

/*
 * prim_resolve: Resolve a qualified name (module.symbol)
 *
 * Args: qualified_name (string like "Module.symbol")
 * Returns: The resolved value or NULL
 *
 * Example:
 *   (resolve "MyModule.foo")
 */
Obj* prim_resolve(Obj* qualified_name) {
    const char* name = NULL;

    if (IS_BOXED(qualified_name) && qualified_name->tag == TAG_STRING) {
        name = (const char*)qualified_name->ptr;
    } else if (IS_BOXED(qualified_name) && qualified_name->tag == TAG_SYM) {
        name = (const char*)qualified_name->ptr;
    }

    if (!name) return NULL;

    /* Find the dot separator */
    const char* dot = strchr(name, '.');
    if (!dot) return NULL;

    /* Split into module and symbol */
    size_t module_name_len = dot - name;
    char* module_name = malloc(module_name_len + 1);
    strncpy(module_name, name, module_name_len);
    module_name[module_name_len] = '\0';

    const char* symbol_name = dot + 1;

    /* Find the module */
    Module* m = find_module(module_name);

    free(module_name);

    if (!m) return NULL;

    /* Find the export */
    for (ModuleExport* exp = m->exports; exp; exp = exp->next) {
        if (strcmp(exp->name, symbol_name) == 0) {
            return exp->value;
        }
    }

    return NULL;
}

/* ============== Cleanup ============== */

/*
 * Free all modules (for shutdown)
 */
void free_all_modules(void) {
    Module* m = g_module_registry;

    while (m) {
        Module* next = m->next;

        /* Free exports */
        ModuleExport* exp = m->exports;
        while (exp) {
            ModuleExport* next_exp = exp->next;
            free(exp->name);
            free(exp);
            exp = next_exp;
        }

        /* Free imports */
        ModuleImport* imp = m->imports;
        while (imp) {
            ModuleImport* next_imp = imp->next;
            free(imp->module_name);
            if (imp->prefix) free(imp->prefix);
            free(imp);
            imp = next_imp;
        }

        /* Free module name and structure */
        free(m->name);
        free(m);

        m = next;
    }

    g_module_registry = NULL;
    g_current_module = NULL;
}

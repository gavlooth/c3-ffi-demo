/*
 * OmniLisp Analysis Module
 *
 * Static analysis passes for ASAP memory management:
 * - Liveness analysis: determine when variables are last used
 * - Escape analysis: determine if values escape their scope
 * - Ownership analysis: track value ownership for safe deallocation
 * - Shape analysis: determine if data structures are Tree/DAG/Cyclic
 * - Reuse analysis: identify opportunities for in-place mutation
 */

#ifndef OMNILISP_ANALYSIS_H
#define OMNILISP_ANALYSIS_H

#include "../ast/ast.h"
#include "type_id.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============== Variable Usage ============== */

typedef enum {
    VAR_USAGE_NONE = 0,
    VAR_USAGE_READ = 1,
    VAR_USAGE_WRITE = 2,
    VAR_USAGE_CAPTURED = 4,    /* Captured by closure */
    VAR_USAGE_ESCAPED = 8,     /* Escapes current scope */
    VAR_USAGE_RETURNED = 16,   /* Returned from function */
    VAR_USAGE_REGION_RETAINED = 32,   /* Region retained via retain/release (Issue 1 P2) */
} VarUsageFlags;

typedef struct VarUsage {
    char* name;
    int flags;
    int first_use;       /* Position of first use */
    int last_use;        /* Position of last use */
    int def_pos;         /* Position where defined */
    bool is_param;       /* Is this a function parameter */

    /* OPTIMIZATION (T-opt-region-metadata-compiler): Type ID for compile-time type resolution */
    int type_id;         /* TypeID enum value (e.g., TYPE_ID_INT, TYPE_ID_PAIR) */

    /* Issue 3 P2: Non-lexical region end - Track which region owns this variable */
    int region_id;        /* Region ID (e.g., 0 for _local_region, 1 for nested regions) */

    struct VarUsage* next;
} VarUsage;

/* ============== Escape Classification ============== */

typedef enum {
    ESCAPE_NONE = 0,     /* Value stays local, can stack-allocate */
    ESCAPE_ARG,          /* Escapes via function argument */
    ESCAPE_RETURN,       /* Escapes via return value */
    ESCAPE_CLOSURE,      /* Escapes via closure capture */
    ESCAPE_GLOBAL,       /* Escapes to global/module scope */
} EscapeClass;

/* ============== Escape Repair Strategy (Issue 1 P2) ============== */

typedef enum {
    ESCAPE_REPAIR_TRANSMIGRATE = 0,  /* Copy value graph to destination region */
    ESCAPE_REPAIR_RETAIN_REGION = 1,  /* Keep source region alive via RC */
} EscapeRepairStrategy;

typedef struct EscapeInfo {
    char* name;
    EscapeClass escape_class;
    bool is_unique;      /* Is this the only reference */
    struct EscapeInfo* next;
} EscapeInfo;

/* ============== Shape Analysis (forward for OwnerInfo) ============== */

typedef enum {
    SHAPE_UNKNOWN = 0,
    SHAPE_SCALAR,        /* Simple value (int, char, etc.) */
    SHAPE_TREE,          /* Tree structure (no cycles) */
    SHAPE_DAG,           /* DAG (no cycles, shared refs) */
    SHAPE_CYCLIC,        /* Potentially cyclic */
} ShapeClass;

/* ============== Ownership ============== */

typedef enum {
    OWNER_LOCAL = 0,     /* Owned locally, must be freed */
    OWNER_BORROWED,      /* Borrowed reference, don't free */
    OWNER_TRANSFERRED,   /* Ownership transferred to callee */
    OWNER_SHARED,        /* Shared ownership (refcounted) */
} OwnershipKind;

/* Free strategy - determined by ownership + shape analysis */
typedef enum {
    FREE_STRATEGY_NONE = 0,      /* Don't free (borrowed/transferred) */
    FREE_STRATEGY_UNIQUE,        /* free_unique: known single ref, no RC check */
    FREE_STRATEGY_TREE,          /* free_tree: tree-shaped, recursive free */
    FREE_STRATEGY_RC,            /* dec_ref: shared/DAG, RC decrement */
    FREE_STRATEGY_RC_TREE,       /* dec_ref with recursive free on 0 */
    FREE_STRATEGY_SCC_STATIC,    /* static collection of known SCC */
    FREE_STRATEGY_COMPONENT_RELEASE, /* release_handle for component */
} FreeStrategy;

/* Allocation strategy - determined by escape analysis */
typedef enum {
    ALLOC_HEAP = 0,              /* malloc - value may escape */
    ALLOC_STACK,                 /* alloca or local struct - value stays local */
    ALLOC_POOL,                  /* Pool allocation - short-lived, group free */
    ALLOC_ARENA,                 /* Arena allocation - bulk free at scope end */
} AllocStrategy;

typedef struct OwnerInfo {
    char* name;
    OwnershipKind ownership;
    bool must_free;      /* Must free when scope ends */
    int free_pos;        /* Position where free should occur */
    bool is_unique;      /* Known to be the only reference */
    bool is_static_scc;  /* True if SCC can be statically collected */
    ShapeClass shape;    /* Shape of the data structure */
    AllocStrategy alloc_strategy;  /* Where to allocate */
    struct OwnerInfo* next;
} OwnerInfo;

/* ============== Shape Analysis (continued) ============== */

typedef struct ShapeInfo {
    char* type_name;
    ShapeClass shape;
    char** back_edge_fields;  /* Fields that form back-edges */
    size_t back_edge_count;
    struct ShapeInfo* next;
} ShapeInfo;

/* ============== Type Registry ============== */

/* Field ownership strength */
typedef enum {
    FIELD_STRONG = 0,        /* Owning reference (increment RC) */
    FIELD_WEAK,              /* Non-owning reference (no RC) */
    FIELD_BORROWED,          /* Borrowed reference (caller owns) */
} FieldStrength;

/* Variance of type parameters */
typedef enum {
    VARIANCE_INVARIANT = 0,  /* Default: no subtyping */
    VARIANCE_COVARIANT,      /* + : subtyping preserves direction */
    VARIANCE_CONTRAVARIANT,  /* - : subtyping reverses direction */
} VarianceKind;

/* Type field definition */
typedef struct TypeField {
    char* name;              /* Field name */
    char* type_name;         /* Type of field (or NULL for any) */
    FieldStrength strength;  /* Ownership strength */
    bool is_mutable;         /* Can be mutated after construction */
    int index;               /* Field index for fast access */
    VarianceKind variance;   /* Variance for parametric fields */
} TypeField;

/* ============== Metadata System (Phase 19: Syntax Alignment) ============== */

/*
 * Metadata types that can be attached to definitions via ^:key syntax.
 * Metadata provides out-of-band instructions that don't affect identity.
 */
typedef enum {
    META_NONE = 0,
    META_PARENT,             /* ^:parent {Type} - Inheritance relationship */
    META_WHERE,              /* ^:where [Constraints] - Type constraints */
    META_MUTABLE,            /* ^:mutable - Mutability marker */
    META_COVAR,              /* ^:covar - Covariant type parameter */
    META_CONTRA,             /* ^:contra - Contravariant type parameter */
    META_SEQ,                /* ^:seq - Sequential binding (let*) */
    META_REC,                /* ^:rec - Recursive binding (letrec) */
    META_PRIMITIVE,          /* {primitive} - Machine type with bit width */
    META_ABSTRACT,           /* {abstract} - Abstract type */
    META_STRUCT,             /* {struct} - Composite type */
} MetaType;

/* Single metadata entry */
typedef struct MetadataEntry {
    MetaType type;           /* Type of metadata */
    char* key;               /* Metadata key (e.g., "parent", "where") */
    OmniValue* value;        /* Associated value (type name, constraint list, etc.) */
    struct MetadataEntry* next;
} MetadataEntry;

/* ============== Phase 22: ^:where Type Constraints ============== */

/*
 * TypeConstraint - A type variable constraint from ^:where
 *
 * Example: ^:where [T {Number}]
 * Creates: TypeConstraint { name="T", bound="Number", variance=0 }
 *
 * Used for diagonal dispatch where multiple parameters share a type variable.
 */
typedef struct TypeConstraint {
    char* name;              /* Type variable name (e.g., "T") */
    char* bound;             /* Upper bound type (e.g., "Number"), NULL if none */
    int variance;            /* 0=invariant, +1=covariant, -1=contravariant */
    struct TypeConstraint* next;
} TypeConstraint;

/* Type definition in the registry */
typedef struct TypeDef {
    char* name;              /* Type name */
    TypeField* fields;       /* Array of fields */
    size_t field_count;
    size_t field_capacity;
    ShapeClass shape;        /* Inferred shape class */
    bool is_opaque;          /* True if implementation hidden */
    bool has_cycles;         /* True if type can form cycles */

    /* ============== Phase 19: Julia-Aligned Type System ============== */
    char* parent;            /* Parent type name (for inheritance) */
    int bit_width;           /* Bit width for primitives (0 for non-primitives) */
    bool is_abstract;        /* True if abstract type */
    bool is_primitive;       /* True if primitive machine type */
    MetadataEntry* metadata; /* Metadata attached to this type */
    char** type_params;      /* Type parameter names (for parametric types) */
    size_t type_param_count; /* Number of type parameters */

    struct TypeDef* next;    /* Next in registry list */
} TypeDef;

/* Ownership edge in type graph */
typedef struct OwnershipEdge {
    char* from_type;         /* Source type */
    char* from_field;        /* Source field */
    char* to_type;           /* Target type */
    bool is_back_edge;       /* True if detected as back-edge */
    struct OwnershipEdge* next;
} OwnershipEdge;

/* Global type registry */
typedef struct TypeRegistry {
    TypeDef* types;          /* List of registered types */
    OwnershipEdge* edges;    /* Ownership graph edges */
    size_t type_count;
    bool graph_built;        /* True if ownership graph computed */
    bool back_edges_analyzed; /* True if back-edges detected */
} TypeRegistry;

/* ============== Constructor-Level Ownership Tracking ============== */

/*
 * Tracks ownership at construction sites.
 * When (let ((x (mk-Foo ...))) ...), x owns the result.
 * When (set! (Foo-bar obj) val), if bar is a back-edge field and
 * val is already owned, the reference is automatically non-owning.
 */

typedef struct ConstructorOwnership {
    char* var_name;          /* Variable that owns the constructed value */
    char* type_name;         /* Type of constructed value */
    int construct_pos;       /* Position where constructed */
    int scope_id;            /* Scope in which ownership is valid */
    bool is_primary_owner;   /* True if this is the primary owner */
    struct ConstructorOwnership* next;
} ConstructorOwnership;

typedef struct FieldAssignment {
    char* target_var;        /* Object being mutated */
    char* field_name;        /* Field being set */
    char* value_var;         /* Value being assigned */
    int assign_pos;          /* Position of assignment */
    bool is_back_edge;       /* True if field is a back-edge */
    bool is_weak_assign;     /* True if assignment should be weak */
    struct FieldAssignment* next;
} FieldAssignment;

/* ============== Reuse Analysis ============== */

typedef struct ReuseCandidate {
    int alloc_pos;       /* Position of new allocation */
    int free_pos;        /* Position of corresponding free */
    char* freed_var;     /* Name of variable being freed */
    char* type_name;     /* Type being allocated */
    size_t size;         /* Size of allocation */
    bool can_reuse;      /* Can be reused for subsequent alloc */
    bool is_consumed;    /* Has this reuse opportunity been used */
    struct ReuseCandidate* next;
} ReuseCandidate;

/* ============== Scoped Escape Analysis (Phase 15: Branch-Level Region Narrowing) ============== */

/*
 * EscapeTarget - Where does a variable escape to?
 * This is finer-grained than EscapeClass for branch-level narrowing.
 */
typedef enum {
    ESCAPE_TARGET_NONE = 0,      /* Stays in current scope (best case) */
    ESCAPE_TARGET_PARENT,        /* Escapes to immediate parent scope */
    ESCAPE_TARGET_RETURN,        /* Escapes via return value */
    ESCAPE_TARGET_GLOBAL,        /* Escapes to global/module scope */
} EscapeTarget;

/*
 * ScopedVarInfo - Tracks a variable's escape status within a specific scope.
 * For each variable, we track which scope it belongs to and where it escapes to.
 * This enables "Region Narrowing" where branch-local data can be allocated
 * on the stack or scratch arena instead of the parent RC-managed region.
 */
typedef struct ScopedVarInfo {
    char* var_name;              /* Variable name */
    int defining_scope_depth;    /* Depth of scope where variable was defined */
    EscapeTarget escape_target;  /* Where does this variable escape? */
    int def_position;            /* Position where variable was defined */
    bool is_param;               /* Is this a function parameter */
    bool needs_cleanup;          /* True if needs cleanup at scope exit */
    ShapeClass shape;            /* Shape of the data structure */
    struct ScopedVarInfo* next;  /* Next variable in this scope */
} ScopedVarInfo;

/*
 * ScopeInfo - Represents a single lexical scope in the program.
 * Scopes form a tree matching the program's lexical structure.
 * Each scope tracks its variables and can determine if they escape.
 */
typedef struct ScopeInfo {
    int scope_id;                /* Unique scope identifier */
    int scope_depth;             /* Nesting depth (0 = function root) */
    int start_position;          /* First position in this scope */
    int end_position;            /* Last position in this scope */
    ScopedVarInfo* variables;    /* Variables defined in this scope */
    struct ScopeInfo* parent;    /* Enclosing scope (NULL for function root) */
    struct ScopeInfo* children;  /* Child scopes (linked list) */
    struct ScopeInfo* next_sibling; /* Next sibling at same depth */
} ScopeInfo;

/*
 * ASTNodeScopeMap - Maps AST node pointers to their corresponding scopes.
 *
 * During analysis, when we create a new scope for an AST node (like the
 * then/else branches of an if), we store the mapping so that during codegen
 * we can look up the correct scope for each AST node.
 */
typedef struct ASTNodeScopeMap {
    OmniValue* ast_node;        /* The AST node (e.g., if/then/else/let) */
    ScopeInfo* scope;           /* The scope created for this node */
    struct ASTNodeScopeMap* next;
} ASTNodeScopeMap;

/* ============== Region Analysis ============== */

typedef struct RegionInfo {
    int region_id;           /* Unique region identifier */
    char* name;              /* Optional region name */
    int scope_depth;         /* Nesting level of this region */
    int start_pos;           /* First position in region */
    int end_pos;             /* Last position in region */
    char** variables;        /* Variables allocated in this region */
    size_t var_count;
    size_t var_capacity;
    int external_refcount;   /* Count of refs from outside this region */
    bool has_escaping_refs;  /* True if any ref escapes to outer scope */
    struct RegionInfo* parent;   /* Enclosing region */
    struct RegionInfo* next;
} RegionInfo;

/* ============== RC Elision Analysis ============== */

typedef enum {
    RC_REQUIRED = 0,         /* Must use inc_ref/dec_ref */
    RC_ELIDE_INC,            /* Can skip inc_ref only */
    RC_ELIDE_DEC,            /* Can skip dec_ref only */
    RC_ELIDE_BOTH,           /* Can skip both inc and dec */
} RCElisionClass;

typedef struct RCElisionInfo {
    char* var_name;
    RCElisionClass elision;
    int region_id;           /* Which region this var belongs to */
    bool same_region_refs;   /* All refs are within same region */
    struct RCElisionInfo* next;
} RCElisionInfo;

/* ============== Borrow/Tether Analysis ============== */

typedef enum {
    BORROW_NONE = 0,
    BORROW_SHARED,           /* Multiple readers, no writers */
    BORROW_EXCLUSIVE,        /* Single reader/writer */
    BORROW_LOOP,             /* Borrowed for loop iteration */
} BorrowKind;

typedef struct BorrowInfo {
    char* borrowed_var;      /* Variable being borrowed */
    char* borrow_holder;     /* Who holds the borrow (loop var, closure, etc.) */
    BorrowKind kind;
    int start_pos;           /* Where borrow starts */
    int end_pos;             /* Where borrow ends */
    bool needs_tether;       /* Must keep alive during borrow */
    struct BorrowInfo* next;
} BorrowInfo;

typedef struct TetherPoint {
    int position;            /* Program position */
    char* tethered_var;      /* Variable being kept alive */
    bool is_entry;           /* true = tether start, false = tether end */
    bool elided;             /* True if static handle makes tether redundant */
    struct TetherPoint* next;
} TetherPoint;

/* ============== Interprocedural Summaries ============== */

typedef enum {
    PARAM_BORROWED = 0,      /* Parameter is borrowed, caller keeps ownership */
    PARAM_CONSUMED,          /* Parameter is consumed, callee frees it */
    PARAM_PASSTHROUGH,       /* Parameter passes through to return value */
    PARAM_CAPTURED,          /* Parameter is captured in closure/data structure */
} ParamOwnership;

typedef enum {
    RETURN_FRESH = 0,        /* Returns freshly allocated value */
    RETURN_PASSTHROUGH,      /* Returns one of the parameters */
    RETURN_BORROWED,         /* Returns borrowed reference (don't free) */
    RETURN_NONE,             /* Returns nil/void */
} ReturnOwnership;

typedef struct ParamSummary {
    char* name;
    char* type_annotation;   /* Type annotation from Slot syntax (e.g., "Int", "String") */
    ParamOwnership ownership;
    int passthrough_index;   /* If PARAM_PASSTHROUGH, which param passes through */
    struct ParamSummary* next;
} ParamSummary;

typedef struct FunctionSummary {
    char* name;              /* Function name */
    ParamSummary* params;    /* Parameter summaries */
    size_t param_count;
    char* return_type;       /* Return type annotation (NULL if not specified) */
    ReturnOwnership return_ownership;
    int return_param_index;  /* If RETURN_PASSTHROUGH, which param is returned */
    bool allocates;          /* Does this function allocate? */
    bool has_side_effects;   /* Does this function have side effects? */
    TypeConstraint* constraints; /* ^:where constraints for diagonal dispatch */
    struct FunctionSummary* next;
} FunctionSummary;

/* Forward declarations for concurrency types */
typedef struct ThreadLocalityInfo ThreadLocalityInfo;
typedef struct ThreadSpawnInfo ThreadSpawnInfo;
typedef struct ChannelOpInfo ChannelOpInfo;

/* ============== Component Analysis ============== */

typedef struct ComponentInfo {
    int component_id;
    int scc_id;              /* The static SCC this component represents */
    char** handles;          /* External variables (handles) pointing to this component */
    size_t handle_count;
    size_t handle_capacity;
    bool is_static;          /* True if can be collected via ASAP release_handle */
    struct ComponentInfo* next;
} ComponentInfo;

/* ============== Analysis Context ============== */

typedef struct AnalysisContext {
    /* Variable usage info */
    VarUsage* var_usages;

    /* Escape info */
    EscapeInfo* escape_info;

    /* Ownership info */
    OwnerInfo* owner_info;

    /* Shape info */
    ShapeInfo* shape_info;

    /* Component info */
    ComponentInfo* components;
    int next_component_id;

    /* Reuse candidates */
    ReuseCandidate* reuse_candidates;

    /* Region info */
    RegionInfo* regions;
    int next_region_id;
    RegionInfo* current_region;

    /* RC elision info */
    RCElisionInfo* rc_elision;

    /* Borrow tracking */
    BorrowInfo* borrows;

    /* Tether points */
    TetherPoint* tethers;

    /* Handle tracking for tether elision */
    char** active_handles;
    size_t handle_count;
    size_t handle_capacity;

    /* Function summaries */
    FunctionSummary* function_summaries;

    /* Type registry */
    TypeRegistry* type_registry;

    /* Concurrency tracking */
    ThreadLocalityInfo* thread_locality;
    ThreadSpawnInfo* thread_spawns;
    ChannelOpInfo* channel_ops;
    int current_thread_id;   /* -1 = main thread, >= 0 = spawned */

    /* Current position counter */
    int position;

    /* Function being analyzed */
    OmniValue* current_function;

    /* Constructor-level ownership tracking */
    ConstructorOwnership* constructor_owners;
    FieldAssignment* field_assignments;
    int next_scope_id;

    /* ============== Phase 15: Scoped Escape Analysis ============== */
    /* Scope tree tracking for branch-level region narrowing */
    ScopeInfo* root_scope;        /* Root scope of current function */
    ScopeInfo* current_scope;     /* Current scope during analysis */
    int next_scope_id_counter;    /* Counter for generating unique scope IDs */
    ASTNodeScopeMap* ast_scope_map; /* Maps AST nodes to their scopes */

    /* Analysis flags */
    bool in_lambda;
    bool in_return_position;
    bool in_loop;
    int scope_depth;              /* Legacy: use current_scope->scope_depth instead */
} AnalysisContext;

/* ============== Analysis API ============== */

/* Create a new analysis context */
AnalysisContext* omni_analysis_new(void);

/* Free analysis context */
void omni_analysis_free(AnalysisContext* ctx);

/* Run all analyses on an expression */
void omni_analyze(AnalysisContext* ctx, OmniValue* expr);

/* Run all analyses on a program (list of expressions) */
void omni_analyze_program(AnalysisContext* ctx, OmniValue** exprs, size_t count);

/* ============== Individual Analysis Passes ============== */

/* Liveness analysis - compute last-use positions */
void omni_analyze_liveness(AnalysisContext* ctx, OmniValue* expr);

/* Escape analysis - compute escape classifications */
void omni_analyze_escape(AnalysisContext* ctx, OmniValue* expr);

/* Ownership analysis - compute ownership and free points */
void omni_analyze_ownership(AnalysisContext* ctx, OmniValue* expr);

/* Shape analysis - compute data structure shapes */
void omni_analyze_shape(AnalysisContext* ctx, OmniValue* type_def);

/* Reuse analysis - find reuse opportunities */
void omni_analyze_reuse(AnalysisContext* ctx, OmniValue* expr);

/* ============== Query Functions ============== */

/* Get variable usage info */
VarUsage* omni_get_var_usage(AnalysisContext* ctx, const char* name);

/* ============== Type ID Query Functions (Phase 24) ============== */

/*
 * Get type_id for a variable
 * Returns the TypeID enum value assigned during type inference
 */
int omni_get_var_type_id(AnalysisContext* ctx, const char* name);

/*
 * Set type_id for a variable
 * Called during type inference to assign compile-time type constant
 */
void omni_set_var_type_id(AnalysisContext* ctx, const char* name, int type_id);

/* ============== Type Inference Functions (Phase 25) ============== */

/*
 * Analyze expression and infer/set type_id for variable
 * Called during let binding analysis to assign type_id
 *
 * Args:
 *   ctx: Analysis context
 *   var_name: Name of the variable
 *   init: Initialization expression
 *
 * Returns:
 *   The inferred TypeID enum value
 */
TypeID analyze_and_set_type_id(AnalysisContext* ctx, const char* var_name, OmniValue* init);

/* ============== Type ID Query Functions (Phase 24) ============== */

/*
 * Get type_id for a variable
 * Returns the TypeID enum value assigned during type inference
 */
int omni_get_var_type_id(AnalysisContext* ctx, const char* name);

/*
 * Set type_id for a variable
 * Called during type inference to assign compile-time type constant
 */
void omni_set_var_type_id(AnalysisContext* ctx, const char* name, int type_id);

/* Get escape classification for a variable */
EscapeClass omni_get_escape_class(AnalysisContext* ctx, const char* name);

/* Get ownership info for a variable */
OwnerInfo* omni_get_owner_info(AnalysisContext* ctx, const char* name);

/* Check if a variable should be freed at given position */
bool omni_should_free_at(AnalysisContext* ctx, const char* name, int position);

/* Get all variables that should be freed at given position */
char** omni_get_frees_at(AnalysisContext* ctx, int position, size_t* out_count);

/* Check if a type has cyclic references */
bool omni_is_cyclic_type(AnalysisContext* ctx, const char* type_name);

/* Get back-edge fields for a type */
char** omni_get_back_edge_fields(AnalysisContext* ctx, const char* type_name, size_t* out_count);

/* Check if a specific field is a back-edge (should be weak reference) */
bool omni_is_back_edge_field(AnalysisContext* ctx, const char* type_name, const char* field_name);

/* Get shape classification for a type */
ShapeClass omni_get_type_shape(AnalysisContext* ctx, const char* type_name);

/* Check if a field name looks like a back-edge by naming convention */
bool omni_is_back_edge_pattern(const char* field_name);

/* Get the free strategy for a variable (combines ownership + shape) */
FreeStrategy omni_get_free_strategy(AnalysisContext* ctx, const char* name);

/* Get free strategy name string for codegen comments */
const char* omni_free_strategy_name(FreeStrategy strategy);

/* Get allocation strategy for a variable (based on escape analysis) */
AllocStrategy omni_get_alloc_strategy(AnalysisContext* ctx, const char* name);

/* Get allocation strategy name string for codegen comments */
const char* omni_alloc_strategy_name(AllocStrategy strategy);

/* Check if a variable can be stack allocated */
bool omni_can_stack_alloc(AnalysisContext* ctx, const char* name);

/* ============== Reuse Analysis Query Functions ============== */

/* Add a reuse candidate (allocation paired with prior free) */
void omni_add_reuse_candidate(AnalysisContext* ctx, const char* freed_var,
                              const char* alloc_type, int alloc_pos);

/* Get reuse candidate for an allocation position */
ReuseCandidate* omni_get_reuse_at(AnalysisContext* ctx, int alloc_pos);

/* Mark a reuse candidate as consumed (used) */
void omni_consume_reuse(ReuseCandidate* rc);

/* Check if a freed variable's memory can be reused for a new type */
bool omni_can_reuse_for(AnalysisContext* ctx, const char* freed_var,
                        const char* new_type);

/* Get the size of a type in bytes (for reuse matching) */
size_t omni_type_size(const char* type_name);

/* ============== Region Analysis Functions ============== */

/* Create a new region */
RegionInfo* omni_region_new(AnalysisContext* ctx, const char* name);

/* End current region and return to parent */
void omni_region_end(AnalysisContext* ctx);

/* Add a variable to the current region */
void omni_region_add_var(AnalysisContext* ctx, const char* var_name);

/* Get the region a variable belongs to */
RegionInfo* omni_get_var_region(AnalysisContext* ctx, const char* var_name);

/* Check if two variables are in the same region */
bool omni_same_region(AnalysisContext* ctx, const char* var1, const char* var2);

/* ============== Per-Region External Refcount Functions ============== */

/* Increment external refcount for a region */
void omni_region_inc_external(AnalysisContext* ctx, int region_id);

/* Decrement external refcount for a region */
void omni_region_dec_external(AnalysisContext* ctx, int region_id);

/* Get the external refcount for a region */
int omni_region_get_external(AnalysisContext* ctx, int region_id);

/* Check if a reference crosses region boundaries */
bool omni_is_cross_region_ref(AnalysisContext* ctx, const char* src_var, const char* dst_var);

/* Mark a region as having escaping references */
void omni_region_mark_escaping(AnalysisContext* ctx, int region_id);

/* Check if a region can be bulk-freed (no external refs) */
bool omni_region_can_bulk_free(AnalysisContext* ctx, int region_id);

/* Get region by ID */
RegionInfo* omni_get_region_by_id(AnalysisContext* ctx, int region_id);

/* ============== RC Elision Functions ============== */

/* Analyze RC elision opportunities for an expression */
void omni_analyze_rc_elision(AnalysisContext* ctx, OmniValue* expr);

/* Get RC elision class for a variable */
RCElisionClass omni_get_rc_elision(AnalysisContext* ctx, const char* var_name);

/* Get RC elision class name for debugging */
const char* omni_rc_elision_name(RCElisionClass elision);

/* Check if inc_ref can be elided for a variable */
bool omni_can_elide_inc_ref(AnalysisContext* ctx, const char* var_name);

/* Check if dec_ref can be elided for a variable */
bool omni_can_elide_dec_ref(AnalysisContext* ctx, const char* var_name);

/* ============== Borrow/Tether Functions ============== */

/* Analyze borrow patterns in an expression */
void omni_analyze_borrows(AnalysisContext* ctx, OmniValue* expr);

/* Start a borrow (e.g., beginning of loop over collection) */
void omni_borrow_start(AnalysisContext* ctx, const char* borrowed_var,
                       const char* holder, BorrowKind kind);

/* End a borrow */
void omni_borrow_end(AnalysisContext* ctx, const char* borrowed_var);

/* Check if a variable is currently borrowed */
bool omni_is_borrowed(AnalysisContext* ctx, const char* var_name);

/* Get the borrow info for a variable */
BorrowInfo* omni_get_borrow_info(AnalysisContext* ctx, const char* var_name);

/* Add a tether point (keep-alive) */
void omni_add_tether(AnalysisContext* ctx, const char* var_name, bool is_entry);

/* Get tether points at a position */
TetherPoint** omni_get_tethers_at(AnalysisContext* ctx, int position, size_t* count);

/* Check if a variable needs tethering at a position */
bool omni_needs_tether(AnalysisContext* ctx, const char* var_name, int position);

/* Get borrow kind name for debugging */
const char* omni_borrow_kind_name(BorrowKind kind);

/* ============== Interprocedural Summary Functions ============== */

/* Analyze a function definition and create its summary */
void omni_analyze_function_summary(AnalysisContext* ctx, OmniValue* func_def);

/* Get the summary for a function by name */
FunctionSummary* omni_get_function_summary(AnalysisContext* ctx, const char* func_name);

/* Get parameter ownership for a function */
ParamOwnership omni_get_param_ownership(AnalysisContext* ctx, const char* func_name,
                                        const char* param_name);

/* Get return ownership for a function */
ReturnOwnership omni_get_return_ownership(AnalysisContext* ctx, const char* func_name);

/* Check if a function consumes a parameter */
bool omni_function_consumes_param(AnalysisContext* ctx, const char* func_name,
                                  const char* param_name);

/* Check if caller should free after call */
bool omni_caller_should_free_arg(AnalysisContext* ctx, const char* func_name,
                                 int arg_index);

/* Get param ownership name for debugging */
const char* omni_param_ownership_name(ParamOwnership ownership);

/* Get return ownership name for debugging */
const char* omni_return_ownership_name(ReturnOwnership ownership);

/* ============== Control Flow Graph ============== */

typedef struct CFGNode {
    int id;
    int position_start;      /* First position in this basic block */
    int position_end;        /* Last position in this basic block */

    /* Control flow edges */
    struct CFGNode** successors;
    size_t succ_count;
    size_t succ_capacity;
    struct CFGNode** predecessors;
    size_t pred_count;
    size_t pred_capacity;

    /* Variables used/defined in this node */
    char** uses;             /* Variables read in this block */
    size_t use_count;
    char** defs;             /* Variables defined in this block */
    size_t def_count;

    /* Liveness sets (computed by dataflow) */
    char** live_in;          /* Live at entry to this node */
    size_t live_in_count;
    char** live_out;         /* Live at exit from this node */
    size_t live_out_count;

    /* Dominator Analysis */
    struct CFGNode* idom;    /* Immediate dominator */
    struct CFGNode** doms;   /* Dominator set */
    size_t dom_count;

    /* SCC Analysis */
    int scc_id;              /* SCC identifier (-1 if not in a cycle) */
    bool is_scc_entry;       /* True if node is the dominator of its SCC */

    /* Node type for structured control flow */
    enum {
        CFG_BASIC,           /* Basic block */
        CFG_BRANCH,          /* If condition */
        CFG_JOIN,            /* Merge point after if */
        CFG_LOOP_HEAD,       /* Loop header */
        CFG_LOOP_EXIT,       /* Loop exit point */
        CFG_ENTRY,           /* Function entry */
        CFG_EXIT,            /* Function exit */
    } node_type;
} CFGNode;

typedef struct CFG {
    CFGNode** nodes;
    size_t node_count;
    size_t node_capacity;
    CFGNode* entry;
    CFGNode* exit;
} CFG;

/* Build CFG from expression */
CFG* omni_build_cfg(OmniValue* expr);

/* Free CFG */
void omni_cfg_free(CFG* cfg);

/* Compute dominators for CFG */
void omni_compute_dominators(CFG* cfg);

/* Compute strongly connected components for CFG */
void omni_compute_scc(CFG* cfg);

/* Static Symmetric RC analysis */
void omni_analyze_static_symmetric(AnalysisContext* ctx, CFG* cfg);

/* Component analysis */
void omni_analyze_components(AnalysisContext* ctx, CFG* cfg);

/* Tether elision optimization */
void omni_optimize_tethers(AnalysisContext* ctx, CFG* cfg);

/* Compute liveness using backward dataflow */
void omni_compute_liveness(CFG* cfg, AnalysisContext* ctx);

/* Get variables that should be freed at end of a CFG node */
char** omni_get_frees_for_node(CFG* cfg, CFGNode* node,
                                AnalysisContext* ctx, size_t* out_count);

/* Print CFG for debugging */
void omni_print_cfg(CFG* cfg);

/* ============== CFG-Based Free Points ============== */

typedef struct CFGFreePoint {
    CFGNode* node;           /* The CFG node */
    char** vars;             /* Variables to free after this node */
    size_t var_count;
    struct CFGFreePoint* next;
} CFGFreePoint;

/* Compute CFG-aware free points */
CFGFreePoint* omni_compute_cfg_free_points(CFG* cfg, AnalysisContext* ctx);

/* Free CFG free points list */
void omni_cfg_free_points_free(CFGFreePoint* points);

/* ============== Issue 3 P2: Non-Lexical Region End ============== */

typedef struct RegionExitPoint {
    int region_id;               /* Region ID to exit */
    int position;               /* Position where region should exit */
    struct RegionExitPoint* next;
} RegionExitPoint;

/* Compute region exit points from liveness analysis */
RegionExitPoint* omni_compute_region_exit_points(AnalysisContext* ctx);

/* Free region exit point list */
void omni_region_exit_points_free(RegionExitPoint* points);

/* ============== ASAP Free Injection ============== */

typedef struct FreePoint {
    int position;
    char** vars;
    size_t var_count;
    struct FreePoint* next;
} FreePoint;

/* Compute all free injection points for a function */
FreePoint* omni_compute_free_points(AnalysisContext* ctx, OmniValue* func);

/* Free the free points list */
void omni_free_points_free(FreePoint* points);

/* ============== Concurrency Ownership Inference ============== */

/* Thread locality classification */
typedef enum {
    THREAD_LOCAL = 0,        /* Data stays in one thread */
    THREAD_SHARED,           /* Data accessed by multiple threads */
    THREAD_TRANSFER,         /* Data transferred via message passing */
    THREAD_IMMUTABLE,        /* Immutable data - can be freely shared */
} ThreadLocality;

/* Channel operation type */
typedef enum {
    CHAN_SEND = 0,           /* Send ownership to channel */
    CHAN_RECV,               /* Receive ownership from channel */
    CHAN_CLOSE,              /* Close channel */
} ChannelOp;

/* Thread spawn info */
typedef struct ThreadSpawnInfo {
    int spawn_pos;           /* Position of spawn */
    char* thread_id;         /* Thread identifier */
    char** captured_vars;    /* Variables captured by thread */
    size_t captured_count;
    ThreadLocality* capture_locality;  /* Locality of each capture */
    struct ThreadSpawnInfo* next;
} ThreadSpawnInfo;

/* Channel operation tracking */
typedef struct ChannelOpInfo {
    int position;            /* Position of operation */
    ChannelOp op;            /* Type of operation */
    char* channel_name;      /* Name of channel variable */
    char* value_var;         /* Variable being sent/received */
    bool transfers_ownership; /* Does this transfer ownership? */
    struct ChannelOpInfo* next;
} ChannelOpInfo;

/* Thread locality info for variables */
typedef struct ThreadLocalityInfo {
    char* var_name;
    ThreadLocality locality;
    int thread_id;           /* -1 for shared, >= 0 for specific thread */
    bool needs_atomic_rc;    /* True if needs atomic refcount operations */
    bool is_message;         /* True if sent via channel */
    struct ThreadLocalityInfo* next;
} ThreadLocalityInfo;

/* ============== Concurrency Analysis Functions ============== */

/* Analyze concurrency patterns in an expression */
void omni_analyze_concurrency(AnalysisContext* ctx, OmniValue* expr);

/* Get thread locality for a variable */
ThreadLocality omni_get_thread_locality(AnalysisContext* ctx, const char* var_name);

/* Check if a variable needs atomic refcount operations */
bool omni_needs_atomic_rc(AnalysisContext* ctx, const char* var_name);

/* Check if a variable is sent via channel (ownership transfer) */
bool omni_is_channel_transferred(AnalysisContext* ctx, const char* var_name);

/* Mark a variable as thread-local */
void omni_mark_thread_local(AnalysisContext* ctx, const char* var_name, int thread_id);

/* Mark a variable as shared between threads */
void omni_mark_thread_shared(AnalysisContext* ctx, const char* var_name);

/* Record a channel send operation */
void omni_record_channel_send(AnalysisContext* ctx, const char* channel,
                              const char* value_var, bool transfers_ownership);

/* Record a channel receive operation */
void omni_record_channel_recv(AnalysisContext* ctx, const char* channel,
                              const char* value_var);

/* Record a thread spawn */
void omni_record_thread_spawn(AnalysisContext* ctx, const char* thread_id,
                              char** captured_vars, size_t count);

/* Get thread locality name for debugging */
const char* omni_thread_locality_name(ThreadLocality locality);

/* Get channel op name for debugging */
const char* omni_channel_op_name(ChannelOp op);

/* Check if caller should free after send (usually false - ownership transfers) */
bool omni_should_free_after_send(AnalysisContext* ctx, const char* channel,
                                 const char* var_name);

/* Get the spawned threads that capture a variable */
ThreadSpawnInfo** omni_get_threads_capturing(AnalysisContext* ctx, const char* var_name,
                                             size_t* count);

/* ============== Type Registry Functions ============== */

/* Create a new type registry */
TypeRegistry* omni_type_registry_new(void);

/* Free type registry */
void omni_type_registry_free(TypeRegistry* reg);

/* Register a type from a deftype expression */
TypeDef* omni_register_type(AnalysisContext* ctx, OmniValue* type_def);

/* Get a type definition by name */
TypeDef* omni_get_type(AnalysisContext* ctx, const char* name);

/* Get a field definition from a type */
TypeField* omni_get_type_field(TypeDef* type, const char* field_name);

/* Add a field to a type */
void omni_type_add_field(TypeDef* type, const char* name, const char* field_type,
                         FieldStrength strength, bool is_mutable);

/* Build ownership graph from registered types */
void omni_build_ownership_graph(AnalysisContext* ctx);

/* Analyze back-edges using DFS cycle detection */
void omni_analyze_back_edges(AnalysisContext* ctx);

/* Check if a field should be treated as weak (back-edge) */
bool omni_is_weak_field(AnalysisContext* ctx, const char* type_name, const char* field_name);

/* Get field strength */
FieldStrength omni_get_field_strength(AnalysisContext* ctx, const char* type_name,
                                       const char* field_name);

/* Get field strength name for debugging */
const char* omni_field_strength_name(FieldStrength strength);

/* Generate constructor for a type */
void omni_gen_type_constructor(AnalysisContext* ctx, TypeDef* type);

/* Generate field accessor for a type */
void omni_gen_field_accessor(AnalysisContext* ctx, TypeDef* type, TypeField* field);

/* Generate field mutator for a type (respects weak/strong) */
void omni_gen_field_mutator(AnalysisContext* ctx, TypeDef* type, TypeField* field);

/* Generate release function for a type (skips weak fields) */
void omni_gen_type_release(AnalysisContext* ctx, TypeDef* type);

/* ============== Phase 19: Julia-Aligned Type System API ============== */

/*
 * Metadata Handling Functions
 * Extract and process metadata (^:key) from definitions.
 */

/* Extract metadata from a form (returns linked list of MetadataEntry) */
MetadataEntry* omni_extract_metadata(OmniValue* form);

/* Free metadata list */
void omni_free_metadata(MetadataEntry* metadata);

/* Get metadata value by key */
OmniValue* omni_get_metadata(MetadataEntry* metadata, const char* key);

/* Check if metadata contains a specific key */
bool omni_has_metadata(MetadataEntry* metadata, const char* key);

/*
 * Type Hierarchy Functions
 * Handle inheritance and subtyping relationships.
 */

/* Set parent type for a type definition */
void omni_type_set_parent(TypeDef* type, const char* parent_name);

/* Get parent type definition */
TypeDef* omni_type_get_parent(AnalysisContext* ctx, TypeDef* type);

/* Check if type_a is a subtype of type_b */
bool omni_type_is_subtype(AnalysisContext* ctx, const char* type_a, const char* type_b);

/* ============== Phase 22: Bottom Type Support ============== */

/* Check if a type is the bottom type (never returns) */
bool omni_is_bottom_type(const char* type_name);

/* Check if a function returns bottom (diverges) */
bool omni_function_returns_bottom(AnalysisContext* ctx, const char* func_name);

/* Enhanced subtype check with bottom support */
bool omni_is_subtype_with_bottom(AnalysisContext* ctx, const char* type_a, const char* type_b);

/* ============== Phase 22: ^:where Constraint Functions ============== */

/* Parse ^:where [T {Bound}] and return TypeConstraint list */
TypeConstraint* omni_parse_where_constraints(OmniValue* where_value);

/* Variance-aware parametric subtype check */
bool omni_is_parametric_subtype(AnalysisContext* ctx, TypeDef* base_type,
                                const char** params_a, size_t count_a,
                                const char** params_b, size_t count_b);

/* Get variance for a type parameter at index */
VarianceKind omni_get_type_param_variance_by_index(TypeDef* type, size_t index);

/* Check if concrete types satisfy constraints (diagonal dispatch) */
bool omni_check_constraints(AnalysisContext* ctx, TypeConstraint* constraints,
                            const char** arg_types, size_t arg_count);

/* Free constraint list */
void omni_free_constraints(TypeConstraint* constraints);

/* Compute specificity score for method dispatch (higher = more specific) */
int omni_compute_specificity(AnalysisContext* ctx, TypeDef* type);

/* Register a type with full metadata support */
TypeDef* omni_register_type_with_metadata(AnalysisContext* ctx, OmniValue* type_def,
                                          MetadataEntry* metadata);

/* Register a primitive type with bit width */
TypeDef* omni_register_primitive_type(AnalysisContext* ctx, const char* name,
                                      const char* parent, int bit_width);

/* Register an abstract type */
TypeDef* omni_register_abstract_type(AnalysisContext* ctx, const char* name,
                                     const char* parent);

/* Register a struct type with fields */
TypeDef* omni_register_struct_type(AnalysisContext* ctx, const char* name,
                                   const char* parent, OmniValue* fields);

/* ============== Phase 22: Union and Function Type Registration ============== */

/* Register a union type */
TypeDef* omni_register_union_type(AnalysisContext* ctx, const char* name,
                                  OmniValue* members_array);

/* Register a function type */
TypeDef* omni_register_function_type(AnalysisContext* ctx, const char* name,
                                     OmniValue* sig_array);

/*
 * Parametric Type Functions
 * Handle type parameters and variance.
 */

/* Add type parameter to a type definition */
void omni_type_add_param(TypeDef* type, const char* param_name, VarianceKind variance);

/* Get variance of a type parameter */
VarianceKind omni_type_get_param_variance(TypeDef* type, const char* param_name);

/* Create a parametric type instance (e.g., (List Int)) */
TypeDef* omni_make_parametric_instance(AnalysisContext* ctx, const char* base_type,
                                       char** type_args, size_t arg_count);

/* ============== Constructor-Level Ownership API ============== */

/*
 * Register that a variable owns a constructed value.
 * Called when analyzing (let ((x (mk-Foo ...))) ...)
 */
void omni_register_constructor_owner(AnalysisContext* ctx, const char* var_name,
                                      const char* type_name, int pos);

/*
 * Check if a variable is the primary owner of its value.
 * Used to determine if a reference should be strong or weak.
 */
bool omni_is_primary_owner(AnalysisContext* ctx, const char* var_name);

/*
 * Get the type name of a variable's value (if known from construction).
 */
const char* omni_get_constructed_type(AnalysisContext* ctx, const char* var_name);

/*
 * Register a field assignment.
 * Called when analyzing (set! (Type-field obj) val)
 * Automatically marks as weak if field is a back-edge and val is already owned.
 */
void omni_register_field_assignment(AnalysisContext* ctx, const char* target_var,
                                     const char* field_name, const char* value_var,
                                     int pos);

/*
 * Check if a field assignment should be treated as weak.
 * Returns true if:
 *   1. The field is a back-edge pattern, or
 *   2. The value being assigned is already owned by another variable
 */
bool omni_is_weak_assignment(AnalysisContext* ctx, const char* target_var,
                              const char* field_name, const char* value_var);

/*
 * Free constructor ownership tracking data.
 */
void omni_free_constructor_owners(AnalysisContext* ctx);

/* ============== Phase 15: Scoped Escape Analysis API ============== */

/*
 * Scope Management Functions
 * These functions manage the scope tree for branch-level escape analysis.
 */

/* Enter a new scope (e.g., when entering an if/then/else branch or let body) */
ScopeInfo* omni_scope_enter(AnalysisContext* ctx, const char* scope_type);

/* Exit the current scope and return to parent */
void omni_scope_exit(AnalysisContext* ctx);

/* Get the current scope */
ScopeInfo* omni_get_current_scope(AnalysisContext* ctx);

/*
 * Scoped Variable Tracking
 * Track variables and their escape status within scopes.
 */

/* Add a variable to the current scope */
ScopedVarInfo* omni_scope_add_var(AnalysisContext* ctx, const char* var_name,
                                  int def_position, bool is_param);

/* Mark a variable as escaping to a specific target */
void omni_scope_mark_escape(AnalysisContext* ctx, const char* var_name,
                            EscapeTarget target);

/* Get escape target for a variable in the current scope */
EscapeTarget omni_scope_get_escape_target(AnalysisContext* ctx, const char* var_name);

/* Get the defining scope for a variable */
ScopeInfo* omni_scope_get_defining_scope(AnalysisContext* ctx, const char* var_name);

/*
 * Scoped Variable Query Functions
 * Query variable information from the scope tree.
 */

/* Get ScopedVarInfo for a variable by name (searches up the scope tree) */
ScopedVarInfo* omni_scope_find_var(AnalysisContext* ctx, const char* var_name);

/* Get ScopedVarInfo for a variable in a specific scope */
ScopedVarInfo* omni_scope_find_var_in_scope(ScopeInfo* scope, const char* var_name);

/* Get all variables that should be freed at scope exit */
char** omni_scope_get_cleanup_vars(ScopeInfo* scope, size_t* out_count);

/* Find a scope by position (for codegen lookup) */
ScopeInfo* omni_scope_find_by_position(AnalysisContext* ctx, int position);

/* Find a scope by AST node pointer (for codegen lookup) */
ScopeInfo* omni_scope_find_by_ast_node(AnalysisContext* ctx, OmniValue* node);

/* Store a mapping from AST node to scope */
void omni_scope_map_ast_node(AnalysisContext* ctx, OmniValue* node, ScopeInfo* scope);

/* Free a scope and all its children */
void omni_scope_free(ScopeInfo* scope);

/* Free the entire scope tree */
void omni_scope_free_tree(AnalysisContext* ctx);

/* Free the AST node scope map */
void omni_scope_free_ast_map(AnalysisContext* ctx);

/*
 * Debug/Utility Functions
 */

/* Get the name of an EscapeTarget for debugging */
const char* omni_escape_target_name(EscapeTarget target);

/* Print the scope tree (for debugging) */
void omni_scope_print_tree(AnalysisContext* ctx);

/*
 * Scoped Escape Analysis Entry Point
 * Run the full scoped escape analysis on an expression.
 */
void omni_analyze_scoped_escape(AnalysisContext* ctx, OmniValue* expr);

/*
 * Type-Based Dispatch Support (Phase 19)
 * These functions support type checking and type-based dispatch.
 */

/* Look up a function signature by name */
FunctionSummary* omni_lookup_function_signature(AnalysisContext* ctx, const char* func_name);

/* Extract type annotation from a parameter node (e.g., [x {Int}] => "Int") */
char* omni_extract_type_annotation(OmniValue* param_node);

/* Check if an argument node is compatible with a parameter type */
bool omni_check_argument_type_compatibility(AnalysisContext* ctx, const char* param_type,
                                           OmniValue* arg_node);

/* Get parameter by index from function summary */
ParamSummary* omni_get_param_by_index(FunctionSummary* func, int index);

#ifdef __cplusplus
}
#endif

#endif /* OMNILISP_ANALYSIS_H */

#!/bin/bash
# Fix the grammar DSL tests to remove tower_cleanup() calls between grammar definition and match

file="test_pika_tower.c"

# For each test function, remove tower_cleanup() calls after the grammar definition
# but keep the final tower_cleanup() at the end

# Fix grammar_dsl_clause_primitives
sed -i '/^TEST(grammar_dsl_clause_primitives)/,/^}$/ {
  /Define grammar using/,/^    Value\* result = tower_eval_string(program/ {
    /tower_cleanup();/d
  }
}' "$file"

# Fix grammar_dsl_quantifiers  
sed -i '/^TEST(grammar_dsl_quantifiers)/,/^}$/ {
  /Define grammar using/,/^    Value\* result = tower_eval_string(program/ {
    /tower_cleanup();/d
  }
}' "$file"

# Fix grammar_dsl_charset
sed -i '/^TEST(grammar_dsl_charset)/,/^}$/ {
  /Define grammar using/,/^    Value\* result = tower_eval_string(program/ {
    /tower_cleanup();/d
  }
}' "$file"

# Fix grammar_dsl_find_all
sed -i '/^TEST(grammar_dsl_find_all)/,/^}$/ {
  /Define grammar using/,/^    Value\* result = tower_eval_string(program/ {
    /tower_cleanup();/d
  }
}' "$file"

# Fix grammar_dsl_lookahead
sed -i '/^TEST(grammar_dsl_lookahead)/,/^}$/ {
  /Define grammar using/,/^    Value\* result = tower_eval_string(program/ {
    /tower_cleanup();/d
  }
}' "$file"

# Fix grammar_dsl_label
sed -i '/^TEST(grammar_dsl_label)/,/^}$/ {
  /Define grammar/,/^    Value\* result = tower_eval_string(program/ {
    /tower_cleanup();/d
  }
}' "$file"

echo "Done fixing tests"

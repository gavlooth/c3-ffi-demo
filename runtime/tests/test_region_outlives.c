/*
 * test_region_outlives.c - Tests for Issue 2 P4.3b ancestry metadata
 *
 * Tests: parent/ancestry tracking, sibling handling, NULL safety
 */

#include "test_main.h"

/* Helper: Create child region with parent linkage */
static Region* create_child_region(Region* parent) {
    Region* child = region_create();
    if (parent && child) {
        omni_region_set_parent(child, parent);
    }
    return child;
}

TEST("parent outlives child") {
    Region* root = region_create();
    Region* child = create_child_region(root);

    ASSERT(omni_region_outlives(root, child) == true);
    ASSERT(omni_region_outlives(child, root) == false);

    region_exit(root);
    region_destroy_if_dead(root);
    region_exit(child);
    region_destroy_if_dead(child);
}

TEST("child does not outlive parent") {
    Region* parent = region_create();
    Region* child = create_child_region(parent);

    ASSERT(omni_region_outlives(child, parent) == false);

    region_exit(parent);
    region_destroy_if_dead(parent);
    region_exit(child);
    region_destroy_if_dead(child);
}

TEST("same rank regions (siblings) do not outlive each other") {
    Region* root = region_create();
    Region* child1 = create_child_region(root);
    Region* child2 = create_child_region(root);

    // Both children have same rank (rank 1)
    ASSERT(omni_region_get_lifetime_rank(child1) == 1);
    ASSERT(omni_region_get_lifetime_rank(child2) == 1);

    // But they are siblings, so neither outlives the other
    ASSERT(omni_region_outlives(child1, child2) == false);
    ASSERT(omni_region_outlives(child2, child1) == false);

    region_exit(root);
    region_destroy_if_dead(root);
    region_exit(child1);
    region_destroy_if_dead(child1);
    region_exit(child2);
    region_destroy_if_dead(child2);
}

TEST("grandparent outlives grandchild") {
    Region* grandparent = region_create();
    Region* parent = create_child_region(grandparent);
    Region* child = create_child_region(parent);

    ASSERT(omni_region_outlives(grandparent, child) == true);
    ASSERT(omni_region_outlives(child, grandparent) == false);

    region_exit(grandparent);
    region_destroy_if_dead(grandparent);
    region_exit(parent);
    region_destroy_if_dead(parent);
    region_exit(child);
    region_destroy_if_dead(child);
}

TEST("region outlives itself") {
    Region* r = region_create();

    ASSERT(omni_region_outlives(r, r) == true);

    region_exit(r);
    region_destroy_if_dead(r);
}

TEST("NULL regions handled safely") {
    Region* r = region_create();

    // NULL should not outlive anything
    ASSERT(omni_region_outlives(NULL, r) == false);
    ASSERT(omni_region_outlives(r, NULL) == false);
    ASSERT(omni_region_outlives(NULL, NULL) == false);

    region_exit(r);
    region_destroy_if_dead(r);
}

/* Run: tests */
void run_region_outlives_tests(void) {
    RUN_TEST(parent_outlives_child);
    RUN_TEST(child_does_not_outlive_parent);
    RUN_TEST(same_rank_regions_do_not_outlive_each_other);
    RUN_TEST(grandparent_outlives_grandchild);
    RUN_TEST(region_outlives_itself);
    RUN_TEST(NULL_regions_handled_safely);
}

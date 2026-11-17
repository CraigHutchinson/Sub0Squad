# Test tiers and areas (docs/DEV_SETUP.md "Test tiers"; the family's CI strategy, CraigHutchinson/.github docs/ci-strategy.md).
#
# Every ctest test carries exactly one TIER label and one AREA label:
#   smoke  the quick set: a few fast tests from every area, run on every change (`ctest -L smoke`, or `ctest --preset quick`);
#   full   everything else: the long suites, run at the quality gates (`ctest --preset full` runs smoke and full together).
# The family's `quick` preset is "everything not labelled full", so with one tier label per test it is exactly the smoke set.
#
# Areas: structure (layering, link proofs, test reporting), platform, kernel (durable jobs), workflow (plan, policy, stages),
# harness (project harnesses and gates), agents (executors, canvas, fork, planner, mends), portal, e2e (real daemon and CLI scenarios).
# Include this file after every add_test; a test with no tier or no area fails the configure, so a new test cannot be left out.

set(SUB0SQUAD_TEST_TIERS smoke full)
set(SUB0SQUAD_TEST_AREAS structure platform kernel workflow harness agents portal e2e)

# Gives each named test the area and tier labels. A name that is not a test on this system (m0_graph_growth is POSIX only) is skipped.
function(sub0squad_label_tests area tier)
  foreach(test ${ARGN})
    if(TEST ${test})
      set_property(TEST ${test} APPEND PROPERTY LABELS ${area} ${tier})
    endif()
  endforeach()
endfunction()

# The top layer's link proof links every layer below it, so it is the smoke representative of all of them.
set(otherLinkProofs ${SUB0SQUAD_LINK_PROOFS})
list(REMOVE_ITEM otherLinkProofs link_presentation)

sub0squad_label_tests(structure smoke layering test_reporting dependency_selftest link_presentation)
sub0squad_label_tests(structure full  ${otherLinkProofs})
sub0squad_label_tests(platform  smoke test_platform_process test_platform_file)
sub0squad_label_tests(platform  full  test_platform_proc test_platform_host test_platform_net test_platform_wait)
sub0squad_label_tests(kernel    smoke test_job_spec test_journal)
sub0squad_label_tests(kernel    full  test_job_store test_shim test_idle test_host_guard test_archive test_adapter m0_graph_growth)
sub0squad_label_tests(workflow  smoke test_policy test_workflow test_plan)
sub0squad_label_tests(workflow  full  test_workflow_stages test_workflow_mends test_process test_predicate test_stance test_clarify)
sub0squad_label_tests(harness   smoke test_diagnostics test_edits)
sub0squad_label_tests(harness   full  test_platforms test_project)
sub0squad_label_tests(agents    smoke test_agent test_mends)
sub0squad_label_tests(agents    full  test_canvas_agent test_fork test_planner test_blocks test_stream)
sub0squad_label_tests(portal    smoke test_portal test_portal_page)
sub0squad_label_tests(portal    full  test_wizard)
sub0squad_label_tests(e2e       smoke test_e2e)
sub0squad_label_tests(e2e       full  test_readme test_scenarios test_executors test_heal test_nearpass test_thrash)

# Every test has exactly one tier and exactly one area.
get_property(allTests DIRECTORY PROPERTY TESTS)
set(unlabelled "")
foreach(test ${allTests})
  get_property(labels TEST ${test} PROPERTY LABELS)
  set(tiers ${labels})
  list(FILTER tiers INCLUDE REGEX "^(smoke|full)$")
  set(areas ${labels})
  list(FILTER areas INCLUDE REGEX "^(structure|platform|kernel|workflow|harness|agents|portal|e2e)$")
  list(LENGTH tiers tierCount)
  list(LENGTH areas areaCount)
  if(NOT tierCount EQUAL 1 OR NOT areaCount EQUAL 1)
    list(APPEND unlabelled "${test} (labels: ${labels})")
  endif()
endforeach()
if(unlabelled)
  string(REPLACE ";" ", " msg "${unlabelled}")
  message(FATAL_ERROR "every test needs one tier label (${SUB0SQUAD_TEST_TIERS}) and one area label (${SUB0SQUAD_TEST_AREAS}) in cmake/TestTiers.cmake: ${msg}")
endif()

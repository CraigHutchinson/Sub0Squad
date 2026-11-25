"""Check the tool-use scorer: its scripted controls and the edge cases they do not reach.

    cd validation/tiers && python -B -m unittest test_t0_tools
"""
import unittest

from t0_tools import CONTROLS, REFERENCE, SCENARIO, SCENARIOS, Scenario, parse_reply, score, selftest, wilson

TWO_SAME = "x = 1;\nx = 1;\n"


class ToolScorerTest(unittest.TestCase):
    def test_scripted_controls_agree_with_the_scorer(self):
        self.assertEqual(selftest(), [])

    def test_every_scenario_has_a_passing_reference_and_a_failing_control(self):
        for scenario in SCENARIOS:
            arm = "need" if scenario.need else "plain"
            self.assertEqual(score(scenario, REFERENCE[scenario.id], arm)["tier"], "pass", scenario.id)
            self.assertTrue(any(c[0] == scenario.id and c[3] != "pass" for c in CONTROLS), scenario.id)

    def test_a_search_matching_twice_is_malformed(self):
        scenario = Scenario("dup", "t", {"a.cpp": TWO_SAME}, frozenset({"search"}), expect=frozenset({"search"}))
        text = "=== EDIT: a.cpp ===\n<<<<<<< SEARCH\nx = 1;\n=======\nx = 2;\n>>>>>>> REPLACE\n=== END EDIT ===\n"
        result = score(scenario, text)
        self.assertEqual((result["tier"], result["kind"]), ("T3", "malformed"))
        self.assertIn("occurs 2 times", result["problems"][0])

    def test_file_may_replace_a_stub_but_not_a_real_file(self):
        stub = Scenario("stub", "t", {"a.cpp": "// stub\n"}, frozenset({"file"}), expect=frozenset({"file"}), stubs=frozenset({"a.cpp"}))
        real = Scenario("real", "t", {"a.cpp": "int f() { return 1; }\n"}, frozenset({"file"}), expect=frozenset({"file"}))
        text = "=== FILE: a.cpp ===\nint g() { return 2; }\n=== END FILE ===\n"
        self.assertEqual(score(stub, text)["tier"], "pass")
        self.assertEqual(score(real, text)["kind"], "malformed")

    def test_an_unterminated_edit_is_malformed(self):
        scenario = SCENARIO["function-missing"]
        result = score(scenario, "=== EDIT: src/calc.cpp SYMBOL factorial ===\ndouble factorial(int n) { return n; }\n")
        self.assertEqual((result["tier"], result["kind"]), ("T3", "malformed"))
        self.assertIn("has no", result["problems"][0])

    def test_a_read_with_another_request_is_malformed(self):
        scenario = SCENARIO["folded-function"]
        text = "=== READ: src/calc.cpp SYMBOL parse_power ===\n=== READ: src/calc.cpp LINES 3-5 ===\n"
        result = score(scenario, text)
        self.assertEqual((result["tier"], result["kind"]), ("T3", "malformed"))
        self.assertIn("whole reply", result["problems"][0])

    def test_a_find_tool_request_must_stand_alone(self):
        scenario = SCENARIO["need-run-tests"]
        text = "=== FIND TOOL: run the unit tests ===\n=== FIND TOOL: run the suite ===\n"
        result = score(scenario, text, "need")
        self.assertEqual((result["tier"], result["kind"]), ("T3", "malformed"))

    def test_an_invented_form_fails_invocation(self):
        scenario = SCENARIO["whole-function-wrong"]
        result = score(scenario, "=== RUN TESTS: src/calc.cpp ===\n", "plain")
        self.assertEqual((result["tier"], result["kind"]), ("T1", "invented-form"))

    def test_a_form_outside_the_arm_is_not_offered(self):
        scenario = SCENARIO["need-run-tests"]
        result = score(scenario, REFERENCE["need-run-tests"], "plain")
        self.assertEqual((result["tier"], result["kind"]), ("T1", "not-offered"))

    def test_lines_outside_the_file_fail(self):
        scenario = SCENARIO["control-read-lines"]
        result = score(scenario, "=== READ: src/calc.cpp LINES 4-9 ===\n", "plain")
        self.assertEqual((result["tier"], result["kind"]), ("T3", "malformed"))

    def test_parse_keeps_prose_and_structure_separate(self):
        transactions, prose, problems = parse_reply("Here:\n=== READ: a.cpp LINES 1-2 ===\nDone.\n")
        self.assertEqual([t["verb"] for t in transactions], ["READ"])
        self.assertEqual(prose, ["Here:", "Done."])
        self.assertEqual(problems, [])

    def test_wilson_bounds_match_the_documented_values(self):
        low, high = wilson(10, 10)
        self.assertAlmostEqual(low, 0.72, places=2)
        self.assertAlmostEqual(high, 1.0, places=2)
        self.assertAlmostEqual(wilson(9, 10)[0], 0.60, places=2)


if __name__ == "__main__":
    unittest.main()

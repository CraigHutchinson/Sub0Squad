"""Check token usage reporting and the cost gate boundary."""
import json
import pathlib
import tempfile
import unittest
from unittest.mock import patch

from gate_t0 import attempt_cost, case_state, cost_limits, cost_mode, judge_cost, judge_run, load_cost_policy, wait_run
from t0_diagnostics import compare_golden_path, repair_diagnostics


class TokenGateTest(unittest.TestCase):
    def test_interruption_records_lower_bound_and_missing_usage(self):
        with tempfile.TemporaryDirectory() as directory:
            attempt = pathlib.Path(directory)
            (attempt / "conversation.md").write_text(
                "## Turn 0 (12:00), request seed 4\n"
                "### Reply, turn 0 (12:01): 100 prompt tokens, 20 completion tokens, finish stop, 1.2 s prompt, 40 of them already cached on the slot\n"
                "Answer:\n\n```\n"
                "## Turn 99 (12:01)\n"
                "### Reply, turn 99 (12:01): 9000 prompt tokens, 9000 completion tokens, finish stop\n"
                "```\n"
                "## Turn 1 (12:01), request seed 5\n"
                "### Reply, turn 1 (12:02): 0 prompt tokens, 0 completion tokens, finish aborted\n",
                encoding="utf-8",
            )
            cost = attempt_cost(attempt, {"turns": 2, "tokensIn": 100, "tokensOut": 20})
        self.assertEqual(cost, {"turns": 2, "tokensIn": 100, "tokensOut": 20, "prefillSeconds": 1.2,
                                "prefillTimedCalls": 1, "cachedPromptTokensObserved": 40, "missingUsageCalls": 1})

    def test_retry_diagnostics_are_harness_verdicts_not_quoted_prompt_text(self):
        verdict = "staged (does not compile yet): duplicate\n  src/a.cpp:5: error: redeclaration of 'x'\n"
        marker = "### Verdict, turn 1 (00:00)\n\n"
        fence = chr(96) * 3
        record = marker + fence + "\n" + verdict + fence + "\n"
        quoted = "Answer:\n\n" + fence + chr(96) + "\n" + record + fence + chr(96) + "\n"
        with tempfile.TemporaryDirectory() as directory:
            attempt = pathlib.Path(directory)
            (attempt / "conversation.md").write_text(
                quoted + record + "\n" + record + "\n"
                + record.replace("turn 1", "turn -1") + "\n"
                + record.replace("turn 1", "turn 3").replace("redeclaration", "syntax error"),
                encoding="utf-8",
            )
            self.assertEqual(repair_diagnostics([attempt]), {"compileFailure": 1, "duplicateDeclaration": 1})

    def test_golden_allowance_is_scoped_and_never_mutes_a_different_diagnostic(self):
        registry = {"engine": "qwen35", "editAddress": "visible-range", "cases": {
            "example": {"key": "current", "golden": {"trajectory": {"calls": 2}}, "allow": {"compileFailure": 1}}
        }}
        recorded = {"compileFailure": 1, "duplicateDeclaration": 1}
        result = compare_golden_path(registry, "example", "current", "qwen35", "visible-range", recorded, {"calls": 3})
        self.assertEqual(result["warnings"], [
            {"kind": "calls", "observed": 3, "allowed": 2},
            {"kind": "duplicateDeclaration", "observed": 1, "allowed": 0},
        ])
        stale = compare_golden_path(registry, "example", "stale", "qwen35", "visible-range", recorded, {"calls": 3})
        self.assertEqual(stale["calibration"], "uncalibrated")
        self.assertEqual(stale["counts"], recorded)
        self.assertEqual(stale["warnings"], [])

    def test_accepted_metadata_does_not_substitute_for_deliverables(self):
        with tempfile.TemporaryDirectory() as directory:
            run = pathlib.Path(directory)
            workflow = run / "state/workflows/w-000001"
            (workflow / "items/app/accepted").mkdir(parents=True)
            (workflow / "report.md").write_text("| app | model | 1 |\n", encoding="utf-8")
            plan = {"items": [{"files": ["src/labels.cpp"]}]}
            result = judge_run(run, plan, run)
            self.assertFalse(result["passed"])
            self.assertEqual(result["lastGate"], ["accepted output missing: src/labels.cpp"])
            final = workflow / "project/src/labels.cpp"
            final.parent.mkdir(parents=True)
            final.write_text('const char* label() { return "ready"; }\n', encoding="utf-8")
            self.assertTrue(judge_run(run, plan, run)["passed"])

    def test_warning_hard_cap_and_missing_usage(self):
        limits = {"warning": {"tokensIn": 100, "tokensOut": 20}, "hard": {"tokensIn": 150, "tokensOut": 30}}
        run = dict(tokensIn=100, tokensOut=20, turns=1, missingUsageCalls=0)
        self.assertEqual(judge_cost(run, limits)[0], "PASS")
        self.assertEqual(judge_cost(dict(run, prefillSeconds=600, prefillTimedCalls=1), limits)[0], "PASS")
        self.assertEqual(judge_cost(dict(run, tokensOut=21), limits)[0], "WARN")
        self.assertEqual(judge_cost(dict(run, tokensOut=30), limits)[0], "WARN")
        self.assertEqual(judge_cost(dict(run, tokensOut=31), limits)[0], "FAIL")
        self.assertEqual(judge_cost(dict(run, tokensOut=21, missingUsageCalls=1), limits)[0], "INCOMPLETE")
        self.assertEqual(judge_cost(dict(run, tokensIn=151, missingUsageCalls=1), limits)[0], "FAIL")
        self.assertEqual(judge_cost(dict(run, costStop={"reason": "stopped"}), limits), ("FAIL", "stopped"))

    def test_case_ceiling_and_unknown_revision_fallback(self):
        policy = json.loads(pathlib.Path(__file__).with_name("t0-token-budget.json").read_text(encoding="utf-8"))
        key = policy["cases"]["fix-op"]["key"]
        self.assertEqual(cost_limits(policy, "fix-op", key), (
            {"warning": {"tokensIn": 13048, "tokensOut": 504},
             "hard": {"tokensIn": 23486, "tokensOut": 1272}}, "case"))
        self.assertEqual(cost_limits(policy, "fix-op", "changed"), (
            {"warning": {"tokensIn": 49010, "tokensOut": 8044},
             "hard": {"tokensIn": 88218, "tokensOut": 14479}}, "suite fallback"))

    def test_calibration_is_pinned_to_the_run_root(self):
        reference = load_cost_policy("qwen35")
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            self.assertEqual(load_cost_policy("qwen35", root), reference)
            updated = {**reference, "fallback": {"tokensIn": 12345, "tokensOut": 678}}
            (root / "token-policy.json").write_text(json.dumps(updated), encoding="utf-8")
            self.assertEqual(load_cost_policy("qwen35", root), updated)
            self.assertIsNone(load_cost_policy("other", root))

    def test_measurement_does_not_change_behavior_gate_or_cancel_for_cost(self):
        self.assertEqual(case_state(3, 3, 3, 1, 1, "measure"), "PASS")
        self.assertEqual(case_state(2, 3, 3, 1, 1, "measure"), "FAIL")
        self.assertEqual(case_state(2, 2, 3, 1, 1, "measure"), "INCOMPLETE")
        self.assertEqual(case_state(3, 3, 3, 1, 1, "enforce"), "FAIL")
        with tempfile.TemporaryDirectory() as directory:
            out = pathlib.Path(directory)
            self.assertEqual(cost_mode(out), "enforce")
            (out / "cost-mode.txt").write_text("measure\n", encoding="utf-8")
            self.assertEqual(cost_mode(out), "measure")
            test_run = out / "scenario/run-1"
            with patch("gate_t0.subprocess.check_output", side_effect=[
                '{"workflows":[{"state":"running"}]}', '{"workflows":[{"state":"succeeded"}]}',
            ]) as command, patch("gate_t0.time.sleep"), patch("gate_t0.measure_cost") as measure_cost:
                self.assertEqual(wait_run(out / "sub0squad", test_run, "qwen35", None), "succeeded")
            measure_cost.assert_not_called()
            self.assertEqual([call.args[0][2] for call in command.call_args_list], ["status", "status"])

    def test_live_watch_waits_at_warning_and_cancels_above_hard_limit(self):
        policy = json.loads(pathlib.Path(__file__).with_name("t0-token-budget.json").read_text(encoding="utf-8"))
        with tempfile.TemporaryDirectory() as directory:
            home = pathlib.Path(directory)
            workflow = home / "state/workflows/w-000001"
            workflow.mkdir(parents=True)
            plan = {"project": "t0-fixture", "items": [{"files": [], "gate": {"tests": [], "minTests": 1},
                                                      "card": {"context": "test"}}]}
            (workflow / "plan.json").write_text(json.dumps(plan), encoding="utf-8")
            commands = []

            def cli(argv, **_):
                commands.append(argv[2])
                return '{"workflows":[{"state":"running"}]}' if argv[2] == "status" else "{}"

            samples = [("", {"tokensIn": 50000, "tokensOut": 8000}), ("", {"tokensIn": 89000, "tokensOut": 9000})]
            with patch("gate_t0.subprocess.check_output", side_effect=cli), patch("gate_t0.time.sleep"), \
                 patch("gate_t0.measure_cost", side_effect=samples):
                self.assertEqual(wait_run(home / "sub0squad", home, "qwen35", policy), "cancelled")
            self.assertEqual(commands, ["status", "status", "cancel"])
            mark = json.loads((home / "cost-stop.json").read_text(encoding="utf-8"))
            self.assertEqual(mark["tokensIn"], 89000)
            self.assertIn("89000 > 88218", mark["reason"])


if __name__ == "__main__":
    unittest.main()

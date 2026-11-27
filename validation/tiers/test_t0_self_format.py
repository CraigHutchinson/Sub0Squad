import unittest

from t0_self_format import check_report
from t0_self_format_check import inspect


class CheckReportTest(unittest.TestCase):
    def test_only_complete_boolean_reports_are_accepted(self):
        self.assertEqual(check_report('{"view":true,"search":true,"edit":false,"rename":true,"request":true}')['edit'], False)
        for text in ('{}', 'not JSON', '{"view":true}',
                     '{"view":1,"search":true,"edit":true,"rename":true,"request":true}'):
            self.assertIsNone(check_report(text))

    def test_calls_are_parsed_without_repair_or_execution(self):
        self.assertFalse(inspect('view<|content_invoke_tool_json|>{"name"""view","args":{}}')["parsed"])
        self.assertTrue(inspect('view<|content_invoke_tool_json|>{"name":"view","args":{"path":"a"}}')["parsed"])
        self.assertTrue(inspect('request(capability_needed="tests", reason="needed", context="none")')["parsed"])
        self.assertFalse(inspect('request(x=__import__("os"))')["parsed"])


if __name__ == '__main__':
    unittest.main()

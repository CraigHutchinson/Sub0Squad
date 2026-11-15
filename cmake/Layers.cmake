# The component layers (docs/COMPONENTS.md), one list of file stems per layer: the single source for BOTH the include check (CheckLayers.cmake) and
# the build (CMakeLists.txt makes one static library per list). A stem is a `src/<stem>.cpp`, an `include/sub0squad/<stem>.hpp`, or both.
set(SUB0SQUAD_LAYER_NAMES kernel runtime domain harnesses agents orchestration presentation)
set(LAYER0 build_id paths frame unix_socket journal job_spec job_store process_tree progress event_channel host_guard harness)
set(LAYER1 shim supervisor status mock_worker)
set(LAYER2 attempt_failure clarify engine plan division policy predicate mends contract scenario_compare process plan_lint plan_checks ledger workflow workflow_status overseer briefing recall trace triage edits outline canvas turn_control throughput qa)
set(LAYER3 harness_registry harness_common harness_web harness_cpp cpp_templates harness_py py_templates calculator_spec web_templates sandbox)
set(LAYER4 llm_agent agent_card canvas_agent fork architect planner optimizer replay scenario blocks)
set(LAYER5 daemon daemon_impl daemon_jobs daemon_workflow daemon_governance)
set(LAYER6 portal portal_impl portal_sampler portal_files portal_page client cli cli_impl cli_jobs cli_project cli_tools cmd_process cmd_archive first_use wizard sub0squad)
